/* Exercise the actual PS5 renderer coordinator against real upstream package
 * types, with deterministic rasterizer/compositor dependencies. No GPU result
 * is inferred: these assertions cover package state, ordering and dimensions. */
#include "vulkan_game_renderer.h"
#include "gpu_raster3d.h"
#include "gfx_render.h"
#include "onescreen.h"
#include "host.h"
#include "runtime/display/frame_package.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static KhdaysFramePackage package;
static char last_error[256];
static int device_token, screen_token[2], picture_token, capture_token;
static int raster_calls, draw_calls, one_calls, target_calls;
static int last_scale, last_raster, last_columns;
static int one_screen, screens, makes_picture, capture_frame, captured;
static int raster_columns;

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "line %d: %s (%s)\n", __LINE__, #x, last_error); return 1; \
} } while (0)

bool SDL_SetError(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(last_error, sizeof(last_error), format, args);
    va_end(args);
    return false;
}
const char *SDL_GetError(void) { return last_error; }
int khdays_raster3d_open(SDL_GPUDevice *device) { return device != NULL; }
void khdays_raster3d_close(void) {}
int khdays_gfx_render_open(SDL_GPUDevice *device) { return device != NULL; }
void khdays_gfx_render_close(void) {}
void khdays_raster3d_use_list(const KhdaysG3dList *list) { (void)list; }
void khdays_raster3d_set_one_screen(int on) { (void)on; }
void khdays_raster3d_set_hud_shapes(const KhdaysShape3d *shapes, int count)
{ (void)shapes; (void)count; }
void khdays_raster3d_set_fine(int on) { (void)on; }
int khdays_textures_hd(void) { return 0; }
int khdays_host_graphics_flags(void) { return one_screen ? KHDAYS_HOST_ONE_SCREEN : 0; }
int khdays_gfx_render_capture_frame(const KhdaysFramePackage *p)
{ (void)p; return capture_frame; }
int khdays_raster3d_rasterize(const KhdaysFramePackage *p, int scale, int smooth)
{ (void)p; (void)scale; (void)smooth; ++raster_calls; return 1; }
int khdays_raster3d_targets(SDL_GPUTexture **color, SDL_GPUTexture **attr, int *columns)
{
    ++target_calls;
    *color = (SDL_GPUTexture *)&screen_token[0];
    *attr = (SDL_GPUTexture *)&screen_token[1];
    *columns = raster_columns;
    return 1;
}
void khdays_gfx_render_set_shown(float shown) { (void)shown; }
int khdays_gfx_render_draw(const KhdaysFramePackage *p, int scale, int raster)
{
    (void)p; ++draw_calls; last_scale = scale; last_raster = raster;
    return 1;
}
int khdays_gfx_render_draw_one(const KhdaysFramePackage *p, int scale, int raster,
                               const KhdaysOneScreenDraw *one)
{
    (void)p; ++one_calls; last_scale = scale; last_raster = raster;
    last_columns = one->columns;
    /* Match the actual compositor's precondition (gfx_render.c:828). */
    if (raster && one->columns != raster_columns)
        return SDL_SetError("the 3D is not as wide as the 2D's description");
    return 1;
}
void khdays_onescreen_prepare(const KhdaysFramePackage *p, int columns,
                              KhdaysComposition *composition, KhdaysOneScreenDraw *draw)
{
    (void)p;
    composition->screens = screens;
    draw->columns = columns;
}
int khdays_onescreen_makes_picture(const KhdaysFramePackage *p)
{ (void)p; return makes_picture; }
SDL_GPUTexture *khdays_ps5_gfx_screen_raw(int engine, int *w, int *h)
{
    *w = 256 * last_scale; *h = 192 * last_scale;
    return (SDL_GPUTexture *)&screen_token[engine];
}
SDL_GPUTexture *khdays_ps5_gfx_captured_raw(int *w, int *h)
{
    *w = 256 * last_scale; *h = 192 * last_scale;
    return captured ? (SDL_GPUTexture *)&capture_token : NULL;
}
SDL_GPUTexture *khdays_ps5_gfx_picture_raw(int *w, int *h)
{
    *w = last_columns * last_scale; *h = 192 * last_scale;
    return (SDL_GPUTexture *)&picture_token;
}

static void reset(void)
{
    khdays_native_vulkan_renderer_close();
    memset(&package, 0, sizeof(package));
    package.wants = KHDAYS_WANT_2D | KHDAYS_WANT_SITUATION;
    package.list3d.widen = 1.0f;
    package.frame = 1;
    raster_calls = draw_calls = one_calls = target_calls = 0;
    last_scale = last_raster = last_columns = 0;
    one_screen = capture_frame = captured = 0;
    screens = KHDAYS_SCREENS_UPPER;
    makes_picture = 1;
    raster_columns = 256;
    last_error[0] = '\0';
    khdays_native_vulkan_renderer_open((SDL_GPUDevice *)&device_token);
}

