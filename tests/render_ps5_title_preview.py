"""Draw a local adapted title with the pinned upstream preview renderer.

This produces local review images only; commercial artwork is never included
in the repository. No PS5 runtime behavior is simulated by this preview.
"""
from __future__ import annotations

import argparse
import importlib.util
from pathlib import Path

import numpy as np
from PIL import Image


def render_preview(pack: Path, upstream: Path, output: Path) -> None:
    spec = importlib.util.spec_from_file_location("upstream_theater_title", upstream / "tools/hd/theater_title.py")
    assert spec and spec.loader
    theater = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(theater)
    output.mkdir(parents=True, exist_ok=True)
    for name in ("root_3_0", "story_2_0"):
        layout = pack / f"layouts/title/en/{name}.txt"
        lines = layout.read_text(encoding="utf-8").splitlines()
        atlas = np.asarray(Image.open(layout.parent / "atlas.png").convert("RGBA"))
        pieces, texels = [], []
        in_loop = False
        for line in lines:
            parts = line.split()
            if not parts:
                continue
            if parts[0] == "loop":
                in_loop = True
            elif in_loop and parts[0] == "piece":
                values = list(map(float, parts[6:]))
                pieces.append({"piece": {"blend": {"over": 0, "add": 1, "take": 2}[parts[5]],
                                          "corners": [values[i:i+4] for i in range(0, 16, 4)]}, "keys": []})
                texels.append(tuple(map(int, parts[1:5])))
            elif in_loop and parts[0] == "key":
                pieces[-1]["keys"].append((float(parts[1]), list(map(float, parts[2:]))))
        canvas = theater.render(atlas, texels, pieces, 20, (1920, 1080))
        image = Image.fromarray((canvas * 255 + 0.5).astype(np.uint8))
        image.save(output / f"{name}-ps5-1080p.png")
        # Catch draw-order regressions: both the large artwork and the new
        # lower-left control legend must survive the full-screen ground.
        hint = np.asarray(image)[1008:1060, 48:750]
        assert np.count_nonzero(hint.min(axis=2) < 100) > 200, "native control hints are obscured"
        print(output / f"{name}-ps5-1080p.png")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pack", required=True, type=Path)
    parser.add_argument("--upstream", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    render_preview(args.pack, args.upstream, args.out)
