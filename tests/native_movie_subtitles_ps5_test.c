/* Real PNG decoding and production subtitle parser/compositor, exercised
 * against small deterministic assets. Locks are single-thread test doubles. */
#define _CRT_SECURE_NO_WARNINGS
#include "../native/ps5/movie_subtitles_ps5.c"

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wunused-but-set-variable"
#endif
#define STBI_ONLY_PNG
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

static int failures, mod_calls;
static const char *mod_schedule;

uint8_t *khdays_png_read(const char *path, int *width, int *height, const char **error)
{
    int channels;
    uint8_t *result = stbi_load(path, width, height, &channels, 4);
    if (result == NULL && error != NULL) { *error = stbi_failure_reason(); }
    return result;
}
void khdays_png_free(uint8_t *pixels) { stbi_image_free(pixels); }
int khdays_mods_find(const char *relative, char *path, int size)
{
    ++mod_calls;
    if (strcmp(relative, "movies/subtitles/en/hd802.txt") != 0 || mod_schedule == NULL) { return 0; }
    const int length = snprintf(path, (size_t)size, "%s", mod_schedule);
    return length > 0 && length < size;
}

static void check(int condition, const char *what)
{
    if (!condition) { fprintf(stderr, "FAIL: %s\n", what); ++failures; }
}
static int schedule(const char *path, const char *lines)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) { return 0; }
    const size_t bytes = strlen(lines);
    const size_t written = fwrite(lines, 1, bytes, f);
    return fclose(f) == 0 && written == bytes;
}
static void black(uint8_t *pixels, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        pixels[i * 4u] = pixels[i * 4u + 1u] = pixels[i * 4u + 2u] = 0;
        pixels[i * 4u + 3u] = 255;
    }
}

int main(int argc, char **argv)
{
    static const uint8_t atlas[] = {255, 0, 0, 255, 0, 255, 0, 128,
                                   0, 0, 255, 255, 255, 255, 255, 0};
    char path[1024], png[1024], why[256];
    uint8_t guarded[32], frame[16];
    if (argc != 2) { fprintf(stderr, "usage: native_movie_subtitles_ps5_test TEMP_DIRECTORY\n"); return 2; }
    snprintf(path, sizeof(path), "%s/subtitles.txt", argv[1]);
    snprintf(png, sizeof(png), "%s/atlas.png", argv[1]);
    check(stbi_write_png(png, 2, 2, 4, atlas, 8), "write actual PNG fixture");
    check(schedule(path, "# theater format\npicture 2 2\nrate 30\natlas atlas.png\nline 0 2 0 0 0 0 2 2\n"),
          "write subtitle schedule");
    check(khdays_ps5_movie_subtitles_load_file(path, why, sizeof(why)), "parse schedule and decode PNG atlas");
    black(frame, 4);
    check(khdays_ps5_movie_subtitles_draw_rgba(frame, sizeof(frame), 8, 2, 2, 0.0, 30.0) == 1,
          "draw active subtitle");
    check(frame[0] == 255 && frame[1] == 0 && frame[2] == 0 && frame[3] == 255, "opaque red source");
    check(frame[4] == 0 && frame[5] == 128 && frame[6] == 0 && frame[7] == 255, "half-alpha green over opaque black");
    check(frame[8] == 0 && frame[9] == 0 && frame[10] == 255, "opaque blue source");
    check(frame[12] == 0 && frame[13] == 0 && frame[14] == 0 && frame[15] == 255, "transparent texel preserves frame");
    black(frame, 4);
    check(khdays_ps5_movie_subtitles_draw_rgba(frame, sizeof(frame), 8, 2, 2, 2.0 / 30.0, 30.0) == 0 && frame[0] == 0,
          "END frame is exclusive");
    check(khdays_ps5_movie_subtitles_draw_rgba(frame, sizeof(frame), 8, 2, 2, 1.49 / (30000.0 / 1001.0),
                                              30000.0 / 1001.0) == 1, "NTSC frame-count timing");
    check(khdays_ps5_movie_subtitles_draw_rgba(frame, sizeof(frame) - 1u, 8, 2, 2, 0.0, 30.0) == -1,
          "reject undersized destination");
    check(khdays_ps5_movie_subtitles_draw_rgba(frame, sizeof(frame), 8, 2, 2, -1.0, 30.0) == -1,
          "reject negative time");
    uint64_t nan_bits = UINT64_C(0x7ff8000000000000);
    double nan;
    memcpy(&nan, &nan_bits, sizeof(nan));
    check(khdays_ps5_movie_subtitles_draw_rgba(frame, sizeof(frame), 8, 2, 2, nan, 30.0) == -1,
          "reject NaN time");

    check(schedule(path, "picture 2 2\nrate 30\natlas atlas.png\nline 0 2 -1 0 0 0 2 2\n"), "write clipping fixture");
    check(khdays_ps5_movie_subtitles_load_file(path, why, sizeof(why)), "load clipped subtitle");
    memset(guarded, 0xa5, sizeof(guarded));
    black(guarded + 8, 4);
    check(khdays_ps5_movie_subtitles_draw_rgba(guarded + 8, 16, 8, 2, 2, 0.0, 30.0) == 1,
          "negative destination clips at frame edge");
    check(guarded[9] == 128 && guarded[13] == 0 && guarded[17] == 0, "clipped column samples correct atlas pixels");
    for (size_t i = 0; i < 8; ++i) {
        check(guarded[i] == 0xa5 && guarded[24 + i] == 0xa5, "clipping preserves both canaries");
    }

    static const char *const invalid[] = {
        "picture 2 2\nrate 0\natlas atlas.png\nline 0 2 0 0 0 0 2 2\n",
        "picture 99999999999999999999 2\natlas atlas.png\nline 0 2 0 0 0 0 2 2\n",
        "picture 2 2\natlas ../atlas.png\nline 0 2 0 0 0 0 2 2\n",
        "picture 2 2\natlas atlas.png\nline 0 2 0 0 1 0 2 2\n",
        "picture 2 2\natlas atlas.png\nline 2 2 0 0 0 0 2 2\n"
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        check(schedule(path, invalid[i]), "write malformed fixture");
        check(!khdays_ps5_movie_subtitles_load_file(path, why, sizeof(why)) && why[0] != '\0',
              "reject malformed schedule with a reason");
        check(khdays_ps5_movie_subtitles_draw_rgba(frame, sizeof(frame), 8, 2, 2, 0.0, 30.0) == 0,
              "failed load clears previous movie");
    }
    check(schedule(path, "picture 2 2\nrate 30\natlas atlas.png\nline 0 2 0 0 0 0 2 2\n"), "restore valid fixture");
    mod_schedule = path;
    khdays_movie_subtitles_load("802", 1);
    black(frame, 4);
    check(mod_calls == 1 && khdays_ps5_movie_subtitles_draw_rgba(frame, sizeof(frame), 8, 2, 2, 0.0, 30.0) == 1,
          "runtime load resolves correct language/movie through mods");
    khdays_movie_subtitles_release();
    check(khdays_ps5_movie_subtitles_draw_rgba(frame, sizeof(frame), 8, 2, 2, 0.0, 30.0) == 0,
          "release frees loaded schedule and atlas");
    printf("Subtitle PNG/parser/timing/clipping/blending validation: %d failures\n", failures);
    return failures != 0;
}