int main(void)
{
    KhdaysNativeVulkanFrame out;

    /* Build 017's exact initial state: hardware says BG0 is 3D, but capture
     * for the next VBlank has not supplied WANT_3D yet. */
    reset();
    package.wants = 0x2a;
    package.info[0].is3d = 1;
    CHECK(khdays_native_vulkan_renderer_draw(&package, 2, &out));
    CHECK(raster_calls == 0 && draw_calls == 1 && last_raster == 0 && last_scale == 1);
    CHECK(out.upper == (SDL_GPUTexture *)&screen_token[0]);
    CHECK(out.lower == (SDL_GPUTexture *)&screen_token[1]);
    CHECK(out.upper_width == 256 && out.upper_height == 192 && out.frame == 1);

    /* The following package supplies the 3D list and uses the requested scale. */
    package.frame = 2;
    package.wants |= KHDAYS_WANT_3D;
    CHECK(khdays_native_vulkan_renderer_draw(&package, 2, &out));
    CHECK(raster_calls == 1 && draw_calls == 2 && last_raster == 1 && last_scale == 2);
    CHECK(out.upper_width == 512 && out.upper_height == 384 && out.frame == 2);

    /* Both physical screen mappings and engine B's retained display capture. */
    package.upper = 1;
    CHECK(khdays_native_vulkan_renderer_draw(&package, 2, &out));
    CHECK(out.upper == (SDL_GPUTexture *)&screen_token[1]);
    CHECK(out.lower == (SDL_GPUTexture *)&screen_token[0]);
    captured = 1;
    CHECK(khdays_native_vulkan_renderer_draw(&package, 2, &out));
    CHECK(out.upper == (SDL_GPUTexture *)&capture_token);
    CHECK(out.lower == (SDL_GPUTexture *)&screen_token[0]);

    /* Display-capture blends require 3D although info.is3d is false. */
    package.info[0].is3d = 0;
    capture_frame = 1;
    CHECK(khdays_native_vulkan_renderer_draw(&package, 2, &out));
    CHECK(last_raster == 1);

    reset();
    one_screen = 1;
    package.wants = 0x2a;
    package.info[0].is3d = 1;
    CHECK(khdays_native_vulkan_renderer_draw(&package, 2, &out));
    CHECK(raster_calls == 0 && one_calls == 1 && last_raster == 0 && last_scale == 1);
    CHECK(out.output_layout == 5 && out.upper == out.lower);

    /* Match upstream's max-width raster target (3 * 256), even if an input
     * list requests a wider image. Omitting the target query fails here. */
    package.wants |= KHDAYS_WANT_3D;
    package.list3d.widen = 4.0f;
    raster_columns = 768;
    package.frame = 2;
    CHECK(khdays_native_vulkan_renderer_draw(&package, 2, &out));
    CHECK(target_calls == 1 && last_columns == 768 && out.upper_width == 1536);
    CHECK(raster_calls == 1 && one_calls == 2 && last_raster == 1);

    /* The other screen's VBlank keeps the composed one-screen image without
     * rasterizing a list that belongs to the other physical screen. */
    makes_picture = 0;
    package.upper = 1;
    package.frame = 3;
    CHECK(khdays_native_vulkan_renderer_draw(&package, 2, &out));
    CHECK(raster_calls == 1 && one_calls == 2 && target_calls == 1);
    CHECK(out.output_layout == 5 && out.frame == 3 && out.upper_width == 1536);

    screens = KHDAYS_SCREENS_SIDE_BY_SIDE;
    CHECK(khdays_native_vulkan_renderer_draw(&package, 2, &out));
    CHECK(out.output_layout == 7);
    screens = KHDAYS_SCREENS_STACKED;
    CHECK(khdays_native_vulkan_renderer_draw(&package, 2, &out));
    CHECK(out.output_layout == 6);

    /* Invalid structural state fails before entering a compositor. */
    reset();
    package.wants = 0;
    CHECK(!khdays_native_vulkan_renderer_draw(&package, 2, &out));
    CHECK(draw_calls == 0 && one_calls == 0 && raster_calls == 0);
    package.wants = KHDAYS_WANT_2D;
    package.upper = 2;
    CHECK(!khdays_native_vulkan_renderer_draw(&package, 2, &out));
    CHECK(draw_calls == 0 && one_calls == 0 && raster_calls == 0);
    khdays_native_vulkan_renderer_close();
    puts("native Vulkan frame contracts: PASS (first 2D, later 3D, capture, mapping, clamped width, retained image)");
    return 0;
}
