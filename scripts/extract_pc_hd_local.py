#!/usr/bin/env python3
"""Extract local KH 1.5+2.5 HD inputs without modifying or publishing the PC install.

This is intentionally a staging tool.  It uses the pinned khdays-port helper
from --port-root, writes only to --out, and never copies commercial data into
the repository.  The Epic install keeps its movie files under
EPIC/Mare/MOVIE/Days/en, while the upstream helper expects the Steam layout;
--pc-root handles that difference explicitly.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import shutil
import subprocess
import sys
from pathlib import Path


def load_module(path: Path, name: str):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def parse_inputs(path: Path) -> dict[str, list[str]]:
    result: dict[str, list[str]] = {}
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        archive, sep, entry = line.partition(" ")
        if sep and archive in {"Mare", "SettingMenu"}:
            result.setdefault(archive, []).append(entry)
    return result


def materialise(archive, paths: list[str], out: Path) -> tuple[int, int]:
    files = bytes_out = 0
    for path in paths:
        entry = archive.read(path)
        target = out / "original" / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(entry.data)
        files += 1
        bytes_out += len(entry.data)
        for name, data in entry.assets:
            asset = out / "remastered" / path / name
            asset.parent.mkdir(parents=True, exist_ok=True)
            asset.write_bytes(data)
            files += 1
            bytes_out += len(data)
    return files, bytes_out


def copy_hd_tools(port_root: Path, out: Path) -> Path:
    source = port_root / "tools" / "hd"
    target = out / "tool-runner" / "tools" / "hd"
    target.mkdir(parents=True, exist_ok=True)
    for path in source.glob("*.py"):
        shutil.copy2(path, target / path.name)
    # Mission/cine generators import the shared data decompressor from the
    # sibling tools directory.
    shared = port_root / "tools" / "decompress_data.py"
    if shared.is_file():
        shutil.copy2(shared, target.parent / shared.name)
    return target


def run_tool(tool: Path, args: list[str], cwd: Path) -> None:
    command = [sys.executable, str(tool), *args]
    print("RUN", " ".join(command), flush=True)
    subprocess.run(command, cwd=cwd, check=True)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--pc-root", type=Path, required=True)
    ap.add_argument("--port-root", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--languages", nargs="+", default=["en"])
    ap.add_argument("--game-data", type=Path, help="prepared extraction root, for mission/cine layouts")
    args = ap.parse_args()

    pc = args.pc_root.resolve()
    port = args.port_root.resolve()
    out = args.out.resolve()
    if not (pc / "Image" / "Mare.hed").is_file() or not (pc / "Image" / "Mare.pkg").is_file():
        raise SystemExit(f"Mare.hed/.pkg not found under {pc / 'Image'}")
    if out.exists() and any(out.iterdir()):
        raise SystemExit(f"output already exists and is not empty: {out}")
    out.mkdir(parents=True, exist_ok=True)

    installer = port / "installer"
    archive_mod = load_module(installer / "collection_archive.py", "khdays_collection_archive")
    inputs = parse_inputs(installer / "hd_inputs.txt")
    records = []

    for archive_name in ("Mare", "SettingMenu"):
        base = pc / "Image" / archive_name
        if not (base.with_name(base.name + ".hed").is_file() and base.with_name(base.name + ".pkg").is_file()):
            if archive_name == "SettingMenu":
                print("SKIP SettingMenu: archive not present", flush=True)
                continue
            raise SystemExit(f"missing archive: {base}.hed/.pkg")
        destination = out / "extracted" / f"{archive_name}.hed_out"
        destination.mkdir(parents=True, exist_ok=True)
        print(f"EXTRACT {archive_name}: {len(inputs.get(archive_name, []))} indexed inputs", flush=True)
        with archive_mod.Archive(base, None) as archive:
            files, size = materialise(archive, inputs.get(archive_name, []), destination)
        records.append({"archive": archive_name, "inputs": len(inputs.get(archive_name, [])), "files": files, "bytes": size})

    tools = copy_hd_tools(port, out)
    runner_root = out / "tool-runner"
    mare = out / "extracted" / "Mare.hed_out" / "original"
    settings = out / "extracted" / "SettingMenu.hed_out" / "original"

    # These two outputs need only Mare/SettingMenu; the remaining HD-pack
    # screens also require KH 2.8 and are deliberately left for a later pass.
    run_tool(tools / "theater_title.py", [str(mare), "--mod", "HD PACK", "--lang", *args.languages], runner_root)
    if settings.is_dir():
        run_tool(tools / "theater_settings.py", [str(settings), "--mod", "HD PACK", "--lang", *args.languages], runner_root)

    if args.game_data is not None:
        game_data = args.game_data.resolve()
        run_tool(
            tools / "theater_mission.py",
            [str(mare), "--mod", "HD PACK", "--lang", *args.languages, "--data", str(game_data)],
            runner_root,
        )
        run_tool(
            tools / "theater_cine.py",
            [str(mare), "--mod", "HD PACK", "--lang", *args.languages, "--data", str(game_data)],
            runner_root,
        )

    movies = pc / "EPIC" / "Mare" / "MOVIE" / "Days" / "en"
    if not movies.is_dir():
        raise SystemExit(f"Epic movie folder not found: {movies}")
    run_tool(
        tools / "theater_movies.py",
        [str(mare), "--movies", str(movies), "--videos", "--subtitles", "--lang", *args.languages],
        runner_root,
    )

    manifest = {
        "source": str(pc),
        "sourceKind": "local-user-install",
        "languages": args.languages,
        "archives": records,
        "movies": {
            "source": str(movies),
            "files": len(list(movies.glob("*.mp4"))),
            "sha256": hashlib.sha256("\n".join(sorted(p.name for p in movies.glob("*.mp4"))).encode()).hexdigest(),
        },
        "generated": [item for item in [
            "HD PACK title/settings layouts",
            "HD PACK mission/rooms/cine layouts" if args.game_data is not None else None,
            "cinematics videos/subtitles",
        ] if item is not None],
        "commercialDataPublished": False,
    }
    (out / "local-extraction-manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(json.dumps(manifest, indent=2), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
