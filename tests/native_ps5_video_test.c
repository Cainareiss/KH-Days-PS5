/* Test the production VideoOut presenter against the public SDK declarations.
 * Device calls are mocked; no console display success is claimed by this test. */
#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *test_resolution;
static const char *test_layout = "stacked";
static const char *test_log_root;
static int test_headless;
static jmp_buf test_abort_target;
static char *test_getenv(const char *name)
{
    if (!strcmp(name, "KHDAYS_PS5_VIDEO")) return (char *)test_resolution;
    if (!strcmp(name, "KHDAYS_PS5_LAYOUT")) return (char *)test_layout;
    if (!strcmp(name, "KHDAYS_HEADLESS")) return test_headless ? "1" : NULL;
    return NULL;
}
static _Noreturn void test_abort(void) { longjmp(test_abort_target, 1); }
#define getenv test_getenv
#define abort test_abort
#include "../native/ps5/host_video_ps5.c"
#undef getenv
#undef abort

static int opens, closes, allocations, releases, unmaps, registers, unregisters;
static int io_starts, io_stops, polls, submits, vblanks;
static int open_fail, allocation_fail, map_fail, register_fail, flip_fail, wait_fail, close_fail;
static unsigned char *mapped_memory;
static size_t mapped_bytes;
static int64_t shown_marker;
static unsigned attribute_width, attribute_height;

void platform_init(const char *name) { assert(name != NULL); }
const char *khdays_host_user_folder(void) { return test_log_root; }
int sceSystemServiceHideSplashScreen(void) { return 0; }
uint64_t sceKernelReadTsc(void) { static uint64_t ticks; return ticks += 1000; }
uint64_t sceKernelGetTscFrequency(void) { return 1000000; }
int khdays_ps5_host_io_init(void) { ++io_starts; return 1; }
void khdays_ps5_host_io_shutdown(void) { ++io_stops; }
void khdays_ps5_host_io_poll(void) { ++polls; }
int sceVideoOutOpen(int32_t user, int32_t bus, int32_t index, const void *param)
{
    assert(user == 255 && bus == 0 && index == 0 && param == NULL);
    ++opens;
    return open_fail ? -1 : 42;
}
int sceVideoOutClose(int32_t handle) { assert(handle == 42); ++closes; return close_fail ? -1 : 0; }
int64_t sceKernelGetDirectMemorySize(void) { return INT64_C(12) * 1024 * 1024 * 1024; }
int32_t sceKernelAllocateDirectMemory(int64_t first, int64_t last, size_t size,
                                    size_t alignment, int type, int64_t *physical)
{
    assert(first == 0 && last > 0 && size > 0 && alignment == 0x200000 && type == 3);
    ++allocations;
    *physical = 0x10000000;
    return allocation_fail ? -1 : 0;
}
int32_t sceKernelReleaseDirectMemory(int64_t first, size_t size)
{
    assert(first == 0x10000000 && size > 0);
    ++releases;
    return 0;
}
int32_t sceKernelMapDirectMemory(void **out, size_t size, int prot, int flags,
                               int64_t first, size_t alignment)
{
    assert(prot == 0x33 && flags == 0 && first == 0x10000000 && alignment == 0x200000);
    if (map_fail) return -1;
    mapped_memory = malloc(size + 64);
    assert(mapped_memory != NULL);
    memset(mapped_memory, 0xa5, size + 64);
    mapped_bytes = size;
    *out = mapped_memory;
    return 0;
}
int32_t sceKernelMunmap(void *address, size_t size)
{
    assert(address == mapped_memory && size == mapped_bytes);
    for (unsigned i = 0; i < 64; ++i) assert(mapped_memory[size + i] == 0xa5);
    free(mapped_memory);
    mapped_memory = NULL;
    ++unmaps;
    return 0;
}
void sceVideoOutSetBufferAttribute2(void *attribute, uint64_t format, uint32_t tiling,
                                  uint32_t width, uint32_t height, uint64_t a, uint32_t b, uint64_t c)
{
    assert(attribute && format == UINT64_C(0x8000000022000000) && tiling == 0);
    assert(a == 0 && b == 0 && c == 0);
    attribute_width = width;
    attribute_height = height;
}
int sceVideoOutSetFlipRate(int32_t handle, int32_t rate)
{
    assert(handle == 42 && rate == 0);
    return 0;
}
int sceVideoOutRegisterBuffers2(int32_t handle, int32_t set, int32_t first,
                               struct ps5_video_out_buffer *buffers, int32_t count,
                               void *attribute, int32_t reserved, void *option)
{
    assert(handle == 42 && set == 0 && first == 0 && count == 2);
    assert(buffers[0].data == mapped_memory && buffers[1].data == mapped_memory + video.frame_bytes);
    assert(attribute && reserved == 0 && option == NULL);
    ++registers;
    return register_fail ? -1 : 0;
}
int sceVideoOutUnregisterBuffers(int32_t handle, int32_t set)
{
    assert(handle == 42 && set == 0);
    ++unregisters;
    return 0;
}
int sceVideoOutSubmitFlip(int32_t handle, int32_t index, uint32_t mode, int64_t marker)
{
    assert(handle == 42 && (index == 0 || index == 1) && mode == PS5_VIDEO_OUT_FLIP_VSYNC);
    ++submits;
    if (flip_fail) return -1;
    shown_marker = marker;
    return 0;
}
int sceVideoOutGetFlipStatus(int32_t handle, uint64_t status[PS5_VIDEO_OUT_FLIP_STATUS_WORDS])
{
    assert(handle == 42);
    status[PS5_VIDEO_OUT_FLIP_STATUS_SHOWN_ARGUMENT] = wait_fail ? 0 : (uint64_t)shown_marker;
    return 0;
}
int sceVideoOutWaitVblank(int32_t handle) { assert(handle == 42); ++vblanks; return wait_fail ? -1 : 0; }

