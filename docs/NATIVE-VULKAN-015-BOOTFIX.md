# Native Vulkan startup, build 01.000.015

The console log supplied as `<pasta-local-do-usuario>\Documents\boot.log` identifies
build `01.000.014`, then reports SIGSEGV (11), fault address zero and RIP zero,
before a game frame or scene is recorded. The last completed checkpoint is the
host's build announcement. That establishes a startup failure consistent with
a NULL function call; it does **not** identify which function was missing or
prove that the failure occurred inside RADV. No Vulkan bootstrap checkpoint
existed before the crash in that build.

## Changes

- Validate all 76 required global, instance and device entrypoints in phases,
  before invoking them. An absent command reports its exact name and returns
  failure to the existing CPU fallback.
- Use `volkLoadInstanceOnly` after instance creation. Check
  `vkGetDeviceProcAddr` before loading device commands. This follows
  [Volk's documented instance/device loading sequence](https://github.com/zeux/volk).
- Allow cleanup of partial initialization when a destroy or idle command is
  unavailable; no device/swapchain resource creation proceeds until its
  dispatch gate passes.
- Persist timing/layout, loader, instance, GPU, device, display and command-pool
  startup checkpoints. The initial boot marker now identifies version 015 too.
- For RIP zero, persist the interrupted stack's return address and its ELF
  offset so a remaining NULL call can be mapped to its caller. The normal
  fatal record is written first and remains allocation/lock free.

## Local validation

`python -m unittest discover -s tests -p test_native_vulkan_bootstrap.py -v`
builds and runs the real backend plus the bundled Volk on Windows. A test ICD
omits each required command in turn. All 76 cases return a named error without
calling NULL, and check cleanup of any instance/device already created. A
separate check inventories every Vulkan call in the backend to catch omissions
from the dispatch gates.

The persistent crash-record test also checks the new caller/ELF offset record.
These are startup error-path tests; they do not execute PS5 shaders or prove
the actual console crash is fixed. The host's Intel HD 620 Vulkan driver
reports Win32 surfaces but no `VK_KHR_display`, so it cannot validate PS5
VideoOut on this PC.

## Console validation

Image: `build-native-exfat/dist/PPSA35820-native-vulkan-015.exfat`.
Use this new image in place of 014, then check the new log for
`BUILD 01.000.015`. `VULKAN ACTIVE` remains the acceleration success marker.
`missing Vulkan entrypoint:` followed by a CPU fallback is an orderly failure,
not proof of accelerated rendering. An internal driver crash may still need
the new checkpoint and `NULL_CALL ... return_ELF+...` record to locate it.

Hardware result: pending. Versions 013 and 014 remain available unchanged.
