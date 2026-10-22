/* Native PS5 display host. RADV/KHR_display is the primary renderer; the
 * reference VideoOut tiling path remains available as a diagnostic fallback. */
#include "host.h"
#include "instance.h"
#include "host_io_ps5.h"
#include "runtime/display/frame_package.h"
#include "platform.h"
#include "sdl_gpu_vulkan.h"
#include "vulkan_game_renderer.h"

#include <ps5platform/kernel.h>
#include <ps5platform/videoout.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <emmintrin.h>
#include <stdatomic.h>

#define FRAME_ALIGNMENT ((size_t)0x200000)
#define MEMORY_TYPE_WC_GARLIC 3
/* SDL's public PS5 backend uses this attribute with SDL_PIXELFORMAT_ABGR8888,
 * not 0xAARRGGBB. Keep the wire value explicit rather than the SDK's misleading
 * B8G8R8A8_SRGB alias. See docs/NATIVE-VIDEO-DIAGNOSTIC.md. */
#define KHDAYS_VIDEO_ABGR8888 UINT64_C(0x8000000022000000)

extern int sceSystemServiceHideSplashScreen(void);

static struct {
    int handle, registered, running, headless, vulkan;
    SDL_GPUDevice *gpu;
    KhdaysFramePackage *frame_package;
    unsigned width, height, next;
    unsigned renderer_scale;
    unsigned presents;
    unsigned diagnostic_next[3], diagnostic_scene[3], scene_events[3];
    unsigned layout, clear_pending[2];
    unsigned render_samples, render_reports;
    uint64_t render_ticks[4];
    uint64_t started_tsc, tsc_hz, work_ticks, wait_ticks;
    size_t frame_bytes, allocation_bytes;
    int64_t physical, marker;
    unsigned char *mapping;
} video = {.handle = -1};

/* Invert the swizzle once, then write each 64 KiB tile sequentially. This
 * avoids millions of per-frame divisions/XORs and scattered WC stores. */
static uint16_t tile_positions[128 * 128];
static uint32_t scanout_screens[2][256 * 192];
static struct {
    unsigned screen;
    uint16_t columns[3840], rows[2160];
} views[2];
static const char *const layout_names[] = {"upper", "lower", "upper-fit", "lower-fit", "stacked"};
static atomic_bool layout_requested;

static void boot_report(const char *format, ...) __attribute__((format(printf, 1, 2)));

static void vulkan_log(const char *message)
{
    boot_report("Vulkan: %s", message != NULL ? message : "(sem mensagem)");
}

static void vulkan_close(void)
{
    if (video.gpu != NULL && !khdays_vk_wait_idle(video.gpu))
        boot_report("ERROR waiting for Vulkan shutdown: %s", khdays_vk_error());
    khdays_native_vulkan_renderer_close();
    if (video.gpu != NULL) khdays_vk_device_close(video.gpu);
    free(video.frame_package);
    video.frame_package = NULL;
    video.gpu = NULL;
    video.vulkan = 0;
}

static void boot_report(const char *format, ...)
{
    char line[1536];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    fprintf(stderr, "host video: %s\n", line);
    const char *root = khdays_host_user_folder();
    if (root == NULL || *root == '\0') return;
    char path[1200];
    const int length = snprintf(path, sizeof(path), "%s/boot.log", root);
    if (length <= 0 || (size_t)length >= sizeof(path)) return;
    FILE *log = fopen(path, "a");
    if (log != NULL) {
        fprintf(log, "host video: %s\n", line);
        fclose(log);
    }
}

static uint16_t read16(const uint8_t *p)
{
    uint16_t value;
    memcpy(&value, p, sizeof(value));
    return value;
}

static uint32_t read32(const uint8_t *p)
{
    uint32_t value;
    memcpy(&value, p, sizeof(value));
    return value;
}

static void area_usage(const KhdaysVramPages *vram, unsigned area,
                       unsigned *mapped_pages, unsigned *nonzero_bytes)
{
    *mapped_pages = 0;
    *nonzero_bytes = 0;
    const size_t page_count =
        (khdays_vram_area_size[area] + KHDAYS_VRAM_PAGE - 1u) / KHDAYS_VRAM_PAGE;
    for (size_t page = 0; page < page_count; ++page) {
        const uint8_t *bytes = vram->page[area][page];
        if (bytes == NULL) continue;
        ++*mapped_pages;
        for (size_t i = 0; i < KHDAYS_VRAM_PAGE; ++i) {
            *nonzero_bytes += bytes[i] != 0;
        }
    }
}

