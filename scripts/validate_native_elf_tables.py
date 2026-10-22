"""Cross-compile and inspect the actual ELF table layout with section GC enabled."""
import json
from pathlib import Path
import struct
import subprocess


def validate():
    project = Path(__file__).resolve().parents[1]
    llvm = project / "toolchains/llvm-23.1.3/clang+llvm-23.1.3-x86_64-pc-windows-msvc/bin"
    sdk = project / "toolchains/sources/PS5_Vulkan/.deps/native/ps5-payload-sdk/target"
    work = project / "build-native-elf-tables-test"
    work.mkdir(exist_ok=True)
    obj, elf = work / "records.o", work / "records.elf"
    subprocess.run([str(llvm / "clang.exe"), "--target=x86_64-sie-ps5",
                    f"--sysroot={sdk}", "-isystem", str(sdk / "include"),
                    "-I", str(project / "native/ps5"), "-std=c17", "-Wall", "-Wextra", "-Werror",
                    "-fpack-struct=4", "-fPIC", "-ffunction-sections", "-fdata-sections",
                    "-c", str(project / "tests/native_elf_table_fixture.c"), "-o", str(obj)], check=True)
    subprocess.run([str(llvm / "ld.lld.exe"), "-shared", "--gc-sections",
                    "--undefined=fixture_keep_data", "-z", "max-page-size=0x4000",
                    "-T", str(project / "native/ps5/game_tables.ld"), str(obj), "-o", str(elf)], check=True)
    data = elf.read_bytes()
    if data[:6] != b"\x7fELF\x02\x01":
        raise ValueError("Expected ELF64 little-endian")
    shoff = struct.unpack_from("<Q", data, 40)[0]
    shsize, shcount, shstrings = struct.unpack_from("<HHH", data, 58)
    headers = [struct.unpack_from("<IIQQQQIIQQ", data, shoff + i * shsize) for i in range(shcount)]
    names_header = headers[shstrings]
    names = data[names_header[4]:names_header[4] + names_header[5]]
    sections = {}
    for h in headers:
        name = names[h[0]:].split(b"\0", 1)[0].decode()
        sections[name] = {"size": h[5], "address": h[3], "alignment": h[8]}
    for name, size in (("khdays_data", 24), ("khdays_functions", 16)):
        section = sections.get(name)
        if section is None or section["size"] != size or section["address"] % 8:
            raise ValueError(f"Invalid record table after --gc-sections: {name}: {section}")
    result = {"target": "x86_64-sie-ps5", "elf": str(elf), "section_gc": True,
              "tables": {k: sections[k] for k in ("khdays_data", "khdays_functions")},
              "result": "pass", "scope": "ELF layout; no hardware execution"}
    (work / "validation.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    validate()
