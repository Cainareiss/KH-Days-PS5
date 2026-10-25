#ifndef KHDAYS_PS5_GRAPHICS_MENU_H
#define KHDAYS_PS5_GRAPHICS_MENU_H

#include <stdint.h>

enum {
    KHDAYS_PS5_MENU_UP       = 1u << 0,
    KHDAYS_PS5_MENU_DOWN     = 1u << 1,
    KHDAYS_PS5_MENU_LEFT     = 1u << 2,
    KHDAYS_PS5_MENU_RIGHT    = 1u << 3,
    KHDAYS_PS5_MENU_CIRCLE   = 1u << 4,
    KHDAYS_PS5_MENU_CROSS    = 1u << 5,
    KHDAYS_PS5_MENU_OPTIONS  = 1u << 6,
    KHDAYS_PS5_MENU_TRIANGLE = 1u << 7,
};

enum {
    KHDAYS_PS5_GRAPHICS_SCALE  = 1u << 0,
    KHDAYS_PS5_GRAPHICS_AA     = 1u << 1,
    KHDAYS_PS5_GRAPHICS_FILTER = 1u << 2,
    KHDAYS_PS5_GRAPHICS_OUTPUT = 1u << 3,
    KHDAYS_PS5_GRAPHICS_MOVIES = 1u << 4,
    KHDAYS_PS5_GRAPHICS_CLOSE  = 1u << 5,
};

typedef struct KhdaysPs5GraphicsMenu {
    unsigned scale;  /* internal render multiplier, 1..4 */
    unsigned antialiasing; /* supersampling multiplier, 1 (off)..4; scale*AA <= 8 */
    unsigned filter; /* 0: nearest, 1: linear */
    unsigned output; /* 0: automatic/1080p default, 1: 1080p, 2: 4K */
    unsigned hd_movies; /* use prepared HD movies when present */
    unsigned selected;
    uint32_t previous_buttons;
    int open;
} KhdaysPs5GraphicsMenu;

void khdays_ps5_graphics_menu_init(KhdaysPs5GraphicsMenu *menu,
                                   unsigned scale, unsigned antialiasing,
                                   unsigned filter, unsigned output, unsigned hd_movies);
void khdays_ps5_graphics_menu_open(KhdaysPs5GraphicsMenu *menu);
unsigned khdays_ps5_graphics_menu_handle(KhdaysPs5GraphicsMenu *menu, uint32_t buttons);
void khdays_ps5_graphics_menu_draw(const KhdaysPs5GraphicsMenu *menu,
                                   uint32_t *rgba, unsigned width, unsigned height);

#endif
