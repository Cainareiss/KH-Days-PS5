# Build 01.000.016: missing Mesa dispatch implementations

## Console evidence and cause

The build 015 log supplied as `<pasta-local-do-usuario>\Downloads\boot.log` stops
while enumerating physical devices and reports a NULL call with
`return_ELF+0x0000000000d13a14`. Symbolizing this offset against the exact
015 ELF identifies `wsi_device_init`, `wsi_common.c:114`. The instruction
immediately before the return address calls the local
`GetPhysicalDeviceProperties2` function pointer.

The pinned Mesa source obtains that function through `radv_wsi_proc_addr` /
`vk_instance_get_proc_addr_unchecked`. It is an internal driver dispatch path,
not one of the application function pointers checked in build 015. Its fallback
implementation, `vk_common_GetPhysicalDeviceProperties2`, was undefined in the
015 ELF despite being supplied by `libvulkan_radeon.ps5.a`.

Mesa's generated dispatch tables use weak references. These do not extract
otherwise unreferenced objects from a static archive. Our title linker applied
`--whole-archive` to the game archive but not the RADV archive, omitting 38
implementations the driver provided. The pinned upstream recipe already
documents this exact WSI failure and requires RADV to be linked whole:
`toolchains/sources/PS5_Vulkan/tools/radv-link.sh:35`.

The original log, ELF, link report and failed callback audit are retained in
`build-native-vulkan-ps5/evidence/015-*`. In particular,
`015-vulkan-link-failure.json` records the original ELF hash, all 38 omitted
functions and the missing properties2 dispatch relocation.

## Correction

`scripts/link_native_ps5_elf.ps1` now includes the complete RADV archive using
`--whole-archive ... --no-whole-archive`, matching the upstream recipe. SDK
libraries retain their previous extraction behavior. No Mesa source or ROM
content was changed.

The new `validate_native_vulkan_link.py` gate checks both:

- whether any undefined Mesa callback in the ELF has an implementation in the
  RADV archive that the linker omitted;
- whether the common physical-device dispatch table contains a relocated
  pointer to the real `vk_common_GetPhysicalDeviceProperties2` function.

The title is refused before signing if this gate fails, and exFAT packaging
requires the matching successful report and ELF hash.

## Verification and remaining hardware test

The regression test uses actual PS5-target ELF objects and `ld.lld`: a lazy
archive reproduces the missing callback, and a whole-archive link supplies it
and its table relocation. The same audit fails on the preserved 015 ELF with
38 omissions and passes on the real 016 ELF with zero omissions and the
properties2 pointer present. The game registry and direct-syscall audits are
also required before signing.

Package: `build-native-exfat/dist/PPSA35820-native-vulkan-016.exfat`.
The console log must identify `BUILD 01.000.016`; `VULKAN ACTIVE` establishes
that accelerated initialization completed. Fixing the identified 015 crash
does not establish that later shader creation or gameplay has passed on PS5.
That console test remains pending.
