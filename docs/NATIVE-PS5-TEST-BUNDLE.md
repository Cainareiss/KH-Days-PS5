# Native PS5 data/memory boot correction (2026-10-21)

The user reports that the syscallfix image launches and immediately closes.
The reused data staging contained the old prototype extraction: the production
cartridge reader rejects it because it lacks `system/header.bin` and the native
`nitrofs` layout. That tree was replaced with the preserved European extraction.
The large memory window/heaps were also moved from anonymous/flexible memory
to the native SDK's virtual reservations and direct-memory backing. The exact
console fault/return is still unobserved; hardware boot needs a retest.

## Latest files

- FSELF: `build-native-game-ps5/eboot.bin`
- Converted ELF: `build-native-game-ps5/khdays-days-runtime.ps5.elf`
- exFAT image: `C:\Users\<user>\Desktop\PPSA35820-native-bootfix.exfat` (719 MiB)
- Title ID: `PPSA35820`
- Content version: `01.000.003`

The image was mounted read-only and checked. `sce_sys/param.json` reports
`PPSA35820`; it contains the expected FSELF, icon/background, ROM, matching
`libc.prx`, and 1,906 extracted files (1,898 NitroFS files plus system/metadata).
All cartridge files were checked against their manifest hashes inside the
mounted image, not just counted. The volume uses exFAT, 512-byte
sectors, and 64 KiB clusters.

## Changes and checks

- The production C reader rejected the previous staged tree, and accepted the
  correct extraction: 261,842,549 bytes compared with the original cartridge,
  zero failures. The source, refreshed stage, and mounted image all passed the
  new native-data validator (header, revision, system sizes, FAT records, hashes).
- Memory routing uses `ps5_vrange_reserve/commit/decommit` and `ps5_shm_map`;
  mirrors share one direct-memory object. A host test exercises mirror mapping,
  stacks, reset and failure propagation with mocked device APIs. This is not
  a hardware memory-allocation test.
- Boot stages are flushed into `/data/khdays-native/saves/boot.log` before
  memory mapping, cartridge validation, host startup, and entry into NitroMain.

- Removed the payload static `libc`, `pthread`, `dl`, and `ufs` archives from
  the final link and removed inert optional import shims from the link inputs.
- Bound the native platform replacements and heap/thread wrappers used by
  the working Melon Mix title link. `mmap`/`mprotect` are now module imports.
- Decoded 3,126,640 instructions in the raw ELF; no `syscall`/`sysenter`/`int 0x80`
  instruction remains. Three regression tests exercise detection, data-pattern
  exclusion, and rejection of empty disassembly. Packaging now requires the
  matching FSELF hash and syscall audit before staging or formatting.

- Stderr-to-klog capture now begins before the first memory-map operation,
  rather than only when video startup begins.
- The fatal-signal reporter now includes `SIGSYS`, to capture an illegal
  syscall's signal, PC, and fault address when the platform reports it that way.
- FSELF conversion uses the same module/companion SDK metadata and SELF magic
  as the known-good Melon Mix build. Inspection reports `integrity: valid`.
- The shared `libc.prx` hash matches Melon Mix. These checks narrow packaging
  differences but cannot identify or rule out a runtime syscall failure.

- FSELF SHA-256: `6B0C3249C0CD4351138D448FCC63D9ED8D5DDFABAABC3CCE2E6CF0683FE5E4C7`
- Converted ELF SHA-256: `F517727B57E55612A08A5D0E1D22638B436485669D751AD8AC8A4D88AAC8F44E`
- exFAT SHA-256: `7BAD934D5A61F985FC24C9350AE10C8F4E05D3334EBB047EDBE3BD4F4A5A8F9B`

## Retest

Replace the older image on the test device with this file. If it still closes,
retrieve `/data/khdays-native/saves/boot.log` via the console's existing FTP/file
access. If no checkpoint file was created, capture fresh klog/debug output.
The old Desktop `debug.log` belongs to Undertale. The precise failing console
stage is still unknown until that evidence is observed.
