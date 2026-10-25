/* Small native graphics menu for the PS5 build. It renders to an RGBA canvas,
 * so it does not depend on the desktop SDL window/ImGui settings frontend. */
#include "graphics_menu_ps5.h"

#include <stddef.h>
#include <string.h>

/* Five-column, seven-row uppercase bitmap glyphs; bits 4..0 are pixels. */
static const uint8_t font[36][7] = {
    {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30}, {14,17,16,16,16,17,14},
    {30,17,17,17,17,17,30}, {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
    {14,17,16,23,17,17,15}, {17,17,17,31,17,17,17}, {14,4,4,4,4,4,14},
    {7,2,2,2,18,18,12}, {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17}, {14,17,17,17,17,17,14},
    {30,17,17,30,16,16,16}, {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4}, {17,17,17,17,17,17,14},
    {17,17,17,17,17,10,4}, {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31},
    {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14}, {14,17,1,2,4,8,31},
    {30,1,1,14,1,1,30}, {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
    {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8}, {14,17,17,14,17,17,14},
    {14,17,17,15,1,1,14},
};

enum { W = 960, H = 540 };
/* Little-endian uint32 values whose in-memory bytes are R, G, B, A, as
 * required by SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM. */
static const uint32_t background = 0xff221510u;
static const uint32_t panel = 0xff33221au;
static const uint32_t active = 0xff232333u;
static const uint32_t red = 0xff394be8u;
static const uint32_t white = 0xffeaf2f5u;
static const uint32_t muted = 0xffb8a79du;

static uint8_t glyph(char c, unsigned row)
{
    if (row >= 7) return 0;
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    if (c >= 'A' && c <= 'Z') return font[(unsigned)(c - 'A')][row];
    if (c >= '0' && c <= '9') return font[26u + (unsigned)(c - '0')][row];
    switch (c) {
    case ':': return row == 1 || row == 5 ? 4 : 0;
    case '.': return row == 6 ? 4 : 0;
    case '-': return row == 3 ? 14 : 0;
    case '/': { static const uint8_t slash[7] = {1,1,2,4,8,16,16}; return slash[row]; }
    case '=': return row == 2 || row == 4 ? 14 : 0;
    case '+': return row == 3 ? 14 : (row >= 1 && row <= 5 ? 4 : 0);
    case '(': return row == 0 || row == 6 ? 2 : 4;
    case ')': return row == 0 || row == 6 ? 8 : 4;
    default: return 0;
    }
}

static void fill(uint32_t *pixels, unsigned width, unsigned height,
                 int x, int y, int w, int h, uint32_t color)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int)width) w = (int)width - x;
    if (y + h > (int)height) h = (int)height - y;
    if (w <= 0 || h <= 0) return;
    for (int py = y; py < y + h; ++py) {
        uint32_t *row = pixels + (size_t)py * width + (unsigned)x;
        for (int px = 0; px < w; ++px) row[px] = color;
    }
}

static void text(uint32_t *pixels, unsigned width, unsigned height,
                 int x, int y, const char *s, int scale, uint32_t color)
{
    if (s == NULL || scale < 1) return;
    for (; *s; ++s, x += 6 * scale) {
        for (unsigned row = 0; row < 7; ++row) {
            uint8_t bits = glyph(*s, row);
            for (unsigned col = 0; col < 5; ++col) {
                if (bits & (1u << (4u - col)))
                    fill(pixels, width, height, x + (int)col * scale,
                         y + (int)row * scale, scale, scale, color);
            }
        }
    }
}

static const char *scale_name(unsigned scale)
{
    static const char *const names[] = {"1X", "2X", "3X", "4X"};
    return names[scale >= 1 && scale <= 4 ? scale - 1 : 1];
}

static const char *output_name(unsigned output)
{
    static const char *const names[] = {"AUTO", "1080P", "4K"};
    return names[output <= 2 ? output : 0];
}

static unsigned max_antialiasing(unsigned scale)
{
    const unsigned limit = scale > 0 ? 8u / scale : 1u;
    return limit > 4u ? 4u : limit;
}

static const char *antialiasing_name(unsigned antialiasing)
{
    static const char *const names[] = {"OFF", "2X", "3X", "4X"};
    return names[antialiasing >= 1 && antialiasing <= 4 ? antialiasing - 1 : 0];
}

void khdays_ps5_graphics_menu_init(KhdaysPs5GraphicsMenu *menu,
                                   unsigned scale, unsigned antialiasing,
                                   unsigned filter, unsigned output, unsigned hd_movies)
{
    if (menu == NULL) return;
    memset(menu, 0, sizeof(*menu));
    menu->scale = scale >= 1 && scale <= 4 ? scale : 2;
    menu->antialiasing = antialiasing >= 1 && antialiasing <= 4 ? antialiasing : 1;
    if (menu->antialiasing > max_antialiasing(menu->scale))
        menu->antialiasing = max_antialiasing(menu->scale);
    menu->filter = filter <= 1 ? filter : 0;
    menu->output = output <= 2 ? output : 0;
    menu->hd_movies = hd_movies != 0;
}

void khdays_ps5_graphics_menu_open(KhdaysPs5GraphicsMenu *menu)
{
    if (menu != NULL) {
        menu->open = 1;
        menu->previous_buttons = 0;
    }
}

