# Build 022: title, controls and HD movie selection

The supplied console log identified build 021, an active Vulkan renderer and
`hd_movies=1`. Gameplay reached 4x rendering with 2x supersampling. The repeated
Settings opens were caused by a global Options handler sharing DS Start.

The corrected build preserves the PC title's HD artwork and Settings bar.
Triangle opens Settings only while that bar belongs to a visible title menu.
Options remains the game's Start/pause, and Triangle remains DS X in gameplay.
The available English title pack is included alongside the movies.
Closing Settings suppresses the held closing buttons until release and
immediately presents the retained title image, including when the game skips
publishing an identical frame.

HD movies were packaged correctly but opened using a relative path. The
PS5 loader does not guarantee a working directory of `/app0`; the runtime now
opens `/app0/data/cutscenes/cinematics/hd<ID>.mpg`. Movie decisions and decode
failures are written to the writable `boot.log` even in Release builds with
the optional upstream session log disabled.

Host tests exercise the complete adapted movie state machine with a different
working directory, successful playback, decoder rejection, missing movies,
mod priority and HD disabled. Controller tests check both title and gameplay
button ownership. Renderer tests derive Settings availability from the actual
composition and visible-title state. The HD asset guard requires the title
variants, atlas, animated Triangle prompts and all movie paths before packaging.

On the console, check the HD title and its top-right Triangle Settings bar,
change graphics there, then enter Story Mode. Options should open the game's
own pause/start menu. Start a new game to exercise the opening HD movie; the
log should name its absolute MPEG path, dimensions and decoder result. The
package contains 38 clips at 1280x720. Console playback remains the final
verification for this build.
