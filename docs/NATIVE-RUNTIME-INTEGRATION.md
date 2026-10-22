# Native PS5 runtime integration

Verified on 2026-10-20 with the pinned `khdays-port` v0.1.1 source tree.

## Artifacts

- Runtime archive: `build-native-game-ps5/libkhdays-native-ps5-runtime.a`
- Relocatable integration: `build-native-game-ps5/runtime-validation/khdays-native-runtime.o`
- Validation report: `build-native-game-ps5/runtime-validation/validation.json`
- Game archive: `build-native-game-ps5/libkhdays-native-ps5-game.a`

The runtime archive contains the generated game services plus the PS5 adapters
for boot/ROM access, VideoOut, input, sceAudioOut, file I/O, memory, MODS
movie handling, subtitle atlas drawing, and the portable C++ asset/runtime
helpers. The build also includes the pinned RADV emulated-TLS object and the
one-screen composition table.

## Audit result

The relocatable link passed with zero unresolved project symbols. The remaining
undefined symbols are expected final-link dependencies from libc/libc++, the
PS5 SDK imports, and linker section boundaries (`__start_*`/`__stop_*`).

Runtime archive SHA-256:

`0154BD2632A37D0E6B4B8FFABD57A74304B3CA093DBADCC27609C3743638235F`

Relocatable audit SHA-256:

`C4DA46C3480C33B89AE49489D53F28FA44E53CD843C162F184326ECA548FBD9C`

## Remaining boundary

This is the runtime integration milestone, not a finished PS5 title. A final
Prospero executable still needs the SDK startup/linker composition, import
inspection, FSELF conversion, and a real console boot test. No `eboot.bin` or
exFAT package is claimed by this milestone.
