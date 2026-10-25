"""Bind a signed native title to the exact sources and link inputs it used.

Source content is always hashed, so copying an older timestamp cannot hide a
source edit. Large binary dependencies use size/mtime to avoid re-reading the
complete game and RADV archives every time an image is packaged.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sys


SCHEMA_VERSION = 1
HASH_BINARY_LIMIT = 16 * 1024 * 1024
SOURCE_SUFFIXES = {
    ".c", ".cc", ".cpp", ".cxx", ".h", ".hpp", ".hxx", ".s", ".inc",
    ".cmake", ".ld", ".glsl", ".comp", ".spv", ".txt",
}
PREPARATION_SCRIPTS = (
    "scripts/prepare_native_runtime_sources.py",
    "scripts/prepare_native_vulkan_sources.py",
    "scripts/prepare_native_elf_sources.py",
    "scripts/link_native_ps5_elf.ps1",
    "scripts/native_link_freshness.py",
)


def digest(path: Path) -> str:
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(chunk)
    return result.hexdigest()


def cache_values(build: Path) -> dict[str, str]:
    values = {}
    for line in (build / "CMakeCache.txt").read_text(encoding="utf-8").splitlines():
        if not line or line.startswith(("#", "//")) or "=" not in line:
            continue
        name, value = line.split("=", 1)
        values[name.split(":", 1)[0]] = value
    return values


def source_roots(project: Path, build: Path) -> list[Path]:
    cache = cache_values(build)
    roots = [project / "native", build / "runtime-sources", build / "vulkan-sources"]
    upstream = cache.get("KHDAYS_NATIVE_UPSTREAM_ROOT")
    if upstream:
        roots.extend(Path(upstream) / child for child in (
            "port", "assets/src", "assets/include", "assets/fetch_ufbx.cmake",
            "external/khdays-decomp/libs/mobiclip/video/portable"))
    if cache.get("KHDAYS_NATIVE_ELF_SOURCES"):
        roots.append(Path(cache["KHDAYS_NATIVE_ELF_SOURCES"]))
    if cache.get("KHDAYS_NATIVE_GENERATED_ROOT"):
        generated = Path(cache["KHDAYS_NATIVE_GENERATED_ROOT"])
        # The raw generator tree also contains backups and a complete second
        # copy of the game. The linker uses the adapted ELF tree above; only
        # these top-level headers/function registry come from the raw output.
        roots.extend(generated.glob("*.h"))
        roots.append(generated / "ds_functions.c")
    template = cache.get("KHDAYS_PS5_TEMPLATE_ROOT")
    if template:
        roots.extend(Path(template) / child for child in ("ps5/src", "ps5/third_party/volk"))
    for relative in (
            "toolchains/sources/PS5_PayloadSDK/platform/src/klog.c",
            "toolchains/sources/PS5_Vulkan/radv/emutls_runtime.c"):
        source = project / relative
        if source.exists():
            roots.append(source)
    roots.extend(project / name for name in PREPARATION_SCRIPTS)
    roots.append(build / "CMakeCache.txt")
    if cache.get("CMAKE_TOOLCHAIN_FILE"):
        roots.append(Path(cache["CMAKE_TOOLCHAIN_FILE"]))
    return sorted(set(path.resolve() for path in roots))


def source_inventory(roots: list[Path]) -> dict[str, str]:
    result = {}
    for root in roots:
        if not root.exists():
            raise ValueError(f"Native source root is missing: {root}")
        paths = root.rglob("*") if root.is_dir() else [root]
        for path in paths:
            if (not root.is_dir() or path.suffix.lower() in SOURCE_SUFFIXES) and path.is_file():
                # roots are already absolute. Resolving every generated unit
                # again adds tens of thousands of slow Win32 handle queries.
                result[str(path)] = digest(path)
    return dict(sorted(result.items()))


def binary_record(path: Path, *, hash_large: bool = False) -> dict:
    info = path.stat()
    result = {"size": info.st_size, "mtime_ns": info.st_mtime_ns}
    if hash_large or info.st_size <= HASH_BINARY_LIMIT:
        result["sha256"] = digest(path)
    return result


def snapshot(project: Path, build: Path, inputs: list[Path], sources: list[Path]) -> dict:
    roots = sorted(set(source_roots(project, build) + [path.resolve() for path in sources]))
    return {
        "schema_version": SCHEMA_VERSION,
        "project": str(project.resolve()),
        "build": str(build.resolve()),
        "source_roots": [str(root) for root in roots],
        "source_sha256": source_inventory(roots),
        "link_inputs": {str(path.resolve()): binary_record(path, hash_large=True) for path in inputs},
    }


def validate_inputs(proof: dict, project: Path) -> None:
    if proof.get("schema_version") != SCHEMA_VERSION:
        raise ValueError("Missing/unsupported native link-input proof; relink before packaging")
    if Path(proof["project"]).resolve() != project.resolve():
        raise ValueError("Native link-input proof belongs to a different project")
    roots = [Path(root) for root in proof["source_roots"]]
    if not set(source_roots(project, Path(proof["build"]))).issubset(set(roots)):
        raise ValueError("Native source inventory changed after linking; rebuild/relink before packaging")
    expected = proof["source_sha256"]
    if not expected or not proof["link_inputs"]:
        raise ValueError("Native link-input proof contains no sources or archives")
    actual = source_inventory(roots)
    changed = sorted(path for path in set(expected) | set(actual)
                     if expected.get(path) != actual.get(path))
    if changed:
        raise ValueError("Native sources changed after linking; rebuild/relink before packaging: "
                         + ", ".join(changed[:8]))
    for name, record in proof["link_inputs"].items():
        path = Path(name)
        if not path.is_file():
            raise ValueError(f"Native link input changed after linking; relink before packaging: {name}")
        current = binary_record(path)
        # Preserve the original hash in the proof, but only re-hash small
        # archives. A newly built large game/RADV archive changes these stats.
        expected_record = {key: record.get(key) for key in current}
        if current != expected_record:
            raise ValueError(f"Native link input changed after linking; relink before packaging: {name}")


def seal(captured: dict, project: Path, elf: Path, fself: Path) -> dict:
    # A source/archive edit while ld or the signer ran must invalidate the link.
    validate_inputs(captured, project)
    return {**captured, "passed": True, "elf": str(elf.resolve()),
            "elf_sha256": digest(elf), "fself": str(fself.resolve()),
            "fself_sha256": digest(fself)}


def verify(proof: dict, project: Path, elf_sha256: str, fself_sha256: str) -> None:
    if not proof.get("passed"):
        raise ValueError("Native link-input proof was not sealed by a successful link")
    if (proof.get("elf_sha256", "").lower() != elf_sha256.lower()
            or proof.get("fself_sha256", "").lower() != fself_sha256.lower()):
        raise ValueError("Native link-input proof does not match the requested ELF/eboot")
    validate_inputs(proof, project)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    capture = commands.add_parser("snapshot")
    capture.add_argument("--project", type=Path, required=True)
    capture.add_argument("--build", type=Path, required=True)
    capture.add_argument("--input", type=Path, action="append", required=True)
    capture.add_argument("--source", type=Path, action="append", default=[])
    capture.add_argument("--output", type=Path, required=True)
    finish = commands.add_parser("seal")
    finish.add_argument("--snapshot", type=Path, required=True)
    finish.add_argument("--project", type=Path, required=True)
    finish.add_argument("--elf", type=Path, required=True)
    finish.add_argument("--fself", type=Path, required=True)
    finish.add_argument("--output", type=Path, required=True)
    check = commands.add_parser("verify")
    check.add_argument("--proof", type=Path, required=True)
    check.add_argument("--project", type=Path, required=True)
    check.add_argument("--elf-sha256", required=True)
    check.add_argument("--fself-sha256", required=True)
    args = parser.parse_args()
    try:
        if args.command == "snapshot":
            report = snapshot(args.project, args.build, args.input, args.source)
        elif args.command == "seal":
            captured = json.loads(args.snapshot.read_text(encoding="utf-8-sig"))
            report = seal(captured, args.project, args.elf, args.fself)
        else:
            proof = json.loads(args.proof.read_text(encoding="utf-8-sig"))
            verify(proof, args.project, args.elf_sha256, args.fself_sha256)
            print(f"Native executable is current: {len(proof['source_sha256'])} source files checked")
            return 0
        args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(f"Native link-input {args.command}: {len(report['source_sha256'])} source files")
        return 0
    except (OSError, KeyError, ValueError) as error:
        print(f"Native executable freshness failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