static const unsigned diagnostic_checkpoints[] = {0, 1, 2, 3, 30, 120, 600, 1200};
#define DIAGNOSTIC_POINTS (sizeof(diagnostic_checkpoints) / sizeof(diagnostic_checkpoints[0]))

static int diagnostic_frame(unsigned frame)
{
    for (size_t i = 0; i < DIAGNOSTIC_POINTS; ++i) {
        if (frame == diagnostic_checkpoints[i]) return 1;
    }
    return 0;
}

static int diagnostic_take(unsigned frame, unsigned stage, unsigned *checkpoint)
{
    unsigned next = video.diagnostic_next[stage];
    if (next >= DIAGNOSTIC_POINTS || frame < diagnostic_checkpoints[next]) return 0;
    /* VBlanks can be merged. Sample once at the first observed frame past a
     * checkpoint and consume missed checkpoints without producing a backlog. */
    while (next + 1 < DIAGNOSTIC_POINTS && frame >= diagnostic_checkpoints[next + 1]) ++next;
    *checkpoint = diagnostic_checkpoints[next];
    video.diagnostic_next[stage] = next + 1;
    return 1;
}

static unsigned count_nonzero(const uint8_t *bytes, size_t length)
{
    unsigned count = 0;
    for (size_t i = 0; i < length; ++i) count += bytes[i] != 0;
    return count;
}

static unsigned count_rgb(const uint32_t *pixels)
{
    unsigned count = 0;
    if (pixels == NULL) return 0;
    for (unsigned i = 0; i < 256u * 192u; ++i) count += (pixels[i] & 0x00ffffffu) != 0;
    return count;
}

static uint32_t hash_bytes(const void *data, size_t length)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < length; ++i) hash = (hash ^ bytes[i]) * 16777619u;
    return hash;
}

