"""Reject decoded syscall instructions in a native title's executable code.

Payload SDK archives can link successfully while injecting direct syscalls in
eboot. Inspect decoded instructions (not byte patterns in game data) before
converting and signing the title.
"""
import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path


def audit(elf: Path, objdump: Path) -> dict:
    process = subprocess.Popen(
        [str(objdump), "-d", "--no-show-raw-insn", str(elf)],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        encoding="utf-8", errors="replace",
    )
    current_symbol = "?"
    forbidden = []
    instruction_count = 0
    symbol = re.compile(r"^[0-9a-fA-F]+ <(.+)>:$")
    instruction = re.compile(r"^\s*([0-9a-fA-F]+):\s+(syscall|sysenter|int\s+\$0x80)\s*(?:#.*)?$")
    for line in process.stdout:
        if re.match(r"^\s*[0-9a-fA-F]+:\s+[a-zA-Z]", line):
            instruction_count += 1
        match = symbol.match(line.strip())
        if match:
            current_symbol = match.group(1)
        match = instruction.match(line)
        if match:
            forbidden.append({"symbol": current_symbol, "address": "0x" + match.group(1),
                              "instruction": match.group(2)})
    process.stdout.close()
    stderr = process.stderr.read()
    process.stderr.close()
    code = process.wait()
    if code:
        raise RuntimeError(f"objdump failed ({code}): {stderr}")
    if not instruction_count:
        raise RuntimeError('No executable instructions decoded; audit the raw ELF with sections')
    with elf.open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha256').hexdigest()
    return {"elf": str(elf.resolve()), "sha256": digest,
            "decoded_instruction_count": instruction_count,
            "direct_syscall_count": len(forbidden),
            "direct_syscalls": forbidden,
            "passed": not forbidden}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--objdump", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    report = audit(args.elf, args.objdump)
    args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report))
    raise SystemExit(0 if report["passed"] else 1)
