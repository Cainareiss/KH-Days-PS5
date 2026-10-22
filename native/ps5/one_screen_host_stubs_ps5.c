/* The one-screen composition code labels its settings prompt using the same
 * positional names as the PC port.  The native PS5 input path does not run
 * SDL's device registry, so provide the small, deterministic device facade
 * that the shared renderer needs.  This does not change the game's DS action
 * mapping: it only lets the UI select the PlayStation icon family. */
#include "vulkan_compat/SDL3/SDL.h"
#include "host/app/devices.h"

#include <string.h>

/* SDL_Gamepad is intentionally opaque in the compatibility header.  The
 * shared one-screen code only passes this token to SDL_GetGamepadType(). */
struct SDL_Gamepad {
    int ps5;
};

static SDL_Gamepad ps5_gamepad = {1};
static KhdaysProfile ps5_profile;
static int ps5_profile_ready;

static void ensure_profile(void)
{
    if (ps5_profile_ready) {
        return;
    }

    memset(&ps5_profile, 0, sizeof(ps5_profile));
    ps5_profile.version = 1;
    for (int device = 0; device < KHDAYS_DEVICE_COUNT; ++device) {
        for (int layer = 0; layer < KHDAYS_LAYER_COUNT; ++layer) {
            for (int action = 0; action < KHDAYS_ACT_COUNT; ++action) {
                for (int slot = 0; slot < KHDAYS_INPUTS_PER_ACTION; ++slot) {
                    ps5_profile.input[device][layer][action][slot] = KHDAYS_NO_INPUT;
                }
            }
        }
    }

    /* Options opens the native settings overlay.  Keep the second slot empty
     * rather than advertising the PS button, which the firmware reserves. */
    ps5_profile.input[KHDAYS_DEVICE_GAMEPAD][KHDAYS_LAYER_BASE][KHDAYS_ACT_OPEN_SETTINGS][0] =
        KHDAYS_PAD_START;
    ps5_profile.input[KHDAYS_DEVICE_GAMEPAD][KHDAYS_LAYER_BASE][KHDAYS_ACT_OPEN_SETTINGS][1] =
        KHDAYS_NO_INPUT;
    ps5_profile_ready = 1;
}

const KhdaysProfile *khdays_devices_profile(void)
{
    ensure_profile();
    return &ps5_profile;
}

SDL_Gamepad *khdays_devices_gamepad(void)
{
    return &ps5_gamepad;
}

int khdays_devices_pad_in_use(void)
{
    /* The native title has no SDL keyboard/mouse path.  The connected
     * DualSense is therefore the active prompt device from boot. */
    return 1;
}

const char *khdays_pad_input_name(int input)
{
    /* These are the stable positional names consumed by the shared
     * composition code.  SDL's PS5 family selection supplies the visible
     * PlayStation variant once its icon atlas is present. */
    static const char *const names[KHDAYS_PAD_COUNT] = {
        [KHDAYS_PAD_SOUTH] = "south",
        [KHDAYS_PAD_EAST] = "east",
        [KHDAYS_PAD_WEST] = "west",
        [KHDAYS_PAD_NORTH] = "north",
        [KHDAYS_PAD_DPUP] = "dpup",
        [KHDAYS_PAD_DPDOWN] = "dpdown",
        [KHDAYS_PAD_DPLEFT] = "dpleft",
        [KHDAYS_PAD_DPRIGHT] = "dpright",
        [KHDAYS_PAD_LEFTSHOULDER] = "leftshoulder",
        [KHDAYS_PAD_RIGHTSHOULDER] = "rightshoulder",
        [KHDAYS_PAD_LEFTTRIGGER] = "lefttrigger",
        [KHDAYS_PAD_RIGHTTRIGGER] = "righttrigger",
        [KHDAYS_PAD_LEFTSTICK] = "leftstick",
        [KHDAYS_PAD_RIGHTSTICK] = "rightstick",
        [KHDAYS_PAD_START] = "start",
        [KHDAYS_PAD_BACK] = "back",
        [KHDAYS_PAD_GUIDE] = "guide",
        [KHDAYS_PAD_MISC1] = "misc1",
        [KHDAYS_PAD_TOUCHPAD] = "touchpad",
        [KHDAYS_PAD_PADDLE1] = "paddle1",
        [KHDAYS_PAD_PADDLE2] = "paddle2",
        [KHDAYS_PAD_PADDLE3] = "paddle3",
        [KHDAYS_PAD_PADDLE4] = "paddle4",
    };
    return input >= 0 && input < KHDAYS_PAD_COUNT ? names[input] : NULL;
}