void khdays_ps5_video_report_ds(unsigned frame, const char *stage, int reference,
                                int want, int ui, uint32_t scene,
                                const uint8_t *io, const uint8_t *palette, const uint8_t *oam,
                                const uint32_t *layer3d, const KhdaysVramPages *vram,
                                const KhdaysG3dList *list, const uint32_t *upper,
                                const uint32_t *lower)
{
    static const unsigned offsets[] = {0x240, 0x241, 0x242, 0x243, 0x244,
                                       0x245, 0x246, 0x248, 0x249};
    unsigned abg_pages, abg_bytes, bbg_pages, bbg_bytes, bobj_pages, bobj_bytes;
    unsigned checkpoint;
    const unsigned stage_id = strcmp(stage, "3d-render") == 0 ? 1u :
                              strcmp(stage, "raw2d") == 0 ? 2u : 0u;
    const int sampled = diagnostic_take(frame, stage_id, &checkpoint);
    int scene_event = 0;
    if (video.diagnostic_scene[stage_id] != scene) {
        video.diagnostic_scene[stage_id] = scene;
        if (video.scene_events[stage_id] < 8) {
            ++video.scene_events[stage_id];
            scene_event = 1;
        }
    }
    if (!sampled && !scene_event) return;
    if (!sampled) checkpoint = frame;
    unsigned layer3d_covered = 0;
    if (layer3d != NULL) {
        for (unsigned i = 0; i < 256u * 192u; ++i) {
            layer3d_covered += layer3d[i] != 0;
        }
    }
    const unsigned layer3d_nonzero = count_rgb(layer3d);
    const unsigned upper_nonzero = count_rgb(upper);
    const unsigned lower_nonzero = count_rgb(lower);
    area_usage(vram, KHDAYS_AREA_ABG, &abg_pages, &abg_bytes);
    area_usage(vram, KHDAYS_AREA_BBG, &bbg_pages, &bbg_bytes);
    area_usage(vram, KHDAYS_AREA_BOBJ, &bobj_pages, &bobj_bytes);
    const unsigned palette_nonzero = count_nonzero(palette, 0x800);
    const unsigned oam_nonzero = count_nonzero(oam, 0x800);
    const unsigned g3d_polys = list != NULL && list->npolys > 0 ? (unsigned)list->npolys : 0;
    const unsigned g3d_verts = list != NULL && list->nverts > 0 ? (unsigned)list->nverts : 0;
    const unsigned g3d_flushes = list != NULL && list->flushes > 0 ? (unsigned)list->flushes : 0;
    boot_report("PIPE stage=%s trigger=%s at=%u ds=%u scene=%08x REF=%d WANT=%02x HOST=%02x UI=%d "
                "DISPCNT=%08x/%08x BGCNT_A=%04x,%04x,%04x,%04x "
                "BGCNT_B=%04x,%04x,%04x,%04x MASTER=%04x/%04x BLD=%04x/%04x "
                "WIN=%04x,%04x/%04x,%04x PAL=%u:%04x/%04x OAM=%u "
                "VRAM=%02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x ABG=%u:%u BGB=%u:%u BOB=%u:%u "
                "G3D=%uP/%uV/%uF PRE=%d/%d DISP3D=%04x CLEAR=%08x/%04x PIX3D=%u/%u SCREENS=%s "
                "PIX=%u/%u HASH=%08x/%08x/%08x",
                stage, sampled ? "checkpoint" : "scene", checkpoint, frame, scene, reference, (unsigned)want,
                (unsigned)khdays_host_frame_wants(), ui,
                read32(io), read32(io + 0x1000), read16(io + 0x08),
                read16(io + 0x0a), read16(io + 0x0c), read16(io + 0x0e),
                read16(io + 0x1008), read16(io + 0x100a), read16(io + 0x100c),
                read16(io + 0x100e), read16(io + 0x6c), read16(io + 0x106c),
                read16(io + 0x50), read16(io + 0x1050), read16(io + 0x48),
                read16(io + 0x4a), read16(io + 0x1048), read16(io + 0x104a),
                palette_nonzero, read16(palette), read16(palette + 0x400), oam_nonzero,
                io[offsets[0]], io[offsets[1]], io[offsets[2]], io[offsets[3]],
                io[offsets[4]], io[offsets[5]], io[offsets[6]], io[offsets[7]],
                io[offsets[8]], abg_pages, abg_bytes,
                bbg_pages, bbg_bytes, bobj_pages, bobj_bytes,
                g3d_polys, g3d_verts, g3d_flushes, list != NULL ? list->npre : 0,
                list != NULL ? list->npre_verts : 0, read16(io + 0x060),
                read32(io + 0x350), read16(io + 0x354),
                layer3d_covered, layer3d_nonzero, upper != NULL && lower != NULL ? "ready" : "n/a",
                upper_nonzero, lower_nonzero,
                layer3d != NULL ? hash_bytes(layer3d, sizeof(uint32_t) * 256u * 192u) : 0,
                upper != NULL ? hash_bytes(upper, sizeof(uint32_t) * 256u * 192u) : 0,
                lower != NULL ? hash_bytes(lower, sizeof(uint32_t) * 256u * 192u) : 0);
    if (list != NULL && list->npolys > 0 && list->polys != NULL) {
        const KhdaysG3dPolygon *p = list->polys;
        const KhdaysG3dVertex *v = list->verts != NULL && p->first >= 0 && p->first < list->nverts ?
                                  list->verts + p->first : NULL;
        boot_report("POLYGON stage=%s ds=%u attr=%08x alpha=%u tex=%08x flags=%08x y=%.2f:%.2f "
                    "vertex=%s %.2f,%.2f,%.4f,w=%.2f",
                    stage, frame, p->attr, (p->attr >> 16) & 31u, p->tex, (unsigned)p->flags,
                    p->ymin, p->ymax, v != NULL ? "valid" : "missing",
                    v != NULL ? v->x : 0.0, v != NULL ? v->y : 0.0,
                    v != NULL ? v->z : 0.0, v != NULL ? v->w : 0.0);
    }
    if (sampled && video.diagnostic_next[stage_id] == DIAGNOSTIC_POINTS) {
        boot_report("PIPE_DONE stage=%s; fixed diagnostic capture complete", stage);
    }
}