static uint32_t upper[256 * 192], lower[256 * 192];

static void test_present(void)
{
    /* Keep the tile-boundary assertions on the explicit diagnostic 4K path;
     * the production default is now the 1080p performance path. */
    test_resolution = "4k";
    test_layout = "stacked";
    assert(khdays_host_start());
    assert(video.width == 3840 && video.height == 2160);
    assert(attribute_width == 3840 && attribute_height == 2160);
    assert(khdays_host_start() && opens == 1 && io_starts == 1);
    unsigned char seen[16384] = {0};
    for (unsigned y = 0; y < 128; ++y) {
        for (unsigned x = 0; x < 128; ++x) {
            size_t offset = tile_offset(x, y);
            assert((offset & 3) == 0 && offset < 65536 && !seen[offset / 4]);
            seen[offset / 4] = 1;
        }
    }
    for (unsigned y = 0; y < video.height; ++y)
        for (unsigned x = 0; x < video.width; ++x)
            assert(tile_offset(x, y) + 4 <= video.frame_bytes);
    for (unsigned i = 0; i < 256 * 192; ++i) { upper[i] = 0x123456; lower[i] = 0x654321; }
    khdays_host_present(upper, lower);
    assert(submits == 1 && polls == 1 && video.next == 1 && video.marker == 1);
    assert(*(uint32_t *)(mapped_memory + tile_offset(1200, 0)) == 0xff563412);
    assert(*(uint32_t *)(mapped_memory + tile_offset(1200, 2159)) == 0xff214365);
    assert(*(uint32_t *)(mapped_memory + tile_offset(1199, 0)) == 0);
    assert(*(uint32_t *)(mapped_memory + tile_offset(2640, 0)) == 0);
    khdays_host_present(upper, lower);
    assert(submits == 2 && video.next == 0 && video.marker == 2);
    khdays_host_close();
    assert(!video.mapping && releases == 1 && unmaps == 1 && unregisters == 1 && closes == 1);
    assert(allocations == 1 && registers == 1 && io_stops == 1);
    khdays_host_close();
    assert(releases == 1 && closes == 1);
    test_resolution = NULL;
}

