#include "graphics_menu_ps5.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "native_ps5_graphics_menu_test:%d: %s\n", __LINE__, #condition); \
        return 1; \
    } \
} while (0)

int main(void)
{
    KhdaysPs5GraphicsMenu menu;
    khdays_ps5_graphics_menu_init(&menu, 4, 4, 0, 0, 1);
    CHECK(menu.scale == 4 && menu.antialiasing == 2);
    khdays_ps5_graphics_menu_open(&menu);

    /* Scale changes keep the requested raster target inside the renderer's
     * tested 8x bound, lowering AA when necessary. */
    CHECK(khdays_ps5_graphics_menu_handle(&menu, KHDAYS_PS5_MENU_UP) == 0);
    CHECK(menu.selected == 4);
    CHECK(khdays_ps5_graphics_menu_handle(&menu, 0) == 0);
    CHECK(khdays_ps5_graphics_menu_handle(&menu, KHDAYS_PS5_MENU_DOWN) == 0);
    CHECK(menu.selected == 0);
    CHECK(khdays_ps5_graphics_menu_handle(&menu, 0) == 0);
    CHECK(khdays_ps5_graphics_menu_handle(&menu, KHDAYS_PS5_MENU_LEFT) == KHDAYS_PS5_GRAPHICS_SCALE);
    CHECK(menu.scale == 3 && menu.antialiasing == 2);

    CHECK(khdays_ps5_graphics_menu_handle(&menu, 0) == 0);
    CHECK(khdays_ps5_graphics_menu_handle(&menu, KHDAYS_PS5_MENU_DOWN) == 0);
    CHECK(menu.selected == 1);
    CHECK(khdays_ps5_graphics_menu_handle(&menu, 0) == 0);
    CHECK(khdays_ps5_graphics_menu_handle(&menu, KHDAYS_PS5_MENU_RIGHT) == KHDAYS_PS5_GRAPHICS_AA);
    CHECK(menu.antialiasing == 1);

    uint32_t *pixels = calloc(960u * 540u, sizeof(*pixels));
    CHECK(pixels != NULL);
    khdays_ps5_graphics_menu_draw(&menu, pixels, 960, 540);
    CHECK(pixels[0] == UINT32_C(0xff221510)); /* RGBA8 bytes are 10 15 22 ff */
    CHECK(pixels[66u * 960u + 57u] != pixels[0]);
    free(pixels);

    CHECK(khdays_ps5_graphics_menu_handle(&menu, 0) == 0);
    CHECK(khdays_ps5_graphics_menu_handle(&menu, KHDAYS_PS5_MENU_DOWN) == 0);
    CHECK(menu.selected == 2);
    CHECK(khdays_ps5_graphics_menu_handle(&menu, 0) == 0);
    CHECK(khdays_ps5_graphics_menu_handle(&menu, KHDAYS_PS5_MENU_DOWN) == 0);
    CHECK(menu.selected == 3);
    CHECK(khdays_ps5_graphics_menu_handle(&menu, 0) == 0);
    CHECK(khdays_ps5_graphics_menu_handle(&menu, KHDAYS_PS5_MENU_RIGHT) == KHDAYS_PS5_GRAPHICS_OUTPUT);
    CHECK(menu.output == 1);
    CHECK(khdays_ps5_graphics_menu_handle(&menu, 0) == 0);
    CHECK(khdays_ps5_graphics_menu_handle(&menu, KHDAYS_PS5_MENU_DOWN) == 0);
    CHECK(menu.selected == 4);
    CHECK(khdays_ps5_graphics_menu_handle(&menu, 0) == 0);
    CHECK(khdays_ps5_graphics_menu_handle(&menu, KHDAYS_PS5_MENU_RIGHT) == KHDAYS_PS5_GRAPHICS_MOVIES);
    CHECK(menu.hd_movies == 0);
    CHECK(khdays_ps5_graphics_menu_handle(&menu, 0) == 0);
    CHECK(khdays_ps5_graphics_menu_handle(&menu, KHDAYS_PS5_MENU_CROSS) == KHDAYS_PS5_GRAPHICS_CLOSE);
    CHECK(!menu.open);
    puts("native_ps5_graphics_menu_test: all checks passed");
    return 0;
}