static size_t tile_offset(unsigned x, unsigned y)
{
    const uint32_t offset = ((y << 4) & 0x70u) ^ ((y << 5) & 0xf00u) ^
        ((y << 9) & 0x1000u) ^ ((y << 8) & 0x4000u) ^ ((x << 2) & 0xcu) ^
        ((x << 5) & 0x380u) ^ ((x << 4) & 0x400u) ^ ((x << 6) & 0x800u) ^
        ((x << 9) & 0xa000u);
    const size_t block = (y >> 7) * ((video.width + 127u) >> 7) + (x >> 7);
    return (block << 16) + offset;
}

static void setup_view(unsigned index, unsigned screen, unsigned left, unsigned top, unsigned width, unsigned height)
{
    views[index].screen = screen;
    for (unsigned x = 0; x < video.width; ++x)
        views[index].columns[x] = x >= left && x < left + width ?
            (uint16_t)((x - left) * 256u / width) : UINT16_MAX;
    for (unsigned y = 0; y < video.height; ++y)
        views[index].rows[y] = y >= top && y < top + height ?
            (uint16_t)((y - top) * 192u / height) : UINT16_MAX;
}

static void setup_layout(unsigned layout)
{
    video.layout = layout < 5 ? layout : 0;
    if (video.layout == 4) {
        unsigned width = video.height * 2u / 3u, left = (video.width - width) / 2u;
        setup_view(0, 0, left, 0, width, video.height / 2u);
        setup_view(1, 1, left, video.height / 2u, width, video.height / 2u);
    } else {
        unsigned main_screen = video.layout & 1u;
        unsigned width = video.layout >= 2 ? video.height * 4u / 3u : video.width;
        unsigned left = (video.width - width) / 2u;
        unsigned pip_width = video.width / 4u, pip_height = pip_width * 3u / 4u;
        unsigned margin = video.width / 80u;
        setup_view(0, main_screen, left, 0, width, video.height);
        setup_view(1, !main_screen, left + width - pip_width - margin,
                   margin, pip_width, pip_height);
    }
    video.clear_pending[0] = video.clear_pending[1] = 1;
}

static uint32_t to_scanout(uint32_t rrggbb)
{
    return 0xff000000u | ((rrggbb & 0xffu) << 16) | (rrggbb & 0xff00u) | ((rrggbb >> 16) & 0xffu);
}

static uint32_t scanout_at(unsigned x, unsigned y)
{
    if (y >= video.height || x >= video.width) return 0;
    for (unsigned index = 2; index != 0; --index) {
        unsigned sx = views[index - 1].columns[x], sy = views[index - 1].rows[y];
        if (sx != UINT16_MAX && sy != UINT16_MAX)
            return scanout_screens[views[index - 1].screen][sy * 256u + sx];
    }
    return 0;
}

void khdays_ps5_video_cycle_layout(void)
{
    atomic_store(&layout_requested, 1);
}

static void apply_layout_request(void)
{
    if (!atomic_exchange(&layout_requested, 0)) return;
    if (!video.running || video.headless) return;
    unsigned next = (video.layout + 1u) % 5u;
    setup_layout(next);
    boot_report("LAYOUT %s; L3+R3 cycles upper/lower fullscreen, upper/lower 4:3, stacked", layout_names[video.layout]);
    const char *root = khdays_host_user_folder();
    if (root != NULL && *root) {
        char path[1200];
        int n = snprintf(path, sizeof(path), "%s/video-layout.txt", root);
        if (n > 0 && (size_t)n < sizeof(path)) {
            FILE *file = fopen(path, "w");
            if (file != NULL) { fprintf(file, "%s\n", layout_names[video.layout]); fclose(file); }
        }
    }
}

/* Six bounded samples from genuine polygon-heavy gameplay, after the boot
 * checkpoints. Separate raster/UI/submit/capture from CPU 3D work. */
void khdays_ps5_video_report_render(unsigned frame, uint32_t scene, int polygons,
                                   uint64_t began, uint64_t engine_a, uint64_t ui,
                                   uint64_t before3d, uint64_t ended)
{
    if (video.render_reports >= 6 || scene != 2 || polygons < 16) return;
    const uint64_t parts[] = {engine_a - began, ui - engine_a, before3d - ui, ended - before3d};
    for (unsigned i = 0; i < 4; ++i) video.render_ticks[i] += parts[i];
    ++video.render_samples;
    static const unsigned sample_at[] = {1, 10, 30, 120, 300, 600};
    if (video.render_samples < sample_at[video.render_reports]) return;
    double scale = 1.0 / (1000000.0 * video.render_samples);
    boot_report("RENDER ds=%u scene=%u polygons=%d samples=%u avg_ms A=%.3f B_UI=%.3f present_capture=%.3f CPU3D=%.3f",
                frame, scene, polygons, video.render_samples, video.render_ticks[0] * scale,
                video.render_ticks[1] * scale, video.render_ticks[2] * scale, video.render_ticks[3] * scale);
    ++video.render_reports;
}

