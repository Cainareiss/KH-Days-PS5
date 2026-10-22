"""Check actual game records survive archive extraction, not just section GC.

The game's numeric references do not make ELF undefined-symbol dependencies.
A normal archive link silently drops data-only objects even with KEEP. Compare
every initializer's (DS address, size, module) and function-reference ID with
the original compiled archive. No copyrighted game data is emitted.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
import mmap
from pathlib import Path
import struct


def sections(data, start=0, size=None):
    size = len(data) - start if size is None else size
    if size < 64 or data[start:start + 6] != b"\x7fELF\x02\x01":
        raise ValueError("expected ELF64 little-endian")
    shoff = struct.unpack_from("<Q", data, start + 40)[0]
    entsize, count, names_index = struct.unpack_from("<HHH", data, start + 58)
    if entsize != 64 or not count or names_index >= count or shoff + count * 64 > size:
        raise ValueError("invalid ELF section table")
    headers = [struct.unpack_from("<IIQQQQIIQQ", data, start + shoff + i * 64)
               for i in range(count)]
    names_header = headers[names_index]
    names = data[start + names_header[4]:start + names_header[4] + names_header[5]]
    result = {}
    for header in headers:
        name = names[header[0]:].split(b"\0", 1)[0].decode()
        if header[1] != 8 and header[4] + header[5] > size:
            raise ValueError(f"out-of-bounds section {name}")
        result[name] = header
    return result


def inventory(data, start, headers):
    records = {}
    for name, width in (("khdays_data", 24), ("khdays_functions", 16)):
        entries = Counter()
        header = headers.get(name)
        if header:
            if header[5] % width or header[8] != 8:
                raise ValueError(f"incorrect record layout: {name}")
            for offset in range(start + header[4], start + header[4] + header[5], width):
                address = struct.unpack_from("<I", data, offset)[0]
                key = ((address,) + struct.unpack_from("<Ii", data, offset + 16)
                       if name == "khdays_data" else (address,))
                if address:
                    entries[key] += 1
        records[name] = entries
    return records


def archive_inventory(data):
    if data[:8] != b"!<arch>\n":
        raise ValueError("expected regular ar archive")
    result = {name: Counter() for name in ("khdays_data", "khdays_functions")}
    offset = 8
    while offset < len(data):
        if offset + 60 > len(data) or data[offset + 58:offset + 60] != b"`\n":
            raise ValueError("invalid archive member")
        size = int(data[offset + 48:offset + 58])
        start = offset + 60
        if start + size > len(data):
            raise ValueError("truncated archive")
        if data[start:start + 4] == b"\x7fELF":
            entries = inventory(data, start, sections(data, start, size))
            for name, values in entries.items():
                result[name].update(values)
        offset = start + size + (size & 1)
    return result


def file_bytes_at(data, headers, address, size):
    for header in headers.values():
        if header[1] != 8 and header[3] <= address and address + size <= header[3] + header[5]:
            offset = header[4] + address - header[3]
            return data[offset:offset + size]
    raise ValueError("initializer payload is not file-backed")


def check_bg_table(data, headers):
    header = headers["khdays_data"]
    for offset in range(header[4], header[4] + header[5], 24):
        address, pointer, size, module = struct.unpack_from("<I4xQIi", data, offset)
        if address == 0x02041ac0 and module == -1:
            if size != 32:
                raise ValueError("wrong BG register table length")
            # A PIE's absolute pointer is relocated by the loader. lld may
            # leave zero in the file and keep the address only in RELA.
            location = header[3] + offset - header[4] + 8
            rela = headers.get(".rela.dyn")
            if rela:
                for roffset in range(rela[4], rela[4] + rela[5], 24):
                    target, info, addend = struct.unpack_from("<QQq", data, roffset)
                    if target == location:
                        if info != 8:  # R_X86_64_RELATIVE, no symbol
                            raise ValueError("unexpected initializer pointer relocation")
                        pointer = addend
                        break
            values = struct.unpack("<8I", file_bytes_at(data, headers, pointer, size))
            expected = tuple(0x0b100000 + n for n in (8, 10, 12, 14, 0x1008, 0x100a, 0x100c, 0x100e))
            if values != expected:
                raise ValueError("BG table still points to raw DS hardware")
            return "all eight BG register references route to the modeled I/O view"
    raise ValueError("missing BG register table initializer (0x02041ac0)")


def validate(archive: Path, elf: Path, check_game_bg_table=False):
    with archive.open("rb") as source, mmap.mmap(source.fileno(), 0, access=mmap.ACCESS_READ) as data:
        expected = archive_inventory(data)
    with elf.open("rb") as source, mmap.mmap(source.fileno(), 0, access=mmap.ACCESS_READ) as data:
        headers = sections(data)
        actual = inventory(data, 0, headers)
        result = {"passed": True, "archive": str(archive), "elf": str(elf),
                  "elf_sha256": hashlib.sha256(data).hexdigest(), "tables": {}}
        for name in expected:
            missing, extra = expected[name] - actual[name], actual[name] - expected[name]
            result["tables"][name] = {
                "expected": expected[name].total(), "linked": actual[name].total(),
                "missing": missing.total(), "extra": extra.total()}
            result["passed"] &= not missing and not extra and bool(expected[name])
        if check_game_bg_table:
            try:
                result["bg_register_table"] = check_bg_table(data, headers)
            except ValueError as error:
                result["bg_register_table"] = str(error)
                result["passed"] = False
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, required=True)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--check-game-bg-table", action="store_true")
    args = parser.parse_args()
    report = validate(args.archive, args.elf, args.check_game_bg_table)
    args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    raise SystemExit(0 if report["passed"] else 1)
