# Optional HD pieces from the PC installer

The PC installer screenshot exposes two separate asset-generation choices:

| Option | Inputs used by the pinned installer | Native PS5 status |
| --- | --- | --- |
| HD menus/HUD | KH 1.5+2.5 **and** KH 2.8; generates `mods/HD PACK` for the PC port frontend | Not enabled. These are not drop-in replacements for the DS game's native HUD or a PS5 settings screen; their layout/assets need a native consumer and adaptation. |
| HD videos | KH 1.5+2.5; generates `data/cutscenes` MP4s and subtitles | Not enabled. The PS5 build currently marks HD movie playback unavailable because its MP4 decoder path is not connected. |

The local PC collection paths previously supplied are absent on this machine, so
there is no verified source from which to build either option right now. The
Birth by Sleep translation/patch is not a substitute for these collection
archives. Nothing from the PC installer is copied into the Vulkan test image;
the package README states this explicitly.

Once the collection folders are present, the pinned upstream helper can
prepare the assets with `installer/setup_steps.py build-hd --menus --videos
--1525 <KH-1.5+2.5-folder> --28 <KH-2.8-folder> --result <result.json>`. That
only prepares PC-port assets. Enabling them in this native title remains a
separate task: adapt the frontend layouts for the PS5 HUD/settings UI, and
integrate a decoder compatible with the PS5 movie/audio path before allowing
the video option to be selected.