unsigned khdays_ps5_graphics_menu_handle(KhdaysPs5GraphicsMenu *menu, uint32_t buttons)
{
    if (menu == NULL || !menu->open) return 0;
    const uint32_t pressed = buttons & ~menu->previous_buttons;
    menu->previous_buttons = buttons;
    if (pressed & (KHDAYS_PS5_MENU_CROSS | KHDAYS_PS5_MENU_OPTIONS)) {
        menu->open = 0;
        return KHDAYS_PS5_GRAPHICS_CLOSE;
    }
    if (pressed & KHDAYS_PS5_MENU_UP) menu->selected = (menu->selected + 4u) % 5u;
    if (pressed & KHDAYS_PS5_MENU_DOWN) menu->selected = (menu->selected + 1u) % 5u;
    if (!(pressed & (KHDAYS_PS5_MENU_LEFT | KHDAYS_PS5_MENU_RIGHT | KHDAYS_PS5_MENU_CIRCLE))) return 0;

    if (menu->selected == 0) {
        if ((pressed & KHDAYS_PS5_MENU_LEFT) && menu->scale > 1) --menu->scale;
        else if ((pressed & (KHDAYS_PS5_MENU_RIGHT | KHDAYS_PS5_MENU_CIRCLE)) && menu->scale < 4) ++menu->scale;
        if (menu->antialiasing > max_antialiasing(menu->scale)) {
            menu->antialiasing = max_antialiasing(menu->scale);
            return KHDAYS_PS5_GRAPHICS_SCALE | KHDAYS_PS5_GRAPHICS_AA;
        }
        return KHDAYS_PS5_GRAPHICS_SCALE;
    }
    if (menu->selected == 1) {
        const unsigned maximum = max_antialiasing(menu->scale);
        if (pressed & KHDAYS_PS5_MENU_LEFT)
            menu->antialiasing = menu->antialiasing <= 1u ? maximum : menu->antialiasing - 1u;
        else
            menu->antialiasing = menu->antialiasing >= maximum ? 1u : menu->antialiasing + 1u;
        return KHDAYS_PS5_GRAPHICS_AA;
    }
    if (menu->selected == 2) {
        menu->filter ^= 1u;
        return KHDAYS_PS5_GRAPHICS_FILTER;
    }
    if (menu->selected == 3) {
        menu->output = (menu->output + ((pressed & KHDAYS_PS5_MENU_LEFT) ? 2u : 1u)) % 3u;
        return KHDAYS_PS5_GRAPHICS_OUTPUT;
    }
    menu->hd_movies ^= 1u;
    return KHDAYS_PS5_GRAPHICS_MOVIES;
}

void khdays_ps5_graphics_menu_draw(const KhdaysPs5GraphicsMenu *menu,
                                   uint32_t *rgba, unsigned width, unsigned height)
{
    if (menu == NULL || rgba == NULL || width == 0 || height == 0) return;
    for (size_t i = 0, count = (size_t)width * height; i < count; ++i) rgba[i] = background;
    const int sx = (int)width / W;
    const int sy = (int)height / H;
    const int unit = sx < sy ? sx : sy;
    if (unit < 1) return;
    const int ox = ((int)width - W * unit) / 2;
    const int oy = ((int)height - H * unit) / 2;
    fill(rgba, width, height, ox + 36 * unit, oy + 34 * unit, 888 * unit, 4 * unit, red);
    text(rgba, width, height, ox + 54 * unit, oy + 66 * unit, "CONFIGURACOES GRAFICAS", unit * 3, white);
    text(rgba, width, height, ox + 54 * unit, oy + 105 * unit, "PORT NATIVO PS5  /  KH 358 2 DAYS", unit * 2, muted);

    const int row_y[5] = {136, 202, 268, 334, 400};
    const char *labels[5] = {"ESCALA INTERNA", "ANTIALIASING", "FILTRO DE SAIDA",
                             "RESOLUCAO DE SAIDA", "CUTSCENES HD"};
    const char *values[5] = {
        scale_name(menu->scale), antialiasing_name(menu->antialiasing),
        menu->filter ? "SUAVE" : "NITIDO", output_name(menu->output),
        menu->hd_movies ? "SIM" : "NAO"
    };
    for (unsigned i = 0; i < 5; ++i) {
        const uint32_t color = i == menu->selected ? active : panel;
        fill(rgba, width, height, ox + 44 * unit, oy + row_y[i] * unit,
             872 * unit, 58 * unit, color);
        fill(rgba, width, height, ox + 44 * unit, oy + row_y[i] * unit,
             5 * unit, 58 * unit, i == menu->selected ? red : panel);
        text(rgba, width, height, ox + 68 * unit, oy + (row_y[i] + 18) * unit,
             labels[i], unit * 2, white);
        text(rgba, width, height, ox + 650 * unit, oy + (row_y[i] + 18) * unit,
             values[i], unit * 2, i == menu->selected ? white : muted);
    }
    text(rgba, width, height, ox + 54 * unit, oy + 466 * unit,
         "D PAD NAVEGA  /  ESQUERDA DIREITA ALTERAM", unit * 2, muted);
    text(rgba, width, height, ox + 54 * unit, oy + 490 * unit,
         "CIRCLE CONFIRMA  /  CROSS VOLTA  /  OPTIONS FECHA", unit * 2, white);
    if (menu->output != 0) {
        text(rgba, width, height, ox + 54 * unit, oy + 516 * unit,
             "SAIDA SERA APLICADA AO REINICIAR", unit * 2, red);
    }
}