static int wait_marker(int64_t marker)
{
    uint64_t status[PS5_VIDEO_OUT_FLIP_STATUS_WORDS] = {0};
    if (!marker) return 1;
    for (unsigned i = 0; i < 240; ++i) {
        if (sceVideoOutGetFlipStatus(video.handle, status) == 0 &&
            (int64_t)status[PS5_VIDEO_OUT_FLIP_STATUS_SHOWN_ARGUMENT] >= marker) {
            if (marker >= 0 && marker <= 1200 && diagnostic_frame((unsigned)marker))
                boot_report("flip marker %lld reported shown", (long long)marker);
            return 1;
        }
        if (sceVideoOutWaitVblank(video.handle) < 0) break;
    }
    boot_report("ERROR flip %lld did not complete", (long long)marker);
    return 0;
}

void khdays_host_close(void)
{
    khdays_ps5_host_io_shutdown();
    if (video.vulkan || video.gpu != NULL || video.frame_package != NULL)
        vulkan_close();
    int safe = 1;
    if (video.handle >= 0) {
        safe = wait_marker(video.marker);
        if (safe && video.registered) {
            int result = sceVideoOutUnregisterBuffers(video.handle, 0);
            safe = result == 0 || result == PS5_VIDEO_OUT_ERROR_BUSY;
        }
        /* Closing the handle stops scan-out before releasing its allocation. */
        if (sceVideoOutClose(video.handle) != 0) safe = 0;
    }
    if (safe && video.mapping) {
        if (sceKernelMunmap(video.mapping, video.allocation_bytes) == 0) {
            sceKernelReleaseDirectMemory(video.physical, video.allocation_bytes);
            video.mapping = NULL;
        }
    }
    video.handle = -1;
    video.running = video.registered = video.headless = 0;
    video.marker = 0;
}

