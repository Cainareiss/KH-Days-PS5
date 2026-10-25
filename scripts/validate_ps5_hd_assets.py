#!/usr/bin/env python3
"""Reject incomplete HD inputs even when every copied byte has a valid hash.

The PS5 title loader needs the English layout variants and their atlas. Its
Settings action must survive conversion to Triangle, and HD movie IDs must map
to the exact MPEG paths opened by the runtime. This checks that contract before
packaging, then compares the staged files with the validated source inventory.
It does not claim that console rendering or playback has been hardware tested.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path, PurePosixPath
import re
import struct
import sys
from typing import Any


PACK = PurePosixPath("mods/HD PACK")
TITLE = PACK / "layouts/title/en"
CINEMATICS = PurePosixPath("data/cutscenes/cinematics")
MANIFEST = CINEMATICS / "ps5-hd-movies-manifest.json"
TITLE_NAMES = (
    "intro_0_0", "logo_3_0", "logo_3_1", "logo_3_2",
    "root_2_0", "root_2_1", "root_3_0", "root_3_1", "root_3_2",
    "story_1_0", "story_2_0", "story_2_1", "mission_2_0", "mission_2_1",
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(4 * 1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def required_file(root: Path, relative: PurePosixPath) -> Path:
    path = root.joinpath(*relative.parts)
    if not path.is_file() or path.stat().st_size == 0:
        raise ValueError(f"required HD runtime asset missing or empty: {relative}")
    if not path.resolve().is_relative_to(root):
        raise ValueError(f"HD asset escapes its input tree: {relative}")
    return path


def png_size(path: Path) -> tuple[int, int]:
    with path.open("rb") as stream:
        header = stream.read(33)
    if (len(header) != 33 or header[:8] != b"\x89PNG\r\n\x1a\n" or
            header[8:16] != b"\x00\x00\x00\rIHDR"):
        raise ValueError(f"invalid HD atlas PNG: {path}")
    width, height = struct.unpack(">II", header[16:24])
    if width < 1 or height < 1:
        raise ValueError(f"empty HD atlas PNG: {path}")
    return width, height


def records(path: Path) -> list[list[str]]:
    return [line.split() for line in path.read_text(encoding="utf-8-sig").splitlines()
            if line.strip() and not line.lstrip().startswith("#")]


def rectangle(fields: list[str], path: Path) -> tuple[float, float, float, float]:
    try:
        values = tuple(float(value) for value in fields)
    except ValueError as error:
        raise ValueError(f"invalid title rectangle: {path}") from error
    if len(values) != 4 or not all(math.isfinite(value) for value in values):
        raise ValueError(f"invalid title rectangle: {path}")
    return values


def validate_title(root: Path) -> dict[str, Any]:
    atlas = required_file(root, TITLE / "atlas.png")
    width, height = png_size(atlas)
    settings_layouts = settings_prompts = 0
    for name in TITLE_NAMES:
        path = required_file(root, TITLE / f"{name}.txt")
        lines = records(path)
        if [line for line in lines if line[0] == "picture"] != [["picture", "1920", "1080"]]:
            raise ValueError(f"HD title must declare a 1920x1080 picture: {path}")
        if [line for line in lines if line[0] == "atlas"] != [["atlas", "atlas.png"]]:
            raise ValueError(f"HD title atlas does not match the runtime layout: {path}")
        # All atlas pieces must remain inside the adapted atlas, including the
        # appended Circle/Cross hints. An old atlas cannot pass with new layouts.
        for line in lines:
            if line[0] == "piece":
                x, y, w, h = rectangle(line[1:5], path)
                if min(x, y) < 0 or min(w, h) <= 0 or x + w > width or y + h > height:
                    raise ValueError(f"HD title piece exceeds its atlas: {path}")
        if not name.startswith(("root_", "story_", "mission_")):
            continue
        buttons = [line for line in lines if line[:2] == ["button", "open_settings"]]
        if len(buttons) != 1:
            raise ValueError(f"HD title requires exactly one Settings button: {path}")
        x0, y0, x1, y1 = rectangle(buttons[0][2:], path)
        if x0 >= x1 or y0 >= y1:
            raise ValueError(f"invalid Settings button bounds: {path}")
        icons = [line for line in lines if line[:2] == ["icon", "playstation:north"]]
        if len(icons) != 1:
            raise ValueError(f"HD title requires the PlayStation Triangle icon: {path}")
        triangle = rectangle(icons[0][2:], path)
        x, y, w, h = triangle
        if min(x, y) < 0 or min(w, h) <= 0 or x + w > width or y + h > height:
            raise ValueError(f"Settings Triangle icon exceeds its atlas: {path}")

        # Validate each clip independently: a single retained prompt elsewhere
        # in the file cannot hide a lost prompt in an animated transition.
        clips: list[list[list[str]]] = []
        for line in lines:
            if line[0] in ("clip", "loop"):
                clips.append([])
            elif clips:
                clips[-1].append(line)
        if not clips:
            raise ValueError(f"Settings title has no animation clips: {path}")
        for clip in clips:
            prompts = [i for i, line in enumerate(clip) if line[:2] == ["prompt", "open_settings"]]
            if len(prompts) != 1:
                raise ValueError(f"each HD title clip requires exactly one Settings prompt: {path}")
            at = prompts[0]
            if at == 0 or clip[at - 1][0] != "piece" or rectangle(clip[at - 1][1:5], path) != triangle:
                raise ValueError(f"Settings prompt fallback must use the Triangle atlas rectangle: {path}")
            if at + 1 >= len(clip) or clip[at + 1][0] != "key":
                raise ValueError(f"Settings prompt is missing its animation keys: {path}")
        settings_layouts += 1
        settings_prompts += len(clips)
    return {"titleLayouts": len(TITLE_NAMES), "settingsLayouts": settings_layouts,
            "settingsPromptClips": settings_prompts,
            "titleAtlas": {"path": str(TITLE / "atlas.png"), "width": width, "height": height}}


def validate_assets(root: Path, expected_report: dict[str, Any] | None = None) -> dict[str, Any]:
    root = root.resolve(strict=True)
    title = validate_title(root)
    manifest_path = required_file(root, MANIFEST)
    manifest = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
    movies = manifest.get("movies")
    if not isinstance(movies, list) or not movies:
        raise ValueError("HD movie manifest contains no verified movies")
    seen: set[str] = set()
    movie_bytes = 0
    movie_hashes = {}
    for movie in movies:
        movie_id = movie.get("id")
        relative = movie.get("path")
        if not isinstance(movie_id, str) or re.fullmatch(r"[0-9]+", movie_id) is None:
            raise ValueError(f"invalid HD movie ID: {movie_id!r}")
        expected_path = str(CINEMATICS / f"hd{movie_id}.mpg")
        if relative != expected_path:
            raise ValueError(f"HD movie {movie_id} must use runtime path {expected_path}, got {relative!r}")
        if relative in seen:
            raise ValueError(f"duplicate HD movie in manifest: {relative}")
        seen.add(relative)
        path = required_file(root, PurePosixPath(relative))
        expected_hash = movie.get("sha256")
        actual_hash = sha256(path)
        if (not isinstance(expected_hash, str) or re.fullmatch(r"[0-9a-fA-F]{64}", expected_hash) is None or
                actual_hash != expected_hash.lower() or movie.get("bytes") != path.stat().st_size):
            raise ValueError(f"HD movie hash/size mismatch: {relative}")
        movie_hashes[relative] = actual_hash
        movie_bytes += path.stat().st_size
    present = {path.relative_to(root).as_posix() for path in (root / CINEMATICS).glob("hd*.mpg")}
    if present != seen:
        raise ValueError(f"HD movie manifest/payload inventory mismatch: {sorted(present ^ seen)}")
    subtitles = root / CINEMATICS / "subtitles/en"
    schedules = list(subtitles.glob("*.txt"))
    atlases = list(subtitles.glob("*.png"))
    if not schedules or not atlases:
        raise ValueError("HD movie package requires English subtitle schedules and atlases")
    for schedule in schedules:
        required_file(root, PurePosixPath(schedule.relative_to(root).as_posix()))
        png_size(required_file(root, PurePosixPath(schedule.with_suffix(".png").relative_to(root).as_posix())))

    inventory = []
    for subtree in (PACK, CINEMATICS):
        for path in sorted((root / subtree).rglob("*")):
            if not path.is_file():
                continue
            relative = path.relative_to(root).as_posix()
            required_file(root, PurePosixPath(relative))
            inventory.append({"path": relative, "bytes": path.stat().st_size,
                              "sha256": movie_hashes.get(relative) or sha256(path)})
    if expected_report is not None:
        if expected_report.get("passed") is not True or not expected_report.get("files"):
            raise ValueError("source HD asset validation report is incomplete")
        expected_files = {entry["path"]: entry for entry in expected_report["files"]}
        actual_files = {entry["path"]: entry for entry in inventory}
        if actual_files != expected_files:
            changed = sorted(path for path in expected_files.keys() | actual_files.keys()
                             if expected_files.get(path) != actual_files.get(path))
            raise ValueError(f"staged HD assets differ from validated source; use a fresh stage: {changed[:8]}")
    return {"passed": True, "root": str(root), **title, "manifest": str(MANIFEST),
            "movieCount": len(movies), "movieBytes": movie_bytes,
            "subtitleSchedules": len(schedules), "subtitleAtlases": len(atlases),
            "hardwarePlaybackTest": "Pending", "files": inventory}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--report", required=True, type=Path)
    parser.add_argument("--expected-report", type=Path, help="source proof to compare against a staged tree")
    args = parser.parse_args()
    try:
        expected = (json.loads(args.expected_report.read_text(encoding="utf-8-sig"))
                    if args.expected_report else None)
        report = validate_assets(args.root, expected)
    except (OSError, ValueError, TypeError, KeyError) as error:
        print(f"HD runtime asset validation failed: {error}", file=sys.stderr)
        return 1
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"HD runtime assets validated: {report['titleLayouts']} title layouts, "
          f"{report['settingsPromptClips']} Triangle prompts, {report['movieCount']} MPEG movies")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
