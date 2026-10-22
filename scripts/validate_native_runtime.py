"""Link all native archives relocatably and audit the resulting host contract.

This is a link-time check, not an executable or a console test. Undefined
symbols are recorded, never ignored or replaced with success stubs.
"""
import argparse
import hashlib
import json
import re
import subprocess
import struct
from pathlib import Path


def run(command):
    result = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", errors="replace")
    if result.returncode:
        raise RuntimeError(" ".join(map(str, command)) + "\n" + result.stdout + result.stderr)
    return result.stdout


def validate(build, llvm, output):
    names = ["game", "runtime", "context", "host-contract", "game-tables", "gpu", "gfx", "sound"]
    archives = [build / f"libkhdays-native-ps5-{name}.a" for name in names]
    for archive in archives:
        if not archive.is_file():
            raise FileNotFoundError(archive)
    output.mkdir(parents=True, exist_ok=True)
    linked = output / "khdays-native-runtime.o"
    # Resolve inter-archive references and reject multiple strong definitions.
    run([str(llvm / "ld.lld.exe"), "-r", "--whole-archive",
         *map(str, archives), "--no-whole-archive", "-o", str(linked)])
    with linked.open("rb") as stream:
        header = stream.read(20)
    if header[:6] != b"\x7fELF\x02\x01" or struct.unpack_from("<HH", header, 16) != (1, 62):
        raise RuntimeError("Runtime bundle must be x86-64 ELF64 relocatable")
    symbols = run([str(llvm / "llvm-nm.exe"), str(linked)])
    defined, unresolved = set(), set()
    for line in symbols.splitlines():
        match = re.match(r"^\s*(?:[0-9a-fA-F]+\s+)?([A-Za-z?])\s+(\S+)$", line)
        if match:
            (unresolved if match[1] == "U" else defined).add(match[2])
    required = [
        "khdays_ps5_boot", "khdays_rom_init", "khdays_runtime_init",
        "khdays_NitroMain", "khdays_memory_map", "khdays_reference",
        "khdays_context_switch", "khdays_host_start", "khdays_host_present",
        "khdays_host_input", "khdays_host_audio", "khdays_host_audio_float",
        "khdays_host_sound_start", "khdays_host_read_file",
        "khdays_host_write_file_atomic", "khdays_clock_cycles",
    ]
    missing = [name for name in required if name not in defined]
    # Boundaries are intentionally provided by game_tables.ld at the final
    # executable link. Everything else belonging to the project must close.
    internal_missing = sorted(name for name in unresolved
                              if name.startswith(("khdays_", "NNS_", "ps5_klog_")))
    with linked.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    report = {
        "stage": "runtime_relocatable_link_passed" if not missing and not internal_missing
                 else "runtime_contract_missing",
        "relocatable": str(linked.resolve()),
        "bytes": linked.stat().st_size,
        "sha256": digest,
        "archives": [str(p.resolve()) for p in archives],
        "required_contracts": {name: name in defined for name in required},
        "unresolved_count": len(unresolved),
        "unresolved_project_symbols": internal_missing,
        "external_dependencies": "SDK libc/libc++/libc++abi/unwind, PS5 imports and final linker boundaries",
        "unresolved": sorted(unresolved),
        "console_test": "not run",
        "final_executable": False,
    }
    (output / "validation.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({k: report[k] for k in ("stage", "bytes", "unresolved_count", "final_executable")}))
    if missing or internal_missing:
        raise RuntimeError("Missing native contracts: " + ", ".join(missing + internal_missing))
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", required=True, type=Path)
    parser.add_argument("--llvm", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    validate(args.build, args.llvm, args.out)