int khdays_host_start(void)
{
    const char *resolution = getenv("KHDAYS_PS5_VIDEO");
    if (video.running) return 1;
    if (video.mapping) {
        fprintf(stderr, "host video: previous scan-out memory still retained\n");
        return 0;
    }
    if (getenv("KHDAYS_HEADLESS")) {
        video.headless = video.running = 1;
        return 1;
    }
    /* The PS5 launch splash remains on top of VideoOut until this service is
     * called. The reference PS5 renderer hides it before opening VideoOut. */
    const int splash_result = sceSystemServiceHideSplashScreen();
    boot_report("sceSystemServiceHideSplashScreen returned %d", splash_result);
    /* The reference compositor is CPU-bound.  At 4K it has to fill about
     * 8.3M tiled pixels for every DS frame; the console log measured roughly
     * 22 ms of presenter work before the game entered its first mission.
     * Use the 1080p path by default so the normal build has a chance to meet
     * the 60 Hz display budget.  Set KHDAYS_PS5_VIDEO=4k for the diagnostic
     * 3840x2160 path.  This is an output-size choice only; it does not change
     * the game's 256x192 DS render target or emulate the game at 4x speed.
     */
    const int output_4k = resolution &&
        (!strcmp(resolution, "4k") || !strcmp(resolution, "3840p"));
    video.width = output_4k ? 3840 : 1920;
    video.height = video.width == 3840 ? 2160 : 1080;
    video.presents = 0;
    memset(video.diagnostic_next, 0, sizeof(video.diagnostic_next));
    memset(video.diagnostic_scene, 0xff, sizeof(video.diagnostic_scene));
    memset(video.scene_events, 0, sizeof(video.scene_events));
    const char *scale_env = getenv("KHDAYS_PS5_3D_SCALE");
    video.renderer_scale = 2;
    if (scale_env != NULL && *scale_env != '\0') {
        char *end = NULL;
        const unsigned long requested = strtoul(scale_env, &end, 10);
        if (end != scale_env && *end == '\0' && requested >= 1 && requested <= 4)
            video.renderer_scale = (unsigned)requested;
    }
    boot_report("BUILD 01.000.019; Vulkan 1.1; deferred GPU resource release; one-screen compositor enabled; 1080p default; 3D scale=%ux",
                video.renderer_scale);
    video.started_tsc = sceKernelReadTsc();
    video.tsc_hz = sceKernelGetTscFrequency();
    boot_report("BOOT timing ready; counter_hz=%llu", (unsigned long long)video.tsc_hz);
    video.work_ticks = video.wait_ticks = 0;
    video.render_samples = video.render_reports = 0;
    atomic_store(&layout_requested, 0);
    memset(video.render_ticks, 0, sizeof(video.render_ticks));
    for (unsigned y = 0; y < 128; ++y)
        for (unsigned x = 0; x < 128; ++x)
            tile_positions[tile_offset(x, y) / 4] = (uint16_t)((y << 7) | x);
    const char *layout = getenv("KHDAYS_PS5_LAYOUT");
    char saved_layout[32] = {0};
    if (layout == NULL) {
        const char *root = khdays_host_user_folder();
        if (root != NULL && *root) {
            char path[1200];
            int n = snprintf(path, sizeof(path), "%s/video-layout.txt", root);
            if (n > 0 && (size_t)n < sizeof(path)) {
                FILE *file = fopen(path, "r");
                if (file != NULL) { if (fscanf(file, "%31s", saved_layout) == 1) layout = saved_layout; fclose(file); }
            }
        }
    }
    unsigned mode = 0;
    for (unsigned i = 0; i < 5; ++i) if (layout && !strcmp(layout, layout_names[i])) mode = i;
    setup_layout(mode);
    boot_report("BOOT layout ready: %s", layout_names[video.layout]);

    const char *renderer = getenv("KHDAYS_PS5_RENDERER");
    if (renderer == NULL || strcmp(renderer, "cpu") != 0) {
        video.gpu = khdays_vk_device_open(video.width, video.height, vulkan_log);
        if (video.gpu != NULL) boot_report("BOOT GPU ready; opening gameplay shader pipelines");
        if (video.gpu != NULL && khdays_native_vulkan_renderer_open(video.gpu)) {
            video.frame_package = calloc(1, sizeof(*video.frame_package));
            if (video.frame_package != NULL && khdays_ps5_host_io_init()) {
                video.vulkan = video.running = 1;
                video.width = khdays_vk_width(video.gpu);
                video.height = khdays_vk_height(video.gpu);
                boot_report("VULKAN ACTIVE: RADV compute renderers + KHR_display VideoOut, %ux%u, scale=%ux",
                            video.width, video.height, video.renderer_scale);
                boot_report("LAYOUT %s; L3+R3 cycles upper/lower fullscreen, upper/lower 4:3, stacked",
                            layout_names[video.layout]);
                return 1;
            }
            if (video.frame_package == NULL)
                boot_report("Vulkan frame-package allocation failed; trying reference VideoOut");
            else
                boot_report("Vulkan input/audio initialization failed; trying reference VideoOut");
            khdays_ps5_host_io_shutdown();
        }
        if (!video.vulkan) {
            char reason[512];
            snprintf(reason, sizeof(reason), "%s", khdays_vk_error());
            boot_report("Vulkan unavailable: %s; falling back to reference VideoOut",
                        reason[0] != '\0' ? reason : "renderer initialization failed");
            vulkan_close();
        }
    }

    const size_t tiled_bytes = ((video.width + 127u) >> 7) *
                              ((video.height + 127u) >> 7) * (size_t)0x10000;
    video.frame_bytes = (tiled_bytes + FRAME_ALIGNMENT - 1) & ~(FRAME_ALIGNMENT - 1);
    video.allocation_bytes = 2 * video.frame_bytes;
    video.next = 0;
    video.handle = sceVideoOutOpen(PS5_VIDEO_OUT_USER_SYSTEM, 0, 0, NULL);
    if (video.handle < 0) goto failed;
    if (sceKernelAllocateDirectMemory(0, sceKernelGetDirectMemorySize(),
            video.allocation_bytes, FRAME_ALIGNMENT, MEMORY_TYPE_WC_GARLIC,
            &video.physical) < 0) goto failed;
    void *mapped = NULL;
    if (sceKernelMapDirectMemory(&mapped, video.allocation_bytes,
            PS5_KERNEL_PROT_CPU_READ | PS5_KERNEL_PROT_CPU_WRITE |
            PS5_KERNEL_PROT_GPU_READ | PS5_KERNEL_PROT_GPU_WRITE,
            0, video.physical, FRAME_ALIGNMENT) < 0) {
        sceKernelReleaseDirectMemory(video.physical, video.allocation_bytes);
        goto failed;
    }
    video.mapping = mapped;
    memset(mapped, 0, video.allocation_bytes);
    struct ps5_video_out_buffer buffers[2] = {
        {.data = mapped}, {.data = video.mapping + video.frame_bytes}
    };
    unsigned char attribute[PS5_VIDEO_OUT_ATTRIBUTE_BYTES] = {0};
    sceVideoOutSetBufferAttribute2(attribute, KHDAYS_VIDEO_ABGR8888,
        PS5_VIDEO_OUT_TILING_64KB_R_X, video.width, video.height, 0, 0, 0);
    if (sceVideoOutSetFlipRate(video.handle, 0) < 0 ||
        sceVideoOutRegisterBuffers2(video.handle, 0, 0, buffers, 2, attribute, 0, NULL) < 0)
        goto failed;
    video.registered = 1;
    if (!khdays_ps5_host_io_init()) goto failed;
    video.running = 1;
    boot_report("VideoOut ready at %ux%u; submitting reference DS screens (%s profile)",
                video.width, video.height, output_4k ? "4K" : "1080p performance");
    boot_report("LAYOUT %s; L3+R3 cycles upper/lower fullscreen, upper/lower 4:3, stacked", layout_names[video.layout]);
    return 1;
failed:
    fprintf(stderr, "host video: PS5 host initialization failed\n");
    khdays_host_close();
    return 0;
}

