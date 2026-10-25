# Optional HD pieces from the PC installer

The PC installer screenshot exposes two separate asset-generation choices:

| Option | Inputs used by the pinned installer | Native PS5 status |
| --- | --- | --- |
| HD menus/HUD | KH 1.5+2.5 **and** KH 2.8 for the installer's complete menu/HUD set; generates `mods/HD PACK` | Build 022 includes the available extracted pack, with 14 English title variants and its HD atlas. The original title Settings bar is preserved and opens the native graphics screen with Triangle. Options retains the game's Start action. This does not establish complete KH 2.8 HUD coverage. |
| HD videos | KH 1.5+2.5; generates `data/cutscenes` MP4s and subtitles | Build 022 opens the 38 converted 1280x720 clips through the absolute `/app0/data/cutscenes/cinematics/hd<ID>.mpg` path. Movie selection, decoder results and fallback reasons persist in `boot.log`. PS5 playback of this corrected build requires a console test. |

The HD video source set and conversion manifest currently available are under
`E:\KH-Days-HD-mpeg1-20261025`; the independent PS5 staging tree is generated
from that manifest and the existing subtitle schedules. The Birth by Sleep
translation/patch is not a substitute for these collection archives.

The independent PS5 movie set is built with
`scripts/stage_ps5_hd_movies.py`, which only accepts hash-matched clips whose
MPEG-1 video and stereo 48 kHz MP2 audio have passed the pinned `pl_mpeg`
decoder probe. The image builder copies the resulting `data/cutscenes` tree,
checks each movie hash before packaging, then verifies every file again from a
read-only remount of the finished image. The graphics menu's HD-cutscene toggle is
saved in `graphics.ini`; a missing or unreadable HD clip falls back to the
game's original movie. The current converted set is 720p, not 2K.

The previous build's byte-level image verification succeeded, but its runtime
fallback path was relative to the loader's working directory and the supplied
asset root did not contain `mods/HD PACK`. These were separate integration
errors: stored video bytes alone did not prove playback, and the native
graphics overlay did not replace the title. Build 022 verifies the required
title files, preserved animated Triangle prompts and exact movie paths before
staging, then verifies their hashes from the finished image.

The title's Settings action is published only while its layout exposes
`button open_settings` and the game reports a visible title menu. Triangle is
consumed in that situation; outside it, Triangle keeps its DS X action.
Options is never a Settings opener. The native graphics page still allows
Options or Cross to close it after it has been opened from the title.
