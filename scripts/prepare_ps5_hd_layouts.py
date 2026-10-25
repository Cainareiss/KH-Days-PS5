#!/usr/bin/env python3
"""Adapt a locally extracted KH HD title for the native PS5 controls.

The theater's Settings entry remains available to the PS5 host. Its action
metadata, bar, words, gear and animation are preserved; its desktop key is
replaced by the collection's PlayStation Triangle icon. Confirm/back hints use
the same icon atlas and menu font, with the native input map (Circle = DS A,
Cross = DS B). No commercial assets are distributed here.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil

from PIL import Image


LABELS = {
    "en": ("CONFIRM", "BACK"),
    "fr": ("CONFIRMER", "RETOUR"),
    "de": ("BESTÄTIGEN", "ZURÜCK"),
    "it": ("CONFERMA", "INDIETRO"),
    "es": ("CONFIRMAR", "VOLVER"),
}


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def piece_blocks(lines: list[str]) -> list[tuple[int, int]]:
    """Return whole piece records, stopping before another clip/loop header."""
    starts = [i for i, line in enumerate(lines) if line.startswith("piece ")]
    result = []
    for at in starts:
        end = at + 1
        while end < len(lines) and not lines[end].startswith(("piece ", "clip ", "loop ")):
            end += 1
        result.append((at, end))
    return result


def adapt_settings_prompt(text: str) -> tuple[list[str], list[list[str]], int]:
    """Keep Settings actionable and replace Esc even without runtime prompt lookup.

    The shared compositor uses ``button open_settings`` for the title action
    and ``prompt open_settings`` to choose the active controller glyph. Keep
    both records. The piece's fallback pixels and rectangle also use Triangle,
    so a preview or an unavailable input profile cannot expose a desktop key.
    """
    lines = text.splitlines()
    buttons = [line for line in lines if line.startswith("button open_settings ")]
    if not buttons:
        return lines, [], 0
    if len(buttons) != 1:
        raise ValueError("expected one Settings button per title layout")
    bounds = tuple(map(float, buttons[0].split()[2:]))
    if len(bounds) != 4 or bounds[0] >= bounds[2] or bounds[1] >= bounds[3]:
        raise ValueError("invalid Settings rectangle")
    x, y, width, height = icon_rect(text, "playstation:north")
    fades: list[list[str]] = []
    adapted = 0
    for start, end in piece_blocks(lines):
        block = lines[start:end]
        if "prompt open_settings" not in block:
            continue
        keys = [line for line in block if line.startswith("key ")]
        if not keys:
            raise ValueError("Settings prompt has no fade keys")
        fades.append(keys)
        fields = lines[start].split()
        fields[1:5] = map(str, (x, y, width, height))
        lines[start] = " ".join(fields)
        for at in range(start + 1, end):
            if not lines[at].startswith("key "):
                continue
            fields = lines[at].split()
            if len(fields) != 10:
                raise ValueError("invalid Settings prompt key")
            x1, y0, y1 = map(float, (fields[4], fields[3], fields[5]))
            # Same aspect-preserving placement as composition.c:draw_animation:
            # use the piece's height and retain its right edge.
            fields[2] = f"{x1 - (y1 - y0) * width / height:.2f}"
            lines[at] = " ".join(fields)
        adapted += 1
    if not fades:
        raise ValueError("Settings button does not have complete prompt/fade records")
    return lines, fades, adapted


def icon_rect(text: str, name: str) -> tuple[int, int, int, int]:
    for line in text.splitlines():
        if line.startswith(f"icon {name} "):
            rect = tuple(map(int, line.split()[2:]))
            if len(rect) == 4 and min(rect[2:]) > 0:
                return rect
    raise ValueError(f"title atlas does not declare {name}")


def font_glyphs(settings: Path) -> tuple[Image.Image, dict[int, tuple[int, int, int]]]:
    lines = settings.read_text(encoding="utf-8").splitlines()
    font_line = next(line.split() for line in lines if line.startswith("font "))
    image = Image.open(settings.parent / font_line[1]).convert("L")
    glyphs = {}
    for line in lines:
        if line.startswith("glyph "):
            _, code, x, y, width = line.split()
            glyphs[int(code)] = (int(x), int(y), int(width))
    return image, glyphs


def text_image(text: str, font: Image.Image, glyphs: dict[int, tuple[int, int, int]]) -> Image.Image:
    widths = [(glyphs[ord(ch)][2] if ch != " " else 12) for ch in text]
    mask = Image.new("L", (sum(w + 2 for w in widths), 32), 0)
    left = 0
    for ch, width in zip(text, widths):
        if ch != " ":
            x, y, _ = glyphs[ord(ch)]
            mask.paste(font.crop((x, y, x + width, y + 32)), (left, 0))
        left += width + 2
    result = Image.new("RGBA", mask.size, (28, 28, 38, 0))
    result.putalpha(mask)
    return result


def hint_strip(atlas: Image.Image, layout_text: str, settings: Path, labels: tuple[str, str], back: bool) -> Image.Image:
    font, glyphs = font_glyphs(settings)
    items = [("playstation:east", labels[0])]
    if back:
        items.append(("playstation:south", labels[1]))
    pieces = []
    for icon, label in items:
        x, y, width, height = icon_rect(layout_text, icon)
        glyph = atlas.crop((x, y, x + width, y + height))
        glyph = glyph.resize((44, 44), Image.Resampling.LANCZOS)
        words = text_image(label, font, glyphs)
        words = words.resize((round(words.width * 1.35), round(words.height * 1.35)), Image.Resampling.LANCZOS)
        item = Image.new("RGBA", (44 + 12 + words.width + 38, 52), (0, 0, 0, 0))
        item.alpha_composite(glyph, (0, 4))
        item.alpha_composite(words, (56, 4))
        pieces.append(item)
    strip = Image.new("RGBA", (sum(piece.width for piece in pieces), 52), (0, 0, 0, 0))
    left = 0
    for piece in pieces:
        strip.alpha_composite(piece, (left, 0))
        left += piece.width
    return strip


def add_hints(lines: list[str], fades: list[list[str]], rect: tuple[int, int, int, int]) -> list[str]:
    """Add one local hint piece per clip, taking that clip's existing fades."""
    x, y, width, height = rect
    result = []
    fade_index = -1

    def finish_clip() -> None:
        if fade_index < 0:
            return
        if fade_index >= len(fades):
            raise ValueError("title clip has no original settings fade")
        # Last in the clip: the theater's full-screen ground would obscure a
        # hint emitted before its existing pieces.
        result.append(f"piece {x} {y} {width} {height} over " + " ".join(["1"] * 16))
        for key in fades[fade_index]:
            fields = key.split()
            # Fixed to the white lower-left area; opacity follows the
            # original title fade instead of staying visible over logos.
            result.append(f"key {fields[1]} 48 1008 {48 + width} {1008 + height} " + " ".join(fields[6:]))

    for line in lines:
        if line.startswith(("clip ", "loop ")):
            finish_clip()
            fade_index += 1
        result.append(line)
    finish_clip()
    if fade_index + 1 != len(fades):
        raise ValueError("settings fades do not match title clips")
    return result


