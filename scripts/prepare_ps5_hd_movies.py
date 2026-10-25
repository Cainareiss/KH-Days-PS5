#!/usr/bin/env python3
"""Prepare local PC movies for the native PS5 runtime's pl_mpeg replacement path.

The shipped runtime cannot decode H.264 MP4. Its already linked video_mod.c can
decode MPEG-1/MP2, shared with the native HD movie backend. This script writes
mods/<mod>/movies/hd<DS id>.mpg so video_mod.c cannot intercept and truncate
extended theater cuts at the cartridge movie's end. Only IDs actually present
in the supplied cartridge's mv/*.mods directory are converted. Theater subtitle
glyphs are burned from the player's extracted PNG atlas and frame schedule.

Run without --execute to inspect the plan. --sample-seconds benchmarks a selected
clip without producing a package-ready replacement. --execute requires the local
pl_mpeg probe built from tests/native_pl_mpeg_probe.c. Resuming checks the source,
subtitle inputs, conversion settings, destination hash and decoder proof.
"""

from __future__ import annotations

import argparse
from fractions import Fraction
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import time
from typing import Any


VERSION = 2
FPS = "30000/1001"
MOVIE_NAME = re.compile(r"hd([0-9]+)\.mp4", re.IGNORECASE)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(4 * 1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def save_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    temporary.replace(path)


def probe_media(path: Path, ffprobe: str) -> dict[str, Any]:
    process = subprocess.run(
        [ffprobe, "-v", "error", "-show_entries",
         "stream=codec_name,codec_type,width,height,r_frame_rate,sample_rate,channels,start_time,duration:format=duration",
         "-of", "json", str(path)], capture_output=True, text=True, check=True,
    )
    result = json.loads(process.stdout)
    videos = [s for s in result.get("streams", []) if s.get("codec_type") == "video"]
    audio = [s for s in result.get("streams", []) if s.get("codec_type") == "audio"]
    if len(videos) != 1:
        raise ValueError(f"expected one video stream: {path}")
    duration = float(result.get("format", {}).get("duration", videos[0].get("duration", 0)))
    if duration <= 0:
        raise ValueError(f"invalid movie duration: {path}")
    return {"video": videos[0], "audio": audio[0] if audio else None, "duration": duration}


def read_ds_movie(path: Path) -> dict[str, Any]:
    with path.open("rb") as source:
        header = source.read(0x30)
    if len(header) != 0x30 or header[:4] != b"MODS":
        raise ValueError(f"not a DS MODS movie: {path}")
    frames, width, height, fixed_rate = struct.unpack_from("<4I", header, 8)
    fps = fixed_rate / float(0x1000000)
    if frames == 0 or width == 0 or height == 0 or fps <= 0:
        raise ValueError(f"invalid DS MODS header: {path}")
    return {"frames": frames, "width": width, "height": height,
            "fps": fps, "duration": frames / fps, "header_sha256": hashlib.sha256(header).hexdigest()}


def read_subtitles(path: Path) -> dict[str, Any]:
    """Read the same picture/rate/atlas/line contract as movie_subtitles_ps5.c."""
    picture = [1920, 1080]
    rate = 30
    atlas_name = ""
    rows: list[list[int]] = []
    for number, original in enumerate(path.read_text(encoding="utf-8-sig").splitlines(), 1):
        fields = original.partition("#")[0].split()
        if not fields:
            continue
        directive, values = fields[0], fields[1:]
        try:
            if directive == "picture":
                if len(values) != 2:
                    raise ValueError("picture needs two dimensions")
                picture = [int(v) for v in values]
                if any(v < 1 or v > 16384 for v in picture):
                    raise ValueError("invalid picture dimensions")
            elif directive == "rate":
                if len(values) != 1:
                    raise ValueError("rate needs one number")
                rate = int(values[0])
                if not 1 <= rate <= 1000:
                    raise ValueError("invalid subtitle rate")
            elif directive == "atlas":
                if (len(values) != 1 or "\\" in values[0] or ":" in values[0]
                        or values[0].startswith("/") or any(p in ("", ".", "..") for p in values[0].split("/"))):
                    raise ValueError("invalid relative atlas name")
                atlas_name = values[0]
            elif directive == "line":
                if len(values) != 8:
                    raise ValueError("line needs eight numbers")
                row = [int(v) for v in values]
                first, end, _x, _y, u, v, w, h = row
                if first < 0 or end <= first or min(u, v) < 0 or min(w, h) < 1 or max(w, h) > 16384:
                    raise ValueError("invalid subtitle rectangle or time")
                rows.append(row)
                if len(rows) > 32768:
                    raise ValueError("too many subtitle lines")
        except ValueError as error:
            raise ValueError(f"{path}:{number}: {error}") from error
    if not atlas_name:
        raise ValueError(f"subtitle schedule has no atlas: {path}")
    atlas = path.parent / atlas_name
    if not atlas.is_file():
        raise ValueError(f"missing subtitle atlas: {atlas}")
    return {"picture": picture, "rate": rate, "atlas": str(atlas.resolve()), "lines": rows,
            "schedule_sha256": sha256(path), "atlas_sha256": sha256(atlas)}


def subtitle_states(schedule: dict[str, Any], duration: float) -> list[tuple[float, float, list[int]]]:
    """Nonoverlapping intervals preserve simultaneous rows and clear gaps."""
    events: dict[float, list[tuple[int, bool]]] = {0.0: [], duration: []}
    for index, row in enumerate(schedule["lines"]):
        first, end = row[:2]
        start, stop = first / schedule["rate"], min(end / schedule["rate"], duration)
        if start >= duration:
            continue
        events.setdefault(start, []).append((index, True))
        events.setdefault(stop, []).append((index, False))
    points = sorted(events)
    active: set[int] = set()
    result = []
    for i, start in enumerate(points[:-1]):
        for index, shown in events[start]:
            if shown:
                active.add(index)
            else:
                active.discard(index)
        stop = points[i + 1]
        if stop > start:
            result.append((start, stop, sorted(active)))
    return result


def write_subtitle_concat(schedule: dict[str, Any], folder: Path, width: int, height: int,
                          duration: float) -> Path:
    """Make sparse timed RGBA pictures; one FFmpeg overlay retains atlas glyphs."""
    from PIL import Image

    folder.mkdir(parents=True, exist_ok=True)
    atlas = Image.open(schedule["atlas"]).convert("RGBA")
    if max(atlas.size) > 16384 or atlas.width * atlas.height > 64 * 1024 * 1024:
        raise ValueError("subtitle atlas exceeds the runtime's dimension limits")
    for row in schedule["lines"]:
        u, v, w, h = row[4:]
        if u + w > atlas.width or v + h > atlas.height:
            raise ValueError("subtitle source rectangle exceeds the atlas")
    scale_x, scale_y = width / schedule["picture"][0], height / schedule["picture"][1]
    files: dict[tuple[int, ...], str] = {}
    listing = ["ffconcat version 1.0"]
    for _start, stop, active in subtitle_states(schedule, duration):
        key = tuple(active)
        if key not in files:
            name = f"subtitle-{len(files):05d}.png"
            picture = Image.new("RGBA", (width, height), (0, 0, 0, 0))
            for index in active:
                _first, _end, x, y, u, v, w, h = schedule["lines"][index]
                glyph = atlas.crop((u, v, u + w, v + h))
                left, top = round(x * scale_x), round(y * scale_y)
                right, bottom = round((x + w) * scale_x), round((y + h) * scale_y)
                glyph = glyph.resize((max(1, right - left), max(1, bottom - top)), Image.Resampling.LANCZOS)
                picture.alpha_composite(glyph, (left, top))
            picture.save(folder / name)
            files[key] = name
        listing += [f"file {files[key]}", "option framerate 1000", f"duration {stop - _start:.9f}"]
    # concat requires a final repeated packet for the preceding duration. Keep it
    # transparent so an early atlas EOF cannot leave the previous caption stuck.
    blank = folder / "subtitle-end.png"
    Image.new("RGBA", (width, height), (0, 0, 0, 0)).save(blank)
    listing += [f"file {blank.name}", "option framerate 1000"]
    target = folder / "subtitles.ffconcat"
    target.write_text("\n".join(listing) + "\n", encoding="utf-8")
    return target


def make_plan(movies: Path, game_movies: Path, subtitles_language: str,
              ffprobe: str, only: set[str] | None = None) -> dict[str, Any]:
    ds = {p.stem: p for p in game_movies.glob("*.mods")}
    if not ds:
        raise ValueError(f"no DS *.mods movies found: {game_movies}")
    records, skipped = [], []
    for source in sorted(movies.glob("hd*.mp4")):
        match = MOVIE_NAME.fullmatch(source.name)
        if not match:
            continue
        movie = match[1]
        if movie not in ds:
            skipped.append({"source": source.name, "reason": "no matching DS movie"})
            continue
        if only is not None and movie not in only:
            continue
        media, game = probe_media(source, ffprobe), read_ds_movie(ds[movie])
        subtitle_path = movies / "subtitles" / subtitles_language / f"hd{movie}.txt"
        subtitles = read_subtitles(subtitle_path) if subtitle_path.is_file() else None
        records.append({"id": movie, "source": str(source.resolve()), "source_bytes": source.stat().st_size,
                        "source_sha256": sha256(source), "source_media": media, "ds": game,
                        "duration_difference_seconds": media["duration"] - game["duration"],
                        "subtitle_schedule": str(subtitle_path.resolve()) if subtitles else None,
                        "subtitle_input": subtitles})
    if not records:
        raise ValueError("no selected PC videos match the supplied DS movie IDs")
    if only and only - {r["id"] for r in records}:
        raise ValueError(f"selected IDs have no matching source and DS movie: {sorted(only - {r['id'] for r in records})}")
    seconds = sum(r["source_media"]["duration"] for r in records)
    return {"version": VERSION, "records": records, "skipped_pc_only": skipped,
            "unavailable_ds_ids": sorted(set(ds) - {r["id"] for r in records}),
            "source_seconds": seconds,
            "rough_output_bytes_at_16_mbps": round(seconds * 16_000_000 / 8)}


def conversion_command(ffmpeg: str, source: Path, target: Path, settings: dict[str, Any],
                       duration: float, subtitles_concat: Path | None, audio: bool) -> list[str]:
    command = [ffmpeg, "-hide_banner", "-loglevel", "warning", "-y", "-i", str(source)]
    if subtitles_concat:
        # These ffconcat files are ours. 'option framerate' requires safe=0;
        # their file names are generated basenames, never schedule input text.
        command += ["-f", "concat", "-safe", "0", "-i", str(subtitles_concat)]
        video_filter = (f"[0:v]setpts=PTS-STARTPTS,scale={settings['width']}:{settings['height']}:flags=lanczos,setsar=1[base];"
                        "[1:v]setpts=PTS-STARTPTS[subs];"
                        "[base][subs]overlay=eof_action=pass:repeatlast=1:shortest=0,format=yuv420p[v]")
        command += ["-filter_complex", video_filter, "-map", "[v]"]
    else:
        command += ["-map", "0:v:0", "-vf",
                    f"scale={settings['width']}:{settings['height']}:flags=lanczos,setsar=1,format=yuv420p"]
    if audio:
        command += ["-map", "0:a:0", "-c:a", "mp2", "-b:a", "192k", "-ar", "48000", "-ac", "2"]
    else:
        command += ["-an"]
    command += ["-c:v", "mpeg1video", "-q:v", str(settings["quality"]),
                "-r", settings["fps"], "-threads", str(settings["threads"]),
                "-t", f"{duration:.9f}", "-f", "mpeg", str(target)]
    return command


def check_output(path: Path, ffprobe: str, decoder_probe: Path, settings: dict[str, Any],
                 expected_duration: float, expected_audio: bool) -> dict[str, Any]:
    media = probe_media(path, ffprobe)
    video = media["video"]
    if (video["codec_name"] != "mpeg1video" or video["width"] != settings["width"]
            or video["height"] != settings["height"]):
        raise ValueError(f"output is not the requested MPEG-1 picture: {path}: {video}")
    if expected_audio and (not media["audio"] or media["audio"]["codec_name"] != "mp2"
                           or int(media["audio"]["sample_rate"]) != 48000 or media["audio"]["channels"] != 2):
        raise ValueError(f"output has no compatible MP2 stereo audio: {path}")
    if abs(media["duration"] - expected_duration) > 0.2:
        raise ValueError(f"output duration does not match source: {path}")
    process = subprocess.run([str(decoder_probe.resolve()), str(path.resolve()), "3"],
                             capture_output=True, text=True, check=True)
    decoded = json.loads(process.stdout)
    if (decoded.get("video_frames", 0) < 1 or decoded.get("width") != settings["width"]
            or decoded.get("height") != settings["height"]
            or abs(decoded.get("fps", 0) - float(Fraction(settings["fps"]))) > 0.001
            or (expected_audio and decoded.get("audio_samples", 0) < 1)):
        raise ValueError(f"pl_mpeg did not decode output picture/audio: {path}")
    return {"media": media, "pl_mpeg": decoded, "sha256": sha256(path), "bytes": path.stat().st_size}


def input_fingerprint(record: dict[str, Any], settings: dict[str, Any]) -> str:
    value = {"version": VERSION, "source_sha256": record["source_sha256"],
             "ds": record["ds"], "subtitle_input": record["subtitle_input"], "settings": settings}
    return hashlib.sha256(json.dumps(value, sort_keys=True).encode("utf-8")).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--movies", type=Path, required=True)
    parser.add_argument("--game-movies", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True, help="mod root; movies/hd<id>.mpg is created inside")
    parser.add_argument("--work", type=Path)
    parser.add_argument("--subtitles-language", default="en")
    parser.add_argument("--ffmpeg", default=shutil.which("ffmpeg"))
    parser.add_argument("--ffprobe", default=shutil.which("ffprobe"))
    parser.add_argument("--decoder-probe", type=Path)
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--quality", type=int, default=2)
    parser.add_argument("--threads", type=int, default=4)
    parser.add_argument("--only", nargs="+", help="DS movie IDs")
    parser.add_argument("--execute", action="store_true")
    parser.add_argument("--sample-seconds", type=float, help="benchmark without producing package-ready files")
    args = parser.parse_args()
    if not args.ffprobe or (args.execute and not args.ffmpeg):
        parser.error("FFmpeg and ffprobe must be installed or explicitly supplied")
    if args.execute and (not args.decoder_probe or not args.decoder_probe.is_file()):
        parser.error("--execute requires the compiled local tests/native_pl_mpeg_probe.c probe")
    if (args.width < 16 or args.height < 16 or args.width > 4095 or args.height > 4095
            or args.width % 2 or args.height % 2 or not 1 <= args.quality <= 31 or args.threads < 1):
        parser.error("invalid even picture dimensions, quality or thread count")
    if args.sample_seconds is not None and (not args.only or args.sample_seconds <= 0):
        parser.error("--sample-seconds requires --only and a positive duration")
    settings = {"width": args.width, "height": args.height, "fps": FPS, "quality": args.quality,
                "threads": args.threads, "audio": "mp2/48000/stereo/192k", "subtitles_language": args.subtitles_language}
    out = args.out.resolve()
    work = args.work.resolve() if args.work else out.parent / ".hd-movie-work"
    plan = make_plan(args.movies.resolve(), args.game_movies.resolve(), args.subtitles_language,
                     args.ffprobe, set(args.only) if args.only else None)
    plan["settings"] = settings
    save_json(work / "conversion-plan.json", plan)
    print(json.dumps({"matched_movies": len(plan["records"]), "source_minutes": round(plan["source_seconds"] / 60, 2),
                      "rough_gib_at_16_mbps": round(plan["rough_output_bytes_at_16_mbps"] / 2**30, 2),
                      "pc_only_skipped": len(plan["skipped_pc_only"]), "plan": str(work / "conversion-plan.json")}), flush=True)
    if not args.execute:
        return 0
    manifest_path = out / "ps5-hd-movies-manifest.json"
    manifest: dict[str, Any] = {"version": VERSION, "settings": settings, "movies": {}}
    if manifest_path.is_file() and args.sample_seconds is None:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    for number, record in enumerate(plan["records"], 1):
        movie = record["id"]
        fingerprint = input_fingerprint(record, settings)
        duration = min(record["source_media"]["duration"], args.sample_seconds or float("inf"))
        target = ((work / "samples" / f"{movie}.mpg") if args.sample_seconds else out / "movies" / f"hd{movie}.mpg")
        prior = manifest.get("movies", {}).get(movie)
        if (not args.sample_seconds and target.is_file() and prior
                and prior.get("input_fingerprint") == fingerprint and prior.get("output", {}).get("sha256") == sha256(target)):
            verified = check_output(target, args.ffprobe, args.decoder_probe, settings, duration, bool(record["source_media"]["audio"]))
            print(f"[{number}/{len(plan['records'])}] {movie}: verified existing MPEG-1/MP2, {verified['bytes']} bytes", flush=True)
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        temporary = target.with_suffix(".partial.mpg")
        subtitle_concat = (write_subtitle_concat(record["subtitle_input"], work / movie / "subtitles",
                                                args.width, args.height, duration)
                           if record["subtitle_input"] else None)
        command = conversion_command(args.ffmpeg, Path(record["source"]), temporary, settings, duration,
                                     subtitle_concat, bool(record["source_media"]["audio"]))
        log = work / movie / ("sample-ffmpeg.log" if args.sample_seconds else "ffmpeg.log")
        log.parent.mkdir(parents=True, exist_ok=True)
        print(f"[{number}/{len(plan['records'])}] {movie}: converting {duration:.2f}s, subtitles={subtitle_concat is not None}", flush=True)
        started = time.monotonic()
        with log.open("w", encoding="utf-8") as errors:
            subprocess.run(command, stdout=subprocess.DEVNULL, stderr=errors, check=True)
        verified = check_output(temporary, args.ffprobe, args.decoder_probe, settings, duration, bool(record["source_media"]["audio"]))
        elapsed = time.monotonic() - started
        temporary.replace(target)
        result = {"id": movie, "input_fingerprint": fingerprint, "source": record["source"],
                  "source_sha256": record["source_sha256"], "subtitle_schedule": record["subtitle_schedule"],
                  "subtitle_lines_burned": len(record["subtitle_input"]["lines"]) if record["subtitle_input"] else 0,
                  "duration_difference_seconds": record["duration_difference_seconds"],
                  "seconds_to_convert": round(elapsed, 3), "output": verified,
                  "output_path": str(target), "decoder_ready": True, "sample_only": args.sample_seconds is not None}
        if args.sample_seconds:
            result["projected_full_bytes"] = round(verified["bytes"] / duration * record["source_media"]["duration"])
            result["projected_full_conversion_seconds"] = round(elapsed / duration * record["source_media"]["duration"])
            save_json(work / "samples" / f"{movie}.validation.json", result)
        else:
            manifest["movies"][movie] = result
            save_json(manifest_path, manifest)
        print(json.dumps({"id": movie, "bytes": verified["bytes"], "elapsed_seconds": round(elapsed, 2),
                          "speed_vs_realtime": round(duration / elapsed, 2), "decoder_ready": True,
                          "sample_only": args.sample_seconds is not None}), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
