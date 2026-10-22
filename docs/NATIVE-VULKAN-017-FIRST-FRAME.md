# Build 01.000.017: accept the first 2D frame

Build 016 initialized RADV, created the VideoOut swapchain and entered
`NitroMain`. Its first frame then failed the renderer's package check.

The game's frame order explains the missing field: it publishes the current
2D frame first, then captures the 3D list for the next VBlank. The first
package can therefore have `KHDAYS_WANT_2D` without
`KHDAYS_WANT_3D`. Build 016 incorrectly required both flags for every package.

Build 017 requires the current 2D payload and valid upper-screen selector. It
allows the initial 2D-only frame and requires 3D data only when the frame's
display mode or capture plan actually uses the 3D rasterizer. The renderer now
reports `wants` and `upper` for successfully presented diagnostic frames, and
reports the specific missing field if a later package is invalid.

This addresses the condition recorded by the 016 log. A fresh PS5 `boot.log`
must show `BUILD 01.000.017`, `VULKAN package accepted`, and advancing frame
numbers to establish that the console passed the startup frame.