static void test_failures(void)
{
    int previous = releases;
    open_fail = 1;
    assert(!khdays_host_start() && !video.running);
    assert(releases == previous);
    open_fail = 0;
    allocation_fail = 1;
    assert(!khdays_host_start() && !video.mapping);
    assert(releases == previous);
    allocation_fail = 0;
    map_fail = 1;
    assert(!khdays_host_start() && !video.mapping);
    assert(releases == ++previous);
    map_fail = 0;
    register_fail = 1;
    assert(!khdays_host_start() && !video.mapping);
    assert(releases == ++previous);
    register_fail = 0;
    test_resolution = "4k";
    assert(khdays_host_start() && video.width == 3840 && video.height == 2160);
    khdays_host_close();
    assert(releases == ++previous);
    test_resolution = "1080p";
    assert(khdays_host_start() && video.width == 1920 && video.height == 1080);
    flip_fail = 1;
    if (setjmp(test_abort_target) == 0) {
        khdays_host_present(upper, lower);
        assert(!"a failed flip must stop presenting");
    }
    flip_fail = 0;
    shown_marker = video.marker;
    khdays_host_close();
    assert(releases == ++previous);

    assert(khdays_host_start());
    video.marker = 100;
    wait_fail = 1;
    if (setjmp(test_abort_target) == 0) {
        khdays_host_present(upper, lower);
        assert(!"an uncompleted flip must stop presenting");
    }
    wait_fail = 0;
    shown_marker = 100;
    khdays_host_close();
    assert(releases == ++previous);
    assert(vblanks > 0);
}

static void test_headless_mode(void)
{
    const int before = opens;
    test_headless = 1;
    assert(khdays_host_start() && video.headless);
    khdays_host_present(upper, lower);
    assert(opens == before);
    khdays_host_close();
}

static void test_patterned_scaling(void)
{
    for (int mode = 0; mode < 2; ++mode) {
      for (unsigned layout = 0; layout < 5; ++layout) {
        test_resolution = mode ? "1080p" : NULL;
        test_layout = layout_names[layout];
        assert(khdays_host_start());
        for (unsigned i = 0; i < 256u * 192u; ++i) {
            upper[i] = (i * 4051u + 0x12345u) & 0x00ffffffu;
            lower[i] = (i * 7919u + 0x654321u) & 0x00ffffffu;
        }
        khdays_host_present(upper, lower);
        for (unsigned y = 0; y < video.height; ++y) {
            for (unsigned x = 0; x < video.width; ++x) {
                uint32_t expected = 0;
                unsigned sx = 0, sy = 0, screen = 0, drawn = 0;
                if (layout == 4) {
                    unsigned width = video.height * 2u / 3u, left = (video.width - width) / 2u;
                    if (x >= left && x < left + width) {
                        unsigned row = y * 384u / video.height;
                        sx = (x - left) * 256u / width; sy = row % 192u;
                        screen = row >= 192u; drawn = 1;
                    }
                } else {
                    unsigned width = layout >= 2 ? video.height * 4u / 3u : video.width;
                    unsigned left = (video.width - width) / 2u;
                    if (x >= left && x < left + width) {
                        sx = (x - left) * 256u / width; sy = y * 192u / video.height;
                        screen = layout & 1u; drawn = 1;
                    }
                    unsigned pip_w = video.width / 4u, pip_h = pip_w * 3u / 4u, margin = video.width / 80u;
                    unsigned pip_x = left + width - pip_w - margin;
                    if (x >= pip_x && x < pip_x + pip_w && y >= margin && y < margin + pip_h) {
                        sx = (x - pip_x) * 256u / pip_w; sy = (y - margin) * 192u / pip_h;
                        screen = !(layout & 1u); drawn = 1;
                    }
                }
                if (drawn) {
                    const uint32_t *source = screen ? lower : upper;
                    uint32_t color = source[sy * 256u + sx];
                    expected = 0xff000000u | (color & 0xff00u) | (color >> 16) | ((color & 0xffu) << 16);
                }
                assert(*(uint32_t *)(mapped_memory + tile_offset(x, y)) == expected);
            }
        }
        khdays_host_close();
      }
    }
    test_layout = "stacked";
}

