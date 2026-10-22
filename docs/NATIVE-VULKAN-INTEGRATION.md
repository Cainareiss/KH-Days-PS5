# Native PS5 Vulkan integration

Build `01.000.019` now links the pinned game's real compute renderers to the
PS5 RADV Vulkan implementation. `host_video_ps5.c` asks the display runtime for
the 3D list, composition, 2D memory, and single-screen situation, renders that
frame package through the upstream rasterizer/compositor, and presents the two
GPU images through a `VK_KHR_display` swapchain. The path does not read the
gameplay image back to the CPU. The former tiled CPU `sceVideoOut` presenter is
kept only as a fallback (`KHDAYS_PS5_RENDERER=cpu` selects it explicitly).

The Vulkan path also enables the port's situation-aware one-screen flag. The
upstream one-screen composer chooses the scene's upper/lower picture, or a
stacked/side-by-side two-screen composition where the scene requires both.
These layouts are aspect-fitted into the output instead of stretching the
original DS screens. This is code/build integration only; validate scene
transitions, HUD placement, and mission selection on hardware.

The narrow SDL_GPU compatibility layer implements the subset used by the
pinned renderers: storage buffers, storage images, SPIR-V compute pipelines,
uploads, image copies, and presentation. The committed upstream SPIR-V is
prepared into the build directory without editing the pinned checkout. Image
layout barriers are recorded before source-image blits, and shutdown waits for
the Vulkan device before releasing renderer resources.

The package defaults to 1080p output and 2x 3D render scale. On the PS5,
`KHDAYS_PS5_VIDEO=4k` requests a 3840x2160 display mode when available, and
`KHDAYS_PS5_3D_SCALE=1..4` selects the 3D target scale. These are independent:
4K output does not mean the DS game is rendered at native 4K or that 60 FPS has
been measured. `VULKAN ACTIVE` confirms that Vulkan initialization and shader
pipeline creation succeeded. It does not establish that a frame executed or
reached VideoOut; advancing frame/presentation records and the console's
visible output are still needed. Startup failure reports its cause and falls
back to CPU.

The PS5 title link includes the RADV archive and converts its AGC symbols to
`libSceAgc.prx`/`libSceAgcDriver.prx` imports. The linker audit checks the
renderer and ICD symbols, both AGC modules, the full game registries, a valid
FSELF, and zero direct syscall instructions.

The PC GPU probe below verifies actual compute output. PS5 shader execution,
VideoOut presentation, color correctness, frame pacing, and the first mission
still require a fresh PS5 run and its `boot.log`.

The 014 console log subsequently recorded a zero-PC startup fault. Build 015
adds checked command loading, partial-init cleanup and startup/caller records;
see [the evidence and test scope](NATIVE-VULKAN-015-BOOTFIX.md). This supersedes
the 014 package but does not yet establish a successful Vulkan console run.

The 015 caller record localized the failure to Mesa's internal properties2
query in WSI initialization. Build 016 links the complete RADV archive and
audits the previously omitted weak dispatch implementations before signing
and packaging. See [the root cause and regression evidence](NATIVE-VULKAN-016-WSI-FIX.md).

The 016 console log then reached the first game frame but aborted because the
renderer required a 3D payload before the game had captured its first 3D list.
The 017 log confirmed that the initial package can mark the display as 3D
while still lacking that first list. Build 018 presents its available 2D
payload without reading absent 3D data, then uses 3D normally once the payload
is available. The new exFAT must confirm that the runtime advances beyond this
point.

Build 019 adds deferred GPU resource destruction, rejects failed command
recordings, requests Vulkan 1.1 for the pinned SPIR-V 1.3 and synchronizes
overlapping transfer writes. Its local probe executed the actual backend and
pinned renderer shaders on the PC's Intel GPU and compared 983,040 output
pixels, including 3D geometry and native textures. This does not test PS5
RADV/VideoOut. See [the deep audit and verification scope](NATIVE-VULKAN-019-DEEP-AUDIT.md).
