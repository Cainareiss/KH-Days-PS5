/* The one-screen composition code can optionally label its settings prompt
 * using the PC port's SDL device registry. PS5 input is supplied by the
 * native host I/O layer, so this optional prompt uses no SDL gamepad object. */
#include "vulkan_compat/SDL3/SDL.h"
#include "host/app/devices.h"

const KhdaysProfile *khdays_devices_profile(void)
{
    return NULL;
}

SDL_Gamepad *khdays_devices_gamepad(void)
{
    return NULL;
}

int khdays_devices_pad_in_use(void)
{
    return 0;
}

const char *khdays_pad_input_name(int input)
{
    (void)input;
    return NULL;
}