static void test_color_and_layout_switch(void)
{
    assert(to_scanout(0xff0000) == 0xff0000ff); /* red byte FIRST */
    assert(to_scanout(0x00ff00) == 0xff00ff00);
    assert(to_scanout(0x0000ff) == 0xffff0000);
    assert(to_scanout(0x808080) == 0xff808080);
    test_resolution = "1080p"; test_layout = "upper";
    assert(khdays_host_start() && video.layout == 0);
    for (unsigned n = 0; n < 256 * 192; ++n) { upper[n] = 0xff0000; lower[n] = 0x0000ff; }
    khdays_host_present(upper, lower);
    assert(*(uint32_t *)(mapped_memory + tile_offset(0, 0)) == 0xff0000ff);
    khdays_ps5_video_cycle_layout();
    assert(video.layout == 0); /* only a request: other threads do not touch views */
    khdays_host_present(upper, lower);
    assert(video.layout == 1);
    khdays_ps5_video_cycle_layout(); khdays_host_present(upper, lower);
    assert(video.layout == 2); /* preserve the DS's original aspect if preferred */
    khdays_ps5_video_cycle_layout(); khdays_host_present(upper, lower);
    assert(video.layout == 3);
    khdays_ps5_video_cycle_layout(); khdays_host_present(upper, lower);
    assert(video.layout == 4);
    /* Both reused buffers must clear content outside the new viewport. */
    assert(*(uint32_t *)(mapped_memory + tile_offset(0, 0)) == 0);
    khdays_host_present(upper, lower);
    assert(*(uint32_t *)(mapped_memory + video.frame_bytes + tile_offset(0, 0)) == 0);
    khdays_ps5_video_cycle_layout(); khdays_host_present(upper, lower);
    assert(video.layout == 0);
    khdays_host_close(); test_layout = "stacked";
}

static void test_layout_preference(const char *directory)
{
    char path[1200], saved[32];
    snprintf(path, sizeof(path), "%s/video-layout.txt", directory);
    FILE *file = fopen(path, "w"); assert(file);
    fputs("upper-fit\n", file); assert(!fclose(file));
    test_log_root = directory; test_layout = NULL;
    assert(khdays_host_start() && video.layout == 2);
    khdays_ps5_video_cycle_layout(); khdays_host_present(upper, lower);
    assert(video.layout == 3);
    file = fopen(path, "r"); assert(file);
    assert(fscanf(file, "%31s", saved) == 1 && !strcmp(saved, "lower-fit"));
    assert(!fclose(file)); khdays_host_close();
    assert(khdays_host_start() && video.layout == 3);
    khdays_host_close();
    test_log_root = NULL; test_layout = "stacked";
}

