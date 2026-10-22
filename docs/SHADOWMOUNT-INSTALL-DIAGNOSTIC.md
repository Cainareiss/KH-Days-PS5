# ShadowMountPlus: installing without a home-screen entry

Investigated on 2026-10-15.

## Concrete packaging defects

The previously delivered folder contained only param.json in sce_sys.
The generated title's icon existed at ps5/sce_sys/icon0.png, but the manual
packaging path omitted it from dist/PPSA12345 and from the first exFAT image.

ShadowMountPlus prints "Installing" before calling mount_and_install. Inside
that function, it copies sce_sys/icon0.png into the staging directory. If the
copy fails, it returns false before attempting title registration. Therefore,
an absent icon is sufficient to prevent registration with the inspected code.
The console's actual log has not been retrieved; other failures may coexist.

Source inspected:
[sm_install.c, revision 98c524890176c7b474fcb03387bec88c4a559861](https://github.com/drakmor/ShadowMountPlus/blob/98c524890176c7b474fcb03387bec88c4a559861/src/sm_install.c).
See the staged-icon copy in mount_and_install and the "Installing" notification
in process_scan_candidates.

The previous bundle also omitted shaders/glsl/khdaysps5/model.vert.spv and
model.frag.spv. These are loaded by the application after launch. Their absence
does not explain registration failure, but would affect the next launch test.

## Correction and verification

Run from the project root:

~~~powershell
./scripts/build_shadowmount_image.ps1
~~~

The script stages the existing 512x512 icon and both compiled model shaders,
checks required files, then invokes ShadowMountPlus's official
New-OsfExfatImage.ps1. It preserves previous images and refuses to overwrite
its output. The new image is PPSA12345-fixed.exfat.

It verifies the raw exFAT header and 64 KiB clusters, mounts the image
read-only, compares all relative paths and file lengths with the source,
and verifies SHA-256 for the ten mandatory executable/metadata/media/shader
files. It dismounts the verification volume and writes a validation JSON next
to the image. Hardware registration and execution remain pending.

The image-builder checkout is a local path supplied with `-ImageBuilder`.
Its revision during this investigation was 98c524890176c7b474fcb03387bec88c4a559861.

## Next console test

Keep only the corrected source for PPSA12345 in the loader's scan paths. Move
the previous image/folder outside those paths; leaving both can produce duplicate
titleId discovery. Copy the corrected image to USB:\homebrew and restart the
console and loader to reset failed-install retries.

If the same symptom remains, retrieve /data/shadowmount/debug.log, including
the lines before and after PPSA12345. Also record firmware and loader versions.
Useful messages distinguish:

- COPY / Failed to copy staged icon: source icon missing or unreadable.
- COPY / Failed to copy sce_sys staging: metadata read/write failure.
- REG / FAIL: registration error, inspect the hexadecimal return code.
- AppInstallTitleDir bridge unavailable: registration bridge failure.
- RETRY / limit reached: automatic retries exhausted.

Do not delete app.db or unrelated console metadata as a diagnostic step.

Official references:

- [Image creation and troubleshooting](https://github.com/drakmor/ShadowMountPlus#creating-an-exfat-image).
- [Similar installing-without-icon report](https://github.com/drakmor/ShadowMountPlus/issues/4).
- [Registration failure after rest mode](https://github.com/drakmor/ShadowMountPlus/issues/147).
