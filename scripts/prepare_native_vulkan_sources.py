"""Prepare pinned khdays GPU renderer for PS5 without SDL's window system.

The game renderer, captures, VRAM uploads and shader arithmetic stay upstream's.
Only SDL window texture wrappers/readback utilities are removed. Existing
SPIR-V is extracted from the committed multi-backend headers; no shader tool,
network connection or modifications to the pinned source tree are needed.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct


RENDERERS = ("gpu_raster3d", "gfx_render", "gpu_hd")
SHADERS = ("raster3d", "compose", "compose2d", "onescreen", "capture", "shrink")
WINDOW_FUNCTIONS = {
    "gpu_raster3d": ("khdays_raster3d_texture", "khdays_raster3d_read"),
    "gfx_render": ("wrap", "khdays_gfx_render_picture", "khdays_gfx_render_texture",
                   "khdays_gfx_render_captured", "khdays_gfx_render_read"),
    "gpu_hd": (),
}
WINDOW_NAMES = re.compile(r"\b(?:wrapped|wrapped_for|picture_wrapped|picture_for|held_wrapped|held_for)\b")


def matching_brace(text: str, opened: int) -> int:
    """The closing brace, with comments and quoted strings skipped."""
    depth, at = 1, opened + 1
    while at < len(text):
        if text.startswith("//", at):
            end = text.find("\n", at + 2)
            at = len(text) if end < 0 else end + 1
        elif text.startswith("/*", at):
            end = text.find("*/", at + 2)
            if end < 0:
                raise ValueError("unterminated C comment")
            at = end + 2
        elif text[at] in "\"'":
            quote = text[at]
            at += 1
            while at < len(text):
                if text[at] == "\\":
                    at += 2
                elif text[at] == quote:
                    at += 1
                    break
                else:
                    at += 1
        else:
            if text[at] == "{":
                depth += 1
            elif text[at] == "}":
                depth -= 1
                if not depth:
                    return at
            at += 1
    raise ValueError("unterminated C function")


def remove_function(text: str, name: str) -> str:
    pattern = re.compile(r"(?m)^[A-Za-z_][^\n;{}]*\b" + re.escape(name) + r"\s*\([^;{}]*\)\s*\{")
    matches = list(pattern.finditer(text))
    if len(matches) != 1:
        raise ValueError(f"expected one definition of {name}, got {len(matches)}")
    match = matches[0]
    end = matching_brace(text, match.end() - 1) + 1
    if text[end:end + 1] == "\n":
        end += 1
    return text[:match.start()] + text[end:]


def remove_window_state(text: str) -> str:
    """Remove only caches that wrapped GPU images in SDL window textures."""
    lines = text.splitlines(keepends=True)
    result: list[str] = []
    at = 0
    while at < len(lines):
        line = lines[at]
        if not WINDOW_NAMES.search(line) or line.lstrip().startswith(("/*", "*", "//")):
            result.append(line)
            at += 1
            continue
        if re.match(r"\s*if\s*\(", line):
            if "{" in line:
                block = "".join(lines[at:])
                end = matching_brace(block, block.index("{")) + 1
                consumed = block[:end].count("\n") + 1
                at += consumed
            else:
                # The wrapping cache's only unbraced statements destroy its SDL texture.
                if at + 1 >= len(lines) or "SDL_DestroyTexture(" not in lines[at + 1]:
                    raise ValueError(f"unknown unbraced window-state condition: {line.rstrip()}")
                at += 2
            continue
        if ";" not in line or not re.search(r"(?:SDL_Texture|SDL_Renderer|=)", line):
            raise ValueError(f"unknown window-state reference: {line.rstrip()}")
        at += 1
    result_text = "".join(result)
    if re.search(r"\bSDL_(?:DestroyTexture|CreateProperties|Renderer|Texture)\b", result_text):
        raise ValueError("SDL window dependency survived renderer preparation")
    return result_text


def relocate_includes(text: str, origin: Path) -> str:
    def include(match: re.Match[str]) -> str:
        name = match.group(1)
        if not name.startswith("../"):
            return match.group(0)
        target = (origin.parent / name).resolve()
        if not target.is_file():
            raise ValueError(f"missing pinned renderer include: {target}")
        return '#include "' + target.as_posix() + '"'
    return re.sub(r'(?m)^#include "([^"]+)"', include, text)


RAW_ACCESSORS = r"""
/* PS5 obtains the real GPU images directly, without an SDL window or readback. */
SDL_GPUTexture *khdays_ps5_gfx_screen_raw(int engine, int *w, int *h)
{
    if (engine < 0 || engine > 1 || screen[engine] == NULL) return NULL;
    *w = screens_shrunk ? shrunk_screen[engine].w : screen_columns[engine] * screen_scale;
    *h = screens_shrunk ? shrunk_screen[engine].h : KHDAYS_FRAME_H * screen_scale;
    return screens_shrunk ? shrunk_screen[engine].texture : screen[engine];
}
SDL_GPUTexture *khdays_ps5_gfx_captured_raw(int *w, int *h)
{
    if (!khdays_gfx_render_captured_shown()) return NULL;
    *w = held_w; *h = held_h;
    return held;
}
SDL_GPUTexture *khdays_ps5_gfx_picture_raw(int *w, int *h)
{
    if (picture == NULL) return NULL;
    *w = picture_shrunk ? shrunk_picture.w : picture_w;
    *h = picture_shrunk ? shrunk_picture.h : picture_h;
    return picture_shrunk ? shrunk_picture.texture : picture;
}
"""


def adapt_renderer(name: str, source: Path) -> str:
    text = source.read_text(encoding="utf-8")
    for function in WINDOW_FUNCTIONS[name]:
        text = remove_function(text, function)
    text = remove_window_state(text)
    text = relocate_includes(text, source)
    if name == "gfx_render":
        text += RAW_ACCESSORS
    return "/* Prepared for PS5 Vulkan; the pinned upstream file is unchanged. */\n" + text


def adapt_header(text: str) -> str:
    # Remove declarations of only the SDL window wrappers/readback utilities.
    names = tuple(function for functions in WINDOW_FUNCTIONS.values() for function in functions if function != "wrap")
    for name in names:
        text = re.sub(r"(?m)^[A-Za-z_][^\n;{}]*\b" + re.escape(name) + r"\s*\([^;{}]*\);\s*\n", "", text)
    if re.search(r"\bSDL_(?:Renderer|Texture)\b", text):
        raise ValueError("SDL window type survived renderer header preparation")
    return text


def extract_spirv(name: str, source: Path) -> tuple[str, dict[str, object]]:
    text = source.read_text(encoding="utf-8")
    stem = name + "_cs"
    array = re.search(r"static const unsigned char " + stem + r"_spirv\[\] = \{(.*?)\};", text, re.S)
    declared = re.search(r"static const unsigned int " + stem + r"_spirv_size = (\d+)u;", text)
    if not array or not declared:
        raise ValueError(f"missing committed SPIR-V for {name}")
    binary = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]{2})\b", array.group(1)))
    if len(binary) != int(declared.group(1)) or len(binary) % 4:
        raise ValueError(f"invalid committed SPIR-V length for {name}")
    words = struct.unpack("<" + "I" * (len(binary) // 4), binary)
    if words[0] != 0x07230203:
        raise ValueError(f"invalid SPIR-V magic for {name}")
    entrypoint, local_size = False, None
    bindings: dict[int, dict[str, int]] = {}
    at = 5
    while at < len(words):
        count, opcode = words[at] >> 16, words[at] & 0xffff
        if count < 1 or at + count > len(words):
            raise ValueError(f"malformed SPIR-V instruction for {name}")
        operands = words[at + 1:at + count]
        if opcode == 15 and operands[0] == 5:  # OpEntryPoint GLCompute
            entrypoint = struct.pack("<" + "I" * len(operands[2:]), *operands[2:]).split(b"\0", 1)[0] == b"cs_main"
        elif opcode == 16 and operands[1] == 17:  # OpExecutionMode LocalSize
            local_size = list(operands[2:5])
        elif opcode == 71 and operands[1] in (33, 34):  # OpDecorate Binding / DescriptorSet
            bindings.setdefault(operands[0], {})["binding" if operands[1] == 33 else "set"] = operands[2]
        at += count
    if not entrypoint or local_size != [8, 8, 1]:
        raise ValueError(f"unexpected SPIR-V compute entrypoint/workgroup for {name}")
    # Alignment is required when SDL's byte-code pointer reaches vkCreateShaderModule.
    header = "/* Unmodified committed SPIR-V; no DXIL/MSL/window backend is linked. */\n"
    header += "_Alignas(4) static const unsigned char " + stem + "_spirv[] = {" + array.group(1) + "};\n"
    header += f"static const unsigned int {stem}_spirv_size = {len(binary)}u;\n"
    for backend in ("dxil", "msl"):
        header += f"#define {stem}_{backend} ((const unsigned char *)0)\n#define {stem}_{backend}_size 0u\n"
    header += f'#define {stem.upper()}_ENTRY "cs_main"\n'
    return header, {"bytes": len(binary), "sha256": hashlib.sha256(binary).hexdigest(),
                    "entrypoint": "cs_main", "local_size": local_size,
                    "bindings": sorted(bindings.values(), key=lambda item: (item.get("set", -1), item.get("binding", -1)))}


def prepare(source: Path, out: Path) -> dict[str, object]:
    source, out = source.resolve(), out.resolve()
    if out == source or source in out.parents:
        raise ValueError("renderer output must not be inside the pinned source tree")
    render = source / "port/host/render"
    if not (render / "gpu_raster3d.c").is_file():
        raise ValueError("--source must be the pinned khdays-port root")
    prepared: dict[str, str] = {}
    inputs: dict[str, str] = {}
    shaders: dict[str, object] = {}
    for name in RENDERERS:
        path = render / (name + ".c")
        prepared[name + ".c"] = adapt_renderer(name, path)
        inputs[path.relative_to(source).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
        path = render / (name + ".h")
        prepared[name + ".h"] = adapt_header(path.read_text(encoding="utf-8"))
        inputs[path.relative_to(source).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
    for name in SHADERS:
        path = render / "generated" / (name + "_cs.h")
        header, info = extract_spirv(name, path)
        prepared["generated/" + name + "_cs.h"] = header
        shaders[name] = info
        inputs[path.relative_to(source).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
    manifest: dict[str, object] = {"schema": 1, "source": source.as_posix(), "inputs": inputs,
                                   "shaders": shaders, "files": {}}
    for relative, text in prepared.items():
        path = out / relative
        data = text.encode("utf-8")
        path.parent.mkdir(parents=True, exist_ok=True)
        if not path.is_file() or path.read_bytes() != data:
            path.write_bytes(data)
        manifest["files"][relative] = hashlib.sha256(data).hexdigest()  # type: ignore[index]
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return manifest


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    arguments = parser.parse_args()
    manifest = prepare(arguments.source, arguments.out)
    print(f"Prepared {len(manifest['files'])} immutable-source GPU renderer files, "
          f"{sum(item['bytes'] for item in manifest['shaders'].values())} bytes of original SPIR-V")


if __name__ == "__main__":
    main()
