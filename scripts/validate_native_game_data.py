"""Validate the exact extracted-cartridge layout consumed by rom_ps5.c."""
import argparse
import hashlib
import json
import struct
from pathlib import Path, PurePosixPath


def choose_root(root):
    root = root.resolve()
    if (root / 'system/header.bin').is_file():
        return root
    children = [p for p in root.iterdir() if p.is_dir() and (p / 'system/header.bin').is_file()]
    if len(children) != 1:
        raise ValueError('Expected one native extraction with system/header.bin and nitrofs; '
                         f'found {len(children)} under {root}')
    return children[0]


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def validate(root):
    root = choose_root(root)
    manifest_path = root / 'manifest.json'
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    header = (root / 'system/header.bin').read_bytes()
    if len(header) != 512 or header[12:16] != b'YKGP' or header[0x1e] != 0:
        raise ValueError('Native runtime requires a 512-byte YKGP revision 0 header')
    word = lambda offset: struct.unpack_from('<I', header, offset)[0]
    if word(0x28) != 0x02000000:
        raise ValueError('Unexpected ARM9 RAM address')
    required = {'header.bin': (0, 512), 'arm9.bin': (word(0x20), word(0x2c)),
                'arm7.bin': (word(0x30), word(0x3c)),
                'arm9_overlay_table.bin': (word(0x50), word(0x54)),
                'fnt.bin': (word(0x40), word(0x44)), 'fat.bin': (word(0x48), word(0x4c))}
    checked_bytes = 0

    def check_file(record, expected_path):
        nonlocal checked_bytes
        relative = PurePosixPath(record['path'])
        if relative.is_absolute() or '..' in relative.parts or str(relative) != expected_path:
            raise ValueError(f'Unexpected manifest path: {relative}')
        path = root.joinpath(*relative.parts).resolve()
        if not path.is_relative_to(root):
            raise ValueError(f'File outside extraction: {path}')
        if not path.is_file() or path.stat().st_size != record['size']:
            raise ValueError(f'Missing file or wrong size: {relative}')
        if digest(path) != record['sha256']:
            raise ValueError(f'Checksum mismatch: {relative}')
        checked_bytes += record['size']

    for name, (offset, size) in required.items():
        record = manifest['system'][name]
        if (record['offset'], record['size']) != (offset, size):
            raise ValueError(f'System metadata does not match cartridge header: {name}')
        check_file(record, 'system/' + name)
    fat = (root / 'system/fat.bin').read_bytes()
    records = manifest['files']
    count = word(0x4c) // 8
    if word(0x4c) % 8 or len(records) != count or count != 1898:
        raise ValueError('Expected the full European cartridge FAT (1898 files)')
    for file_id, record in enumerate(records):
        start, end = struct.unpack_from('<II', fat, file_id * 8)
        if (record['file_id'], record['start'], record['end'], record['size']) != (
                file_id, start, end, end - start):
            raise ValueError(f'FAT/manifest mismatch: file {file_id}')
        if not record['path'].startswith('nitrofs/'):
            raise ValueError('Native NitroFS files must live under nitrofs/')
        check_file(record, record['path'])
    return {'passed': True, 'root': str(root), 'gameCode': 'YKGP', 'revision': 0,
            'systemFiles': len(required), 'nitrofsFiles': count, 'validatedBytes': checked_bytes,
            'manifestSha256': digest(manifest_path),
            'headerSha256': hashlib.sha256(header).hexdigest(),
            'extractedFiles': sum(1 for p in root.rglob('*') if p.is_file())}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', required=True, type=Path)
    parser.add_argument('--report', required=True, type=Path)
    args = parser.parse_args()
    report = validate(args.root)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report))