void khdays_host_present(const uint32_t *upper, const uint32_t *lower)
{
    if (!video.running || video.headless) return;
    const uint64_t began = sceKernelReadTsc();
    khdays_ps5_host_io_poll();
    apply_layout_request();
    if (!wait_marker(video.marker)) abort();
    const uint64_t waited = sceKernelReadTsc();
    unsigned char *out = video.mapping + video.next * video.frame_bytes;
    if (video.clear_pending[video.next]) {
        memset(out, 0, video.frame_bytes);
        video.clear_pending[video.next] = 0;
    }
    /* Convert at DS size, not once per pixel of the 4K enlargement. */
    for (unsigned i = 0; i < 256u * 192u; ++i) {
        scanout_screens[0][i] = to_scanout(upper[i]);
        scanout_screens[1][i] = to_scanout(lower[i]);
    }
    const unsigned width = video.layout == 4 ? video.height * 2u / 3u :
                           video.layout >= 2 ? video.height * 4u / 3u : video.width;
    const unsigned left = (video.width - width) / 2u;
    const unsigned pitch = (video.width + 127u) / 128u;
    for (unsigned ty = 0; ty < (video.height + 127u) / 128u; ++ty) {
        for (unsigned tx = left / 128u; tx <= (left + width - 1u) / 128u; ++tx) {
            uint32_t *tile = (uint32_t *)(out + ((size_t)ty * pitch + tx) * 0x10000u);
            for (unsigned p = 0; p < 128u * 128u; p += 4) {
                uint32_t pixels[4];
                for (unsigned n = 0; n < 4; ++n) {
                    unsigned xy = tile_positions[p + n];
                    pixels[n] = scanout_at(tx * 128u + (xy & 127u), ty * 128u + (xy >> 7));
                }
                _mm_stream_si128((__m128i *)(tile + p), _mm_setr_epi32(pixels[0], pixels[1], pixels[2], pixels[3]));
            }
        }
    }
    /* WC_GARLIC + non-temporal stores: drain WC buffers once. Do not walk
     * megabytes of WC memory with CLFLUSH on every presentation. */
    _mm_sfence();
    const int submitted = sceVideoOutSubmitFlip(video.handle, (int)video.next,
            PS5_VIDEO_OUT_FLIP_VSYNC, ++video.marker);
    if (submitted < 0) {
        boot_report("ERROR submit flip failed at present %u code=%08x", video.presents + 1, (unsigned)submitted);
        abort();
    }
    ++video.presents;
    const uint64_t ended = sceKernelReadTsc();
    video.work_ticks += ended - waited;
    video.wait_ticks += waited - began;
    if (diagnostic_frame(video.presents)) {
        unsigned nonblack_upper = 0, nonblack_lower = 0;
        for (unsigned i = 0; i < 256u * 192u; ++i) {
            nonblack_upper += (upper[i] & 0x00ffffffu) != 0;
            nonblack_lower += (lower[i] & 0x00ffffffu) != 0;
        }
        boot_report("frame submit #%u %ux%u marker=%lld rgb_pixels=%u/%u center=%08x/%08x",
                    video.presents, video.width, video.height, (long long)video.marker,
                    nonblack_upper, nonblack_lower, upper[96u * 256u + 128u],
                    lower[96u * 256u + 128u]);
        if (video.tsc_hz != 0) {
            boot_report("PERF presents=%u elapsed_ms=%.1f work_us=%.1f wait_us=%.1f avg_work_us=%.1f avg_wait_us=%.1f",
                video.presents, (double)(ended - video.started_tsc) * 1000.0 / video.tsc_hz,
                (double)(ended - waited) * 1000000.0 / video.tsc_hz,
                (double)(waited - began) * 1000000.0 / video.tsc_hz,
                (double)video.work_ticks * 1000000.0 / video.tsc_hz / video.presents,
                (double)video.wait_ticks * 1000000.0 / video.tsc_hz / video.presents);
        }
    }
    video.next ^= 1u;
}