def adapt_pack(source: Path, destination: Path) -> dict:
    source = source.resolve(strict=True)
    destination = destination.resolve()
    if source == destination or source in destination.parents or destination in source.parents:
        raise ValueError("source and destination must be separate trees")
    if destination.exists():
        raise ValueError("destination already exists; preserve it or choose a new output")
    if not (source / "layouts/title").is_dir():
        raise ValueError("source must be the extracted HD PACK directory")
    # Prepare everything first so missing icon/font data cannot leave a
    # partially transformed output that looks ready for packaging.
    plans = []
    atlases = []
    for language_dir in sorted((source / "layouts/title").iterdir()):
        if not language_dir.is_dir():
            continue
        originals = [(path, path.read_text(encoding="utf-8")) for path in sorted(language_dir.glob("*.txt"))]
        reference = next((text for _, text in originals if "button open_settings " in text), None)
        if reference is None:
            continue
        atlas = Image.open(language_dir / "atlas.png").convert("RGBA")
        labels = LABELS.get(language_dir.name, LABELS["en"])
        settings = source / "layouts/settings" / language_dir.name / "settings.txt"
        confirm = hint_strip(atlas, reference, settings, labels, False)
        both = hint_strip(atlas, reference, settings, labels, True)
        if max(confirm.width, both.width) > atlas.width:
            raise ValueError("control hints do not fit the title atlas")
        enlarged = Image.new("RGBA", (atlas.width, atlas.height + confirm.height + both.height + 4), (0, 0, 0, 0))
        enlarged.alpha_composite(atlas)
        enlarged.alpha_composite(confirm, (0, atlas.height + 1))
        enlarged.alpha_composite(both, (0, atlas.height + confirm.height + 3))
        atlases.append((language_dir.relative_to(source) / "atlas.png", enlarged))
        for path, text in originals:
            lines, fades, adapted = adapt_settings_prompt(text)
            if not adapted:
                continue
            back = path.name.startswith(("story_", "mission_"))
            strip = both if back else confirm
            row = atlas.height + (confirm.height + 3 if back else 1)
            lines = add_hints(lines, fades, (0, row, strip.width, strip.height))
            lines.insert(0, "# Native PS5: Circle confirms; Cross goes back; Triangle opens Settings from this title.")
            plans.append((path, "\n".join(lines) + "\n", adapted))
    if not plans:
        raise ValueError("no desktop title controls were found to adapt")
    shutil.copytree(source, destination)
    for relative, atlas in atlases:
        atlas.save(destination / relative)
    report = {"source": str(source), "destination": str(destination), "native_controls": {
        "confirm": "Circle (DS A)", "back": "Cross (DS B)",
        "settings": "Triangle at the title", "pause": "Options (DS Start)"}, "layouts": []}
    for original, text, adapted in plans:
        output = destination / original.relative_to(source)
        output.write_text(text, encoding="utf-8", newline="\n")
        report["layouts"].append({"path": str(original.relative_to(source)), "source_sha256": digest(original),
                                  "output_sha256": digest(output), "adapted_settings_prompts": adapted,
                                  "settings_action_preserved": True})
    (destination / "ps5-title-adaptation.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True, help="locally extracted mods/HD PACK")
    parser.add_argument("--out", type=Path, required=True, help="new, separate mods/HD PACK directory")
    args = parser.parse_args()
    report = adapt_pack(args.source, args.out)
    print(f"PS5 title layouts: {len(report['layouts'])}; output: {args.out}")


if __name__ == "__main__":
    main()