static void test_bounded_diagnostics(const char *directory)
{
    static uint8_t io[0x1100], palette[0x800], oam[0x800];
    static uint32_t layer3d[256 * 192];
    KhdaysVramPages pages = {0};
    KhdaysG3dList list = {0};
    list.npolys = 1; list.nverts = 3; list.flushes = 1;
    list.npre = 1; list.npre_verts = 3;
    palette[0] = 0xff; palette[1] = 0x7f;
    layer3d[0] = 0x1f00003f;
    test_log_root = directory;
    memset(video.diagnostic_next, 0, sizeof(video.diagnostic_next));
    for (unsigned frame = 0; frame < 2401; ++frame) {
        khdays_ps5_video_report_ds(frame, "composed", 1, KHDAYS_WANT_2D, 1, 7,
                                   io, palette, oam, layer3d, &pages, &list, upper, lower);
        khdays_ps5_video_report_ds(frame, "3d-render", -1, KHDAYS_WANT_2D, 1, 7,
                                   io, palette, oam, layer3d, &pages, &list, NULL, NULL);
        khdays_ps5_video_report_ds(frame, "raw2d", 1, KHDAYS_WANT_2D, 1, 7,
                                   io, palette, oam, layer3d, &pages, &list, upper, lower);
    }
    assert(video.diagnostic_next[0] == DIAGNOSTIC_POINTS);
    assert(video.diagnostic_next[1] == DIAGNOSTIC_POINTS);
    assert(video.diagnostic_next[2] == DIAGNOSTIC_POINTS);
    for (unsigned scene = 8; scene <= 40; ++scene) {
        khdays_ps5_video_report_ds(2500 + scene, "composed", 1, KHDAYS_WANT_2D, 1, scene,
                                   io, palette, oam, layer3d, &pages, &list, upper, lower);
        khdays_ps5_video_report_ds(2500 + scene, "raw2d", 1, KHDAYS_WANT_2D, 1, scene,
                                   io, palette, oam, layer3d, &pages, &list, upper, lower);
        khdays_ps5_video_report_ds(2500 + scene, "3d-render", -1, KHDAYS_WANT_2D, 1, scene,
                                   io, palette, oam, layer3d, &pages, &list, NULL, NULL);
    }
    video.render_samples = video.render_reports = 0;
    memset(video.render_ticks, 0, sizeof(video.render_ticks));
    khdays_ps5_video_report_render(3000, 2, 0, 0, 1000000, 3000000, 4000000, 9000000);
    assert(video.render_samples == 0); /* ignore loading/2D, not a misleading FPS estimate */
    for (unsigned frame = 3000; frame < 6000; ++frame)
        khdays_ps5_video_report_render(frame, 2, 64, 0, 1000000, 3000000, 4000000, 9000000);
    assert(video.render_samples == 600 && video.render_reports == 6);
    char path[1200], line[1800];
    snprintf(path, sizeof(path), "%s/boot.log", directory);
    FILE *log = fopen(path, "r");
    assert(log != NULL);
    unsigned captures = 0, complete = 0, valid_decisions = 0, timings = 0;
    while (fgets(line, sizeof(line), log) != NULL) {
        if (strstr(line, "PIPE stage=")) {
            ++captures;
            assert(strstr(line, "G3D=1P/3V/1F PRE=1/3") != NULL);
            assert(strstr(line, "PIX3D=1/1") != NULL);
            assert(strstr(line, "HASH=") != NULL); /* full line, no truncation */
        }
        if (strstr(line, "REF=1 WANT=08 HOST=00 UI=1")) ++valid_decisions;
        if (strstr(line, "PIPE_DONE")) ++complete;
        if (strstr(line, "RENDER ds=")) {
            ++timings;
            assert(strstr(line, "avg_ms A=1.000 B_UI=2.000 present_capture=1.000 CPU3D=5.000"));
        }
    }
    assert(fclose(log) == 0);
    assert(captures == 45 && complete == 3 && valid_decisions == 30);
    assert(timings == 6);
    test_log_root = NULL;
    /* A merged VBlank must sample once at/after a checkpoint, not miss it or
     * generate a delayed backlog of snapshots. */
    memset(video.diagnostic_next, 0, sizeof(video.diagnostic_next));
    unsigned checkpoint;
    assert(diagnostic_take(0, 0, &checkpoint) && checkpoint == 0);
    assert(diagnostic_take(5, 0, &checkpoint) && checkpoint == 3);
    assert(!diagnostic_take(5, 0, &checkpoint));
    assert(diagnostic_take(121, 0, &checkpoint) && checkpoint == 120);
    assert(diagnostic_take(1400, 0, &checkpoint) && checkpoint == 1200);
    assert(!diagnostic_take(1800, 0, &checkpoint));
}

int main(int argc, char **argv)
{
    test_present();
    test_failures();
    test_patterned_scaling();
    test_color_and_layout_switch();
    test_headless_mode();
    assert(argc == 2);
    test_headless = 0;
    test_layout_preference(argv[1]);
    test_bounded_diagnostics(argv[1]);
    puts("native PS5 video: PASS (tile bounds, screen pixels, lifecycle, failures, bounded diagnostics)");
    return 0;
}
