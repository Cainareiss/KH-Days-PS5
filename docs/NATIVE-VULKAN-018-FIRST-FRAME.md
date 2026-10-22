# Build 01.000.018: tolerate the initial empty 3D frame

The 017 console log reported `wants=0x2a`, `display3d=1`, and then aborted
because the package had no 3D payload. The game enables its 3D display mode
before it has captured the first 3D list; that list is made available to the
following frame. The first package also showed zero 3D polygons, vertices and
flushes.

Build 018 accepts this initial state. It renders the available 2D package and
does not invoke the 3D rasterizer until the frame package contains its 3D
payload. Subsequent frames with `KHDAYS_WANT_3D` use the normal 3D path. The
first presented frames log both `display3d` and `payload3d` so the next console
log shows when the 3D package becomes available.

Hardware confirmation is still required. A successful startup should show
`BUILD 01.000.018`, an accepted initial package with `display3d=1 payload3d=0`,
and advancing `VULKAN frame` entries. Later accepted packages should report
`payload3d=1` once the game's first 3D list has been captured.
