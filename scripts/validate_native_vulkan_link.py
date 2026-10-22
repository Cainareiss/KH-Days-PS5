"""Reject Mesa callbacks silently omitted by lazy static-archive extraction.

Mesa's generated dispatch tables use weak references. An otherwise valid ELF
can therefore contain NULL callbacks even when RADV's archive provides their
implementation. Build 015 hit this in wsi_device_init/GetPhysicalDeviceProperties2.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import mmap
from pathlib import Path
import struct

from validate_native_link_records import sections


def symbols(data, start, headers):
    table = headers.get(".symtab")
    if table is None or table[9] != 24:
        raise ValueError("missing ELF64 symbol table")
    # Use the raw sh_link index: relocatable objects can have repeated .group
    # section names, which cannot be indexed through the section-name dict.
    section_offset = struct.unpack_from("<Q", data, start + 40)[0]
    strings = struct.unpack_from("<IIQQQQIIQQ", data, start + section_offset + table[6] * 64)
    names = data[start + strings[4]:start + strings[4] + strings[5]]
    result = {}
    for at in range(start + table[4], start + table[4] + table[5], 24):
        name, info, other, index, value, size = struct.unpack_from("<IBBHQQ", data, at)
        if not name:
            continue
        end = names.find(b"\0", name)
        if end < 0:
            raise ValueError("unterminated ELF symbol name")
        text = names[name:end].decode()
        result[text] = {"defined": index != 0, "type": info & 15, "value": value, "size": size}
    return result


def available_callbacks(archive):
    if archive[:8] != b"!<arch>\n":
        raise ValueError("expected a regular RADV archive")
    result = set()
    offset = 8
    while offset < len(archive):
        if offset + 60 > len(archive) or archive[offset + 58:offset + 60] != b"`\n":
            raise ValueError("invalid RADV archive member")
        size = int(archive[offset + 48:offset + 58])
        start = offset + 60
        if start + size > len(archive):
            raise ValueError("truncated RADV archive")
        if archive[start:start + 4] == b"\x7fELF":
            for name, symbol in symbols(archive, start, sections(archive, start, size)).items():
                if symbol["defined"] and symbol["type"] == 2 and name.startswith(("radv_", "vk_", "wsi_")):
                    result.add(name)
        offset = start + size + (size & 1)
    return result


def audit(elf: Path, archive: Path):
    with archive.open("rb") as stream, mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as data:
        available = available_callbacks(data)
    with elf.open("rb") as stream, mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as data:
        headers = sections(data)
        linked = symbols(data, 0, headers)
        missing = sorted(name for name, symbol in linked.items()
                         if not symbol["defined"] and name in available)
        # Check the precise callback that crashed, not just symbol presence.
        # Its pointer must actually be retained in Mesa's common dispatch table.
        function = linked.get("vk_common_GetPhysicalDeviceProperties2", {})
        table = linked.get("vk_common_physical_device_entrypoints", {})
        callback_relocation = False
        if function.get("defined") and table.get("defined"):
            rela = headers.get(".rela.dyn")
            if rela:
                for at in range(rela[4], rela[4] + rela[5], 24):
                    target, info, addend = struct.unpack_from("<QQq", data, at)
                    if (info == 8 and table["value"] <= target < table["value"] + table["size"]
                            and addend == function["value"]):
                        callback_relocation = True
                        break
        digest = hashlib.sha256(data).hexdigest()
    return {"passed": not missing and callback_relocation, "elf": str(elf.resolve()),
            "elf_sha256": digest, "radv_archive": str(archive.resolve()),
            "available_driver_functions": len(available), "omitted_functions": missing,
            "properties2_common_dispatch_relocated": callback_relocation}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--archive", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    result = audit(args.elf, args.archive)
    args.report.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"passed": result["passed"], "omitted_count": len(result["omitted_functions"]),
                      "properties2_common_dispatch_relocated": result["properties2_common_dispatch_relocated"],
                      "report": str(args.report)}))
    raise SystemExit(0 if result["passed"] else 1)
