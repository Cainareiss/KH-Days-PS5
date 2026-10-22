/* Reuses the native game's real GPU rasterizer, 2D compositor, HD textures and
 * display captures. No CPU-rendered screen is uploaded as the gameplay image. */
#include "vulkan_game_renderer.h"
#include "gpu_raster3d.h"
#include "gfx_render.h"
#include "runtime/display/frame_package.h"
#include "runtime/mods/textures.h"
#include "host.h"
#include "onescreen.h"
#include "ui/composition.h"

#include <string.h>

/* These accessors are appended by prepare_native_vulkan_sources.py. */
extern SDL_GPUTexture *khdays_ps5_gfx_screen_raw(int engine, int *w, int *h);
extern SDL_GPUTexture *khdays_ps5_gfx_captured_raw(int *w, int *h);
extern SDL_GPUTexture *khdays_ps5_gfx_picture_raw(int *w, int *h);

static SDL_GPUDevice *opened;
static int one_screen_made;

int khdays_native_vulkan_renderer_open(SDL_GPUDevice *device)
{
    if (device != NULL && opened == device) return 1;
    khdays_native_vulkan_renderer_close();
    if (device == NULL) return SDL_SetError("PS5 Vulkan: no device");
    if (!khdays_raster3d_open(device)) return 0;
    if (!khdays_gfx_render_open(device)) {
        khdays_raster3d_close();
        return 0;
    }
    opened = device;
    one_screen_made = 0;
    return 1;
}

void khdays_native_vulkan_renderer_close(void)
{
    if (opened == NULL) return;
    /* The 2D compositor holds references to the 3D renderer's images. */
    khdays_gfx_render_close();
    khdays_raster3d_close();
    opened = NULL;
    one_screen_made = 0;
}

int khdays_native_vulkan_renderer_draw(const KhdaysFramePackage *p, int scale,
                                      KhdaysNativeVulkanFrame *out)
{
    SDL_GPUTexture *engine[2];
    int width[2] = {0, 0}, height[2] = {0, 0};
    const int required_2d = KHDAYS_WANT_2D;
    KhdaysComposition composition;
    KhdaysOneScreenDraw one;
    int picture = 0;
    if (out == NULL) return SDL_SetError("PS5 Vulkan: missing frame output");
    memset(out, 0, sizeof(*out));
    if (opened == NULL) return SDL_SetError("PS5 Vulkan: renderer is not open");
    if (p == NULL) return SDL_SetError("PS5 Vulkan: missing frame package");
    if ((p->wants & required_2d) != (unsigned)required_2d)
        return SDL_SetError("PS5 Vulkan: frame package lacks 2D payload (wants=0x%02x)", p->wants);
    if (p->upper > 1)
        return SDL_SetError("PS5 Vulkan: invalid upper-screen selector (%u)", p->upper);
    if (scale < 1 || scale > 8) return SDL_SetError("PS5 Vulkan: scale must be 1..8");

    /* The game can publish a 2D-only first frame while its display registers
     * already select 3D. The first 3D list is captured for the next VBlank.
     * Until that payload exists, draw the available 2D package without a 3D
     * raster; do not abort or read the absent layer/list. */
    const int has_3d = (p->wants & KHDAYS_WANT_3D) != 0;
    const int raster = has_3d && (p->info[0].is3d || khdays_gfx_render_capture_frame(p));

    const int use_one_screen = (khdays_host_graphics_flags() & KHDAYS_HOST_ONE_SCREEN) != 0;
    memset(&composition, 0, sizeof(composition));
    memset(&one, 0, sizeof(one));
    if (use_one_screen) {
        const int columns = (int)SDL_lroundf(p->list3d.widen * KHDAYS_FRAME_W);
        khdays_onescreen_prepare(p, columns > KHDAYS_FRAME_W ? columns : KHDAYS_FRAME_W,
                                 &composition, &one);
        picture = composition.screens == KHDAYS_SCREENS_UPPER ||
                  composition.screens == KHDAYS_SCREENS_LOWER;
        if (picture && one_screen_made && !khdays_onescreen_makes_picture(p)) {
            out->upper = out->lower = khdays_ps5_gfx_picture_raw(&width[0], &height[0]);
            if (out->upper == NULL) return SDL_SetError("PS5 Vulkan: retained one-screen picture is missing");
            out->upper_width = out->lower_width = width[0];
            out->upper_height = out->lower_height = height[0];
            out->frame = p->frame;
            out->output_layout = 5;
            return 1;
        }
    }

    khdays_raster3d_use_list(NULL);
    khdays_raster3d_set_one_screen(picture);
    khdays_raster3d_set_hud_shapes(picture ? composition.shapes3d : NULL,
                                   picture ? composition.shape3d_count : 0);
    khdays_raster3d_set_fine(khdays_textures_hd());
    /* As upstream: display-capture blends still need 3D although the DS's
     * display mode for this frame only shows a VRAM bank. */
    if (raster && !khdays_raster3d_rasterize(p, scale, 0)) return 0;
    khdays_gfx_render_set_shown(0.0f);
    if (picture) {
        SDL_GPUTexture *color3d = NULL, *attr3d = NULL;
        int raster_columns = one.columns;
        /* The rasterizer clamps the list's width to its supported target.
         * Keep the one-screen compositor's width identical, as upstream's
         * gpu_present.c does, instead of rejecting a valid clamped target. */
        if (raster && khdays_raster3d_targets(&color3d, &attr3d, &raster_columns))
            one.columns = raster_columns;
        const int draw_scale = raster || composition.backdrop != NULL ? scale : 1;
        if (!khdays_gfx_render_draw_one(p, draw_scale, raster, &one)) return 0;
        one_screen_made = 1;
        out->upper = out->lower = khdays_ps5_gfx_picture_raw(&width[0], &height[0]);
        if (out->upper == NULL) return SDL_SetError("PS5 Vulkan: one-screen compositor produced no image");
        out->upper_width = out->lower_width = width[0];
        out->upper_height = out->lower_height = height[0];
        out->frame = p->frame;
        out->output_layout = 5;
        return 1;
    }
    if (!khdays_gfx_render_draw(p, raster ? scale : 1, raster)) return 0;
    engine[0] = khdays_ps5_gfx_screen_raw(0, &width[0], &height[0]);
    engine[1] = khdays_ps5_gfx_captured_raw(&width[1], &height[1]);
    if (engine[1] == NULL)
        engine[1] = khdays_ps5_gfx_screen_raw(1, &width[1], &height[1]);
    if (engine[0] == NULL || engine[1] == NULL)
        return SDL_SetError("PS5 Vulkan: no composed screen images");
    const unsigned upper = p->upper;
    out->upper = engine[upper];
    out->lower = engine[!upper];
    out->upper_width = width[upper]; out->upper_height = height[upper];
    out->lower_width = width[!upper]; out->lower_height = height[!upper];
    out->frame = p->frame;
    if (use_one_screen)
        out->output_layout = composition.screens == KHDAYS_SCREENS_SIDE_BY_SIDE ? 7 : 6;
    return 1;
}
