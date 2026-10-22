# Native PS5 boot and cartridge

`native/ps5/start_ps5.c` exposes `khdays_ps5_boot(argc, argv)`. It reserves
the DS reference window before the host starts, initializes the real upstream
runtime and enters `khdays_NitroMain` on a DS-addressable execution context.
ARM9 loading, backward decompression, ITCM/DTCM autoloads, BSS clearing,
native data initializers, sound-driver startup and reset follow upstream
`port/runtime/core/start.c`. An invalid load/autoload range stops the boot.
This path does not call the previous prototype or smoke test.

Configure paths before boot using `khdays_ps5_set_paths(data, saves, mods)`
from `paths_ps5.h`. NULL keeps a default:

| Resource | Default |
| --- | --- |
| Complete extracted cartridge, or parent containing hash folders | `/app0/data/extracted` |
| Writable user/save folder | `/data/khdays-native/saves` |
| Mod folders, selected alphabetically | `/app0/mods` |

The host's user-folder service must return `khdays_ps5_save_root()`. No
global working-directory change or write inside `/app0` is required.

`rom_ps5.c` implements the existing `rom.h` C interface. It validates the
European YKGP revision 0 header, system lengths, FNT tree, FAT bounds and
every original NitroFS file length. System bytes and the FAT are resident;
game files are streamed through a 64-file cache per independent reader.
Absent extraction ranges fail explicitly. Padding reads as FF. Mods can
replace raw files; differently sized replacements are placed after the used
ROM end with the in-memory FAT updated. P2 entries and LZ10/LZ11 use bounded
decoders with overlapping-copy support. PC-specific `text_patches.cpp`
wording is not applied by this C implementation.

Validation performed on 2026-10-20:

- PS5 Clang target `x86_64-sie-ps5`, C17, `-Wall -Wextra -Werror`: boot,
  paths and cartridge objects compile without diagnostics.
- `tests/native_rom_ps5_test.c` exercised the production reader against
  the original European dump and its existing extraction: **261842549
  bytes compared, 1898 files, zero failures**. Header, ARM9, ARM7, overlay
  table, FNT and FAT were also compared byte for byte.
- P2 message entry, LZ10/LZ11 literals/references, overlapping copies,
  extended sizes and malformed streams are covered. The Windows harness
  uses the direct extraction directory; directory scanning is compiled
  for the PS5 target but is not claimed as hardware-tested.

These checks validate boot/card components. They do not establish that the
complete executable links or that gameplay has run on PS5 hardware.
