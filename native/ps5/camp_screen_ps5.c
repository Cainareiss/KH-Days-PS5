/* The PC port's optional custom camp/preferences overlay is not part of the
 * native PS5 UI. Keep its extension point inert while retaining the shared
 * game-driven one-screen composition and HUD renderer. */
#include "vulkan_compat/SDL3/SDL.h"
#include "host/screens/camp_screen.h"

int khdays_camp_screen_model(KhdaysCampModel *out)
{
    (void)out;
    return 0;
}

void khdays_camp_screen_applied(int yes)
{
    (void)yes;
}

SDL_GamepadType SDL_GetGamepadType(SDL_Gamepad *gamepad)
{
    /* The PS5 host supplies a DualSense token from
     * one_screen_host_stubs_ps5.c.  Returning the real family here makes the
     * shared settings/prompt renderer choose PlayStation glyphs instead of
     * the generic/DS fallback. */
    return gamepad != NULL ? SDL_GAMEPAD_TYPE_PS5 : SDL_GAMEPAD_TYPE_UNKNOWN;
}
