/* Run the real composition parser and the PS5 device facade together. */
#include "ui/composition.h"
#include "host/app/devices.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "native_settings_prompt.h"

const unsigned char khdays_compositions_port[] =
    "units port\n[title]\nbackdrop title/en/root_2_0.txt at centre fit\nscreens upper\nstop\n";
const unsigned int khdays_compositions_port_size = sizeof(khdays_compositions_port) - 1;
static const char *mod_root;

int khdays_mods_find(const char *relative, char *path, int size)
{
    snprintf(path, (size_t)size, "%s/%s", mod_root, relative);
    FILE *file = fopen(path, "rb");
    if (file == NULL) return 0;
    fclose(file);
    return 1;
}

long khdays_host_read_file(const char *path, void *data, size_t room)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) return -1;
    const long read = (long)fread(data, 1, room, file);
    fclose(file);
    return read;
}

void khdays_log(const char *format, ...) { (void)format; }

uint8_t *khdays_png_read(const char *path, int *width, int *height, const char **error)
{
    (void)path; (void)error;
    *width = 2048; *height = 2080;
    /* Only layout geometry is examined here, never texture pixels. */
    return calloc(4, 1);
}

void khdays_png_free(uint8_t *pixels) { free(pixels); }
SDL_Thread *SDL_CreateThread(int (*fn)(void *), const char *name, void *data)
{
    (void)fn; (void)name; (void)data;
    return NULL; /* exercise the supported synchronous image-loading fallback */
}
void SDL_WaitThread(SDL_Thread *thread, int *status) { (void)thread; (void)status; }

int main(int argc, char **argv)
{
    assert(argc == 2);
    mod_root = argv[1];
    assert(khdays_devices_gamepad() != NULL && khdays_devices_pad_in_use());
    assert(SDL_GetGamepadType(khdays_devices_gamepad()) == SDL_GAMEPAD_TYPE_PS5);
    const KhdaysProfile *profile = khdays_devices_profile();
    assert(profile->input[KHDAYS_DEVICE_GAMEPAD][KHDAYS_LAYER_BASE][KHDAYS_ACT_OPEN_SETTINGS][0]
           == KHDAYS_PAD_NORTH);
    assert(profile->input[KHDAYS_DEVICE_GAMEPAD][KHDAYS_LAYER_BASE][KHDAYS_ACT_OPEN_SETTINGS][1]
           == KHDAYS_NO_INPUT);
    assert(strcmp(khdays_pad_input_name(KHDAYS_PAD_EAST), "east") == 0);
    assert(strcmp(khdays_pad_input_name(KHDAYS_PAD_SOUTH), "south") == 0);

    KhdaysSituation situation = {.scene = KHDAYS_SIT_TITLE, .frame = 4};
    KhdaysComposition composed;
    khdays_composition_for(&situation, 341, &composed);
    assert(composed.animated && composed.piece_count == 1);
    assert(composed.button_count == 1 && strcmp(composed.buttons[0].action, "open_settings") == 0);
    assert(composed.pieces[0].texels[0] == 7); /* keyboard Esc fallback */

    settings_prompt(); /* actual unmodified upstream function, PS5 facade */
    khdays_composition_for(&situation, 341, &composed);
    assert(composed.animated && composed.piece_count == 1);
    assert(composed.pieces[0].texels[0] == 361); /* PlayStation Triangle */
    assert(composed.pieces[0].texels[1] == 2041);
    assert(composed.pieces[0].texels[2] == 36 && composed.pieces[0].texels[3] == 36);
    assert(composed.pieces[0].rect[2] == 100); /* icon's right edge stays anchored */
    assert(composed.pieces[0].rect[0] > 0 && composed.pieces[0].rect[0] < 100);
    puts("native DualSense prompt integration: PASS (Esc -> PlayStation Triangle)");
    return 0;
}
