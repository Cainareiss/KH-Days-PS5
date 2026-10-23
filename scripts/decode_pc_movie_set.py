#!/usr/bin/env python3
"""Decode the complete local Epic Days MP4 set into the port naming scheme."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


PATTERN = bytes.fromhex("3f1ec193264258b8aa9d5375bf629a97")
SCRAMBLED = 0x100


def decode(source: Path, target: Path) -> int:
    target.parent.mkdir(parents=True, exist_ok=True)
    with source.open("rb") as src, target.open("wb") as dst:
        head = bytearray(src.read(SCRAMBLED))
        for i in range(len(head)):
            head[i] ^= PATTERN[i % len(PATTERN)]
        if len(head) < 8 or head[4:8] != b"ftyp":
            raise ValueError(f"invalid scrambled MP4 header: {source}")
        dst.write(head)
        while True:
            block = src.read(1024 * 1024)
            if not block:
                break
            dst.write(block)
    return target.stat().st_size


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--movies", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args()
    movies = args.movies.resolve()
    out = args.out.resolve()
    if not movies.is_dir():
        raise SystemExit(f"movie directory not found: {movies}")

    records = []
    for source in sorted(movies.glob("hd*.mp4")):
        target = out / source.name
        records.append({"source": source.name, "target": target.name, "bytes": decode(source, target)})
    aliases = {"DOP.mp4": "hd802.mp4", "DED.mp4": "hd843.mp4"}
    for source_name, target_name in aliases.items():
        source = movies / source_name
        if source.is_file():
            target = out / target_name
            records.append({"source": source_name, "target": target_name, "bytes": decode(source, target)})
    # CED is present in the collection but is not referenced by the pinned
    # theater helper. Preserve it without guessing an in-game movie ID.
    extra = movies / "CED.mp4"
    if extra.is_file():
        target = out / "source-extra" / extra.name
        records.append({"source": extra.name, "target": str(target.relative_to(out)), "bytes": decode(extra, target)})

    report = {"source": str(movies), "decoded": len(records), "records": records}
    (out / "complete-movie-set.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({"decoded": len(records), "out": str(out)}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
