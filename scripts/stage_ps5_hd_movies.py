#!/usr/bin/env python3
"""Stage verified MPEG-1/MP2 HD theater movies for the native PS5 runtime.

The upstream runtime looks for data/cutscenes/cinematics/hd<ID>.mpg on PS5.
This copies only converted files whose hashes and decoder proof are present in
the conversion manifest, plus the matching collection subtitle/atlas tree.
It never edits the PC collection or an existing staging directory.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import tempfile
from typing import Any


MOVIE_ID = re.compile(r"^[0-9]+$")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(4 * 1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def validate_manifest(manifest_path: Path) -> tuple[dict[str, Any], list[tuple[str, Path]]]:
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("settings", {}).get("audio") != "mp2/48000/stereo/192k":
        raise ValueError("native PS5 movie audio must be stereo MP2 at 48 kHz")
    records = manifest.get("movies")
    if not isinstance(records, dict) or not records:
        raise ValueError("conversion manifest contains no verified movies")
    items = []
    for movie_id, record in sorted(records.items(), key=lambda pair: int(pair[0])):
        if not MOVIE_ID.fullmatch(movie_id) or record.get("id") != movie_id:
            raise ValueError(f"unsafe or inconsistent movie ID: {movie_id!r}")
        output = record.get("output", {})
        media = output.get("media", {})
        video = media.get("video", {})
        audio = media.get("audio", {})
        pl_mpeg = output.get("pl_mpeg", {})
        source = Path(record.get("output_path", ""))
        if (not source.is_file() or record.get("decoder_ready") is not True or
                output.get("sha256") != sha256(source) or video is None or
                video.get("codec_name") != "mpeg1video" or
                pl_mpeg.get("video_frames", 0) < 1 or
                pl_mpeg.get("audio_samples", 0) < 1 or
                audio.get("codec_name") != "mp2" or
                audio.get("sample_rate") != "48000" or audio.get("channels") != 2):
            raise ValueError(f"movie {movie_id} is not a hash-matched MPEG-1/MP2 decoder pass")
        items.append((movie_id, source))
    return manifest, items


def stage(manifest_path: Path, subtitles: Path, destination: Path) -> dict[str, Any]:
    manifest_path = manifest_path.resolve(strict=True)
    subtitles = subtitles.resolve(strict=True)
    destination = destination.resolve()
    if not subtitles.is_dir():
        raise ValueError("subtitle root must be a directory")
    if destination.exists():
        raise ValueError("destination already exists; choose a new path to preserve prior builds")
    manifest, items = validate_manifest(manifest_path)
    if not any(subtitles.rglob("*.txt")) or not any(subtitles.rglob("*.png")):
        raise ValueError("subtitle schedules and their PNG atlas are both required")

    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="ps5-hd-movies-", dir=destination.parent) as temporary:
        temporary_root = Path(temporary)
        cinematics = temporary_root / "data" / "cutscenes" / "cinematics"
        cinematics.mkdir(parents=True)
        shutil.copytree(subtitles, cinematics / "subtitles")
        staged = []
        for movie_id, source in items:
            target = cinematics / f"hd{movie_id}.mpg"
            shutil.copy2(source, target)
            staged.append({"id": movie_id, "path": str(target.relative_to(temporary_root)).replace("\\", "/"),
                           "bytes": target.stat().st_size, "sha256": sha256(target)})
        (cinematics / "ps5-hd-movies-manifest.json").write_text(
            json.dumps({"source_manifest_sha256": sha256(manifest_path), "movies": staged},
                       indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        report = {"manifest": str(manifest_path), "movie_count": len(staged),
                  "total_movie_bytes": sum(entry["bytes"] for entry in staged),
                  "subtitle_root": str(subtitles), "movies": staged}
        (temporary_root / "ps5-hd-movies-stage-report.json").write_text(
            json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        temporary_root.replace(destination)
        return report


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--subtitles", type=Path, required=True,
                        help="cinematics/subtitles directory with schedules and PNG atlas")
    parser.add_argument("--out", type=Path, required=True,
                        help="new HD assets root accepted by build_native_exfat.ps1 -HdAssetsRoot")
    args = parser.parse_args()
    report = stage(args.manifest, args.subtitles, args.out)
    print(f"Staged {report['movie_count']} decoder-verified HD movies "
          f"({report['total_movie_bytes'] / (1024 ** 3):.2f} GiB): {args.out}")


if __name__ == "__main__":
    main()