/* The upstream reference renderer produces the original complete DS frame.
 * A Vulkan package renderer will request additional geometry explicitly. */
int khdays_host_frame_wants(void)
{
    return video.vulkan ? KHDAYS_WANT_3D | KHDAYS_WANT_COMPOSITION | KHDAYS_WANT_SOURCES |
                          KHDAYS_WANT_2D | KHDAYS_WANT_SITUATION : 0;
}
float khdays_host_frame_widen(void) { return 1.0f; }
int khdays_host_frame_scale(void) { return video.vulkan ? (int)video.renderer_scale : 1; }
int khdays_host_graphics_flags(void)
{
    /* Ask the upstream game/UI logic for its native one-screen compositions
     * only when the GPU package renderer is active. The reference CPU fallback
     * still presents the two original DS screens. */
    return video.vulkan ? KHDAYS_HOST_ONE_SCREEN : 0;
}
KhdaysFramePackage *khdays_host_frame_begin(void) { return video.vulkan ? video.frame_package : NULL; }
void khdays_host_frame_publish(KhdaysFramePackage *p)
{
    if (video.vulkan) {
        KhdaysNativeVulkanFrame frame;
        khdays_ps5_host_io_poll();
        apply_layout_request();
        if (!khdays_native_vulkan_renderer_draw(p, (int)video.renderer_scale, &frame)) {
            boot_report("ERROR Vulkan frame render failed: %s", SDL_GetError());
            abort();
        }
        const unsigned present_layout = frame.output_layout != 0 ? frame.output_layout : video.layout;
        if (!khdays_vk_present(video.gpu, frame.upper, frame.lower, present_layout)) {
            boot_report("ERROR Vulkan VideoOut present failed: %s", khdays_vk_error());
            abort();
        }
        ++video.presents;
        if (diagnostic_frame(video.presents)) {
            boot_report("VULKAN package accepted: frame=%u wants=0x%02x upper=%u display3d=%u payload3d=%u",
                        p->frame, p->wants, p->upper, p->info[0].is3d,
                        (p->wants & KHDAYS_WANT_3D) != 0);
            boot_report("VULKAN frame=%u game_frame=%u size=%ux%u/%ux%u layout=%s scale=%ux",
                        video.presents, frame.frame, frame.upper_width, frame.upper_height,
                        frame.lower_width, frame.lower_height,
                        frame.output_layout == 5 ? "one-screen" :
                        frame.output_layout == 6 ? "one-screen-stacked" :
                        frame.output_layout == 7 ? "one-screen-side-by-side" : layout_names[video.layout],
                        video.renderer_scale);
        }
        return;
    }
    if (p) khdays_host_present(p->screens[p->upper], p->screens[!p->upper]);
}

void khdays_host_tell_no_data(const char *where, const char *broken)
{
    fprintf(stderr, "host: game data at %s unavailable: %s\n", where,
            broken ? broken : "no extracted game found");
}
