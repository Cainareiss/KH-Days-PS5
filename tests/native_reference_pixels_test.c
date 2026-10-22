/* Reproduce default UI requesting descriptions on a bitmap-only host. The
 * decisions header contains the actual pinned and adapted reference_draws
 * functions, extracted by test_native_runtime_sources.py. Use the real DS
 * rasterizer and real UI defaults to check resulting pixels. */
#include "runtime/display/frame_package.h"
#include "ui/parts.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint32_t u32;
static int host_wants, check_composition;
static const char *record_dir, *trace_2d;
int khdays_host_frame_wants(void) { return host_wants; }
static int host_checks(void) { return 0; }
int khdays_gfx_check_on(void) { return 0; }
static int shot_at(u32 frame) { (void)frame; return 0; }
void khdays_log(const char *format, ...) { (void)format; }
#include "display_decisions.h"

static uint8_t io[0x1100], palette[0x800], oam[0x800];
static uint32_t screens[2][256 * 192];
static KhdaysVramPages pages;

static void render_if_requested(int draw)
{
    memset(screens, 0, sizeof(screens));
    if (!draw) return;
    KhdaysGpuInput input = {0};
    KhdaysGpu2dState state[2] = {0};
    input.io = io;
    input.palette = palette;
    input.oam = oam;
    input.vram = &pages;
    for (int engine = 0; engine < 2; ++engine) {
        khdays_gpu2d_frame_ex(&input, &state[engine], engine, screens[engine], NULL, NULL, NULL, NULL);
    }
}

int main(void)
{
    khdays_ui_parts_init();
    assert(khdays_ui_any());
    assert(khdays_ui_mode(KHDAYS_UI_TEXT) == KHDAYS_UI_ON);
    assert(khdays_ui_mode(KHDAYS_UI_HUD) == KHDAYS_UI_ON);
    assert(khdays_ui_mode(KHDAYS_UI_PAUSE) == KHDAYS_UI_ON);
    const int want = khdays_ui_any() ? KHDAYS_WANT_2D : 0;
    io[2] = io[0x1002] = 1; /* both displays in graphics mode */
    palette[0] = 0xff; palette[1] = 0x7f; /* white backdrop A */
    palette[0x401] = 0x7c; /* blue backdrop B */

    assert(!upstream_reference_draws(120, want));
    render_if_requested(upstream_reference_draws(120, want));
    assert(screens[0][0] == 0 && screens[1][0] == 0);
    assert(reference_draws(120, want));
    render_if_requested(reference_draws(120, want));
    for (unsigned i = 0; i < 256 * 192; ++i) {
        assert(screens[0][i] == 0xffffff);
        assert(screens[1][i] == 0x0000ff);
    }
    /* A genuine 2D host still owns composition; do not duplicate its work. */
    host_wants = KHDAYS_WANT_2D;
    assert(!reference_draws(120, want));
    host_wants = 0;
    /* A deliberate fade can correctly produce a black output. */
    io[0x6c] = io[0x106c] = 0x10;
    io[0x6d] = io[0x106d] = 0x80;
    render_if_requested(reference_draws(120, want));
    for (unsigned i = 0; i < 256 * 192; ++i) {
        assert(screens[0][i] == 0 && screens[1][i] == 0);
    }
    puts("reference pixels: reproduced skipped composition; bitmap fallback renders both screens; fade respected");
    return 0;
}
