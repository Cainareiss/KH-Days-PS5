# Build 01.000.019: renderer audit and real GPU verification

The latest supplied PS5 log is still build 017. It ends in the application's
own `abort()` because a valid first package lacks 3D data, while the display
registers already select 3D. Build 018 accepted that state; no console result
for 018 has been supplied. This audit checks the following rendering stages.

## Confirmed defects corrected

- The pinned one-screen compositor records a backdrop upload, releases its
  transfer buffer and submits later. SDL release defers destruction until
  safe. Our shim immediately destroyed the source buffer/memory. All GPU
  resource types now defer destruction while recording; pending presentation
  resources wait for their render fence before destruction. This also covers
  screen/picture resizing before the next command is acquired.
- Void upload/bind/dispatch functions could fail but leave submission looking
  successful. The first recording error is now latched; submission cancels
  that command and preserves its error. Cancellation restores tracked layouts
  before releasing deferred wrappers.
- All six committed shader modules are SPIR-V 1.3. The application requested
  Vulkan 1.0. It now requests/checks Vulkan 1.1.
- Clear/main/PiP writes to overlapping swapchain pixels lacked an execution
  dependency. Each blit now orders preceding transfer writes.
- One-screen composition now takes the actual target width from the rasterizer
  after rasterization, as the upstream PC presenter does. This avoids a later
  width mismatch when the rasterizer clamps a wide picture to 768 columns.

Official contracts: [SDL transfer-buffer release](https://wiki.libsdl.org/SDL3/SDL_ReleaseGPUTransferBuffer)
and [Khronos Vulkan/SPIR-V versions](https://docs.vulkan.org/guide/latest/versions.html).

## Verification

`python -m unittest discover -s tests -p 'test_native_vulkan*.py' -v`

The frame-contract test compiles the actual PS5 coordinator against pinned
frame/UI headers. It covers the exact `wants=0x2a`, `is3d=1` first frame, the
following 3D frame, capture-only 3D, upper/lower engine mapping, retained
pictures and width clamping. Controlled raster/compositor dependencies do not
simulate shader execution.

The lifetime test uses the actual backend with Vulkan calls that enforce
ownership ordering: record/upload/release/submit, pending image release,
cancel/layout restoration and failed descriptor/bind submission rejection.
Bootstrap dispatch and real ELF link regression checks remain in the suite.

The offscreen probe uses the actual production backend and the prepared,
otherwise unchanged `gpu_raster3d`, `gfx_render` and `gpu_hd` sources. It loads
Windows Vulkan and executes on this PC's **Intel HD Graphics 620**, driver
31.0.101.2145, Vulkan 1.3.215. It creates all six pinned compute pipelines.
No fake GPU handles or fake compute successes are used in this probe.

It copies GPU outputs back and compares all 983,040 pixels across eight cases:

- first 2D-only engine A and B pictures at 256x192;
- 3D clear colour, opaque polygon and native direct-colour textured polygon
  at 512x384;
- one-screen background upload/release at 256x192;
- replacement/resize at 512x384;
- compute downsampling back to 256x192.

Recorded output: `build-native-vulkan-tests/offscreen-probe.log`.

## Remaining evidence required

The Windows probe substitutes only device/display initialization and the HD
replacement registry (empty). It does not execute the PS5 RADV compiler,
sceVideoOut/KHR_display swapchain, the game's full runtime or a mission. It
does not certify performance, all scene formats or completion of the port.
The full PC presenter's interpolation/refresh paths are also not integrated.

The new PS5 image must still show advancing `VULKAN frame` entries and a
successful first mission. Startup-only `GPU command` and `WSI frame` records
now identify recording, compute, submission, fence and presentation progress
without logging every command for the entire session.

## Completed build result

On 2026-10-22 all six Vulkan tests passed (14.399 seconds), including the
76 missing-entrypoint cases and the real GPU readback. The PS5 runtime then
compiled and linked with all 4,955 game initializers, zero omitted Mesa
callbacks, zero direct syscall instructions and a valid FSELF.

Package: `build-native-exfat/dist/PPSA35820-native-vulkan-019.exfat`.
The 742 MiB image copied 1,917 files with no failures and passed exFAT/header,
data-manifest and executable checks. Icon and background are included.

SHA-256: `643E56160DCBA78830385BE0D2833F75D157ABD19130CFBECA910642D55C0AA8`.
