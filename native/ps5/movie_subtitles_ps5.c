/* PS5 counterpart of host/movies/movie_subtitles.c's real schedule reader.
 * It retains theater frame timing and PNG atlases, replacing only the SDL
 * texture/render operations with a bounded RGBA8 CPU compositor. This is an
 * optional HD-movie overlay; the DS/MobiClip subtitles are untouched. */
#include "movie_subtitles_ps5.h"
#include "host/render/png_io.h"
#include "rom.h"

#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PATH_ROOM 1024u
#define MAX_LINES 32768u
#define MAX_DIMENSION 16384
#define MAX_ATLAS_PIXELS (64u * 1024u * 1024u)

typedef struct SubtitleLine {
    int first, end, x, y, u, v, w, h;
} SubtitleLine;

typedef struct Subtitles {
    int picture_w, picture_h, rate;
    SubtitleLine *lines;
    size_t count;
    uint8_t *pixels;
    int atlas_w, atlas_h;
} Subtitles;

static pthread_mutex_t subtitles_lock = PTHREAD_MUTEX_INITIALIZER;
static Subtitles loaded;

static int finite_double(double value)
{
    return value >= -DBL_MAX && value <= DBL_MAX;
}

static void drop(Subtitles *s)
{
    free(s->lines);
    if (s->pixels != NULL) {
        khdays_png_free(s->pixels);
    }
    memset(s, 0, sizeof(*s));
}

static void explain(char *why, size_t size, const char *message)
{
    if (why != NULL && size != 0) {
        (void)snprintf(why, size, "%s", message ? message : "");
    }
}

static const char *space(const char *at)
{
    while (*at != '\0' && isspace((unsigned char)*at)) { ++at; }
    return at;
}

static int numbers(const char *at, int *values, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        char *end;
        long value;
        at = space(at);
        errno = 0;
        value = strtol(at, &end, 10);
        if (at == end || errno == ERANGE || value < INT_MIN || value > INT_MAX) {
            return 0;
        }
        values[i] = (int)value;
        at = end;
    }
    at = space(at);
    return *at == '\0' || *at == '#';
}

static int relative_atlas(const char *name)
{
    const char *part = name;
    if (*part == '\0' || *part == '/') { return 0; }
    while (*part != '\0') {
        const char *end = part;
        while (*end != '\0' && *end != '/') {
            if (*end == '\\' || *end == ':') { return 0; }
            ++end;
        }
        if (part == end || (end - part == 1 && part[0] == '.') ||
            (end - part == 2 && part[0] == '.' && part[1] == '.')) { return 0; }
        if (*end == '\0') { return 1; }
        part = end + 1;
    }
    return 0;
}

static int take_atlas(const char *at, char *name, size_t room)
{
    const char *end;
    size_t length;
    at = space(at);
    end = at;
    while (*end != '\0' && !isspace((unsigned char)*end)) { ++end; }
    length = (size_t)(end - at);
    if (length == 0 || length >= room) { return 0; }
    memcpy(name, at, length);
    name[length] = '\0';
    end = space(end);
    return (*end == '\0' || *end == '#') && relative_atlas(name);
}

static uint32_t big32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
           (uint32_t)p[2] << 8 | p[3];
}

/* Bound allocation before stb_image sees the atlas, then recheck the
 * decoded dimensions in case the file changes between the two reads. */
static int atlas_dimensions(const char *path, int *width, int *height)
{
    uint8_t head[24];
    FILE *f = fopen(path, "rb");
    if (f == NULL) { return 0; }
    const size_t got = fread(head, 1, sizeof(head), f);
    fclose(f);
    if (got != sizeof(head) || memcmp(head, "\x89PNG\r\n\x1a\n", 8) != 0 ||
        big32(head + 8) != 13 || memcmp(head + 12, "IHDR", 4) != 0) { return 0; }
    const uint32_t w = big32(head + 16), h = big32(head + 20);
    if (w == 0 || h == 0 || w > MAX_DIMENSION || h > MAX_DIMENSION ||
        (uint64_t)w * h > MAX_ATLAS_PIXELS) { return 0; }
    *width = (int)w;
    *height = (int)h;
    return 1;
}

static int read_file(const char *path, Subtitles *out, char *why, size_t size)
{
    char text[512], atlas[256] = "", atlas_path[PATH_ROOM];
    const char *error = NULL;
    size_t capacity = 0;
    FILE *f;
    int expected_w, expected_h;
    out->picture_w = 1920;
    out->picture_h = 1080;
    out->rate = 30;
    if (path == NULL || strlen(path) >= PATH_ROOM || (f = fopen(path, "r")) == NULL) {
        explain(why, size, "subtitle schedule could not be opened");
        return 0;
    }
    while (fgets(text, sizeof(text), f) != NULL) {
        char *at = (char *)space(text), *end = at;
        int values[8];
        if (strchr(text, '\n') == NULL && !feof(f)) {
            error = "subtitle schedule contains an overlong line";
            break;
        }
        if (*at == '\0' || *at == '#') { continue; }
        while (*end != '\0' && !isspace((unsigned char)*end)) { ++end; }
        const size_t keyword = (size_t)(end - at);
        if (keyword == 7 && memcmp(at, "picture", 7) == 0) {
            if (!numbers(end, values, 2) || values[0] <= 0 || values[1] <= 0 ||
                values[0] > MAX_DIMENSION || values[1] > MAX_DIMENSION) {
                error = "invalid subtitle picture dimensions";
                break;
            }
            out->picture_w = values[0];
            out->picture_h = values[1];
        } else if (keyword == 4 && memcmp(at, "rate", 4) == 0) {
            if (!numbers(end, values, 1) || values[0] <= 0 || values[0] > 1000) {
                error = "invalid subtitle frame rate";
                break;
            }
            out->rate = values[0];
        } else if (keyword == 5 && memcmp(at, "atlas", 5) == 0) {
            if (!take_atlas(end, atlas, sizeof(atlas))) {
                error = "invalid relative subtitle atlas path";
                break;
            }
        } else if (keyword == 4 && memcmp(at, "line", 4) == 0) {
            if (!numbers(end, values, 8) || values[0] < 0 || values[1] <= values[0] ||
                values[4] < 0 || values[5] < 0 || values[6] <= 0 || values[7] <= 0 ||
                values[6] > MAX_DIMENSION || values[7] > MAX_DIMENSION || out->count == MAX_LINES) {
                error = "invalid subtitle line timing or source rectangle";
                break;
            }
            if (out->count == capacity) {
                const size_t room = capacity ? capacity * 2u : 64u;
                SubtitleLine *more = realloc(out->lines, room * sizeof(*more));
                if (more == NULL) {
                    error = "not enough memory for subtitle lines";
                    break;
                }
                out->lines = more;
                capacity = room;
            }
            out->lines[out->count++] = (SubtitleLine){values[0], values[1], values[2], values[3],
                                                     values[4], values[5], values[6], values[7]};
        }
        /* Unknown directives retain upstream's forwards-compatible behavior. */
    }
    if (ferror(f)) { error = "subtitle schedule read failed"; }
    fclose(f);
    if (error != NULL) {
        explain(why, size, error);
        return 0;
    }
    if (out->count == 0 || atlas[0] == '\0') {
        explain(why, size, "subtitle schedule has no lines or atlas");
        return 0;
    }
    const char *slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');
    if (backslash != NULL && (slash == NULL || backslash > slash)) { slash = backslash; }
    const int prefix = slash != NULL ? (int)(slash - path + 1) : 0;
    const int length = snprintf(atlas_path, sizeof(atlas_path), "%.*s%s", prefix, path, atlas);
    if (length < 0 || (size_t)length >= sizeof(atlas_path) ||
        !atlas_dimensions(atlas_path, &expected_w, &expected_h)) {
        explain(why, size, "subtitle PNG atlas is missing, malformed, or too large");
        return 0;
    }
    for (size_t i = 0; i < out->count; ++i) {
        const SubtitleLine *line = &out->lines[i];
        if (line->u > expected_w || line->v > expected_h ||
            line->w > expected_w - line->u || line->h > expected_h - line->v) {
            explain(why, size, "subtitle source rectangle lies outside the PNG atlas");
            return 0;
        }
    }
    out->pixels = khdays_png_read(atlas_path, &out->atlas_w, &out->atlas_h, &error);
    if (out->pixels == NULL || out->atlas_w != expected_w || out->atlas_h != expected_h) {
        explain(why, size, error ? error : "subtitle PNG atlas could not be decoded");
        return 0;
    }
    explain(why, size, "");
    return 1;
}

int khdays_ps5_movie_subtitles_load_file(const char *path, char *why, size_t size)
{
    Subtitles next = {0};
    const int ok = read_file(path, &next, why, size);
    if (!ok) { drop(&next); }
    if (pthread_mutex_lock(&subtitles_lock) != 0) {
        drop(&next);
        explain(why, size, "subtitle synchronization failed");
        return 0;
    }
    drop(&loaded);
    loaded = next;
    pthread_mutex_unlock(&subtitles_lock);
    return ok;
}

static int movie_name(const char *movie)
{
    if (movie == NULL || *movie == '\0' || strlen(movie) > 63) { return 0; }
    for (const unsigned char *at = (const unsigned char *)movie; *at != '\0'; ++at) {
        if (!isalnum(*at) && *at != '_' && *at != '-') { return 0; }
    }
    return 1;
}

void khdays_movie_subtitles_load(const char *movie, int language)
{
    static const char *const codes[] = {"", "en", "fr", "de", "it", "es"};
    char relative[160], path[PATH_ROOM], why[256];
    if (language < 1 || language > 5 || !movie_name(movie)) {
        khdays_movie_subtitles_release();
        return;
    }
    snprintf(relative, sizeof(relative), "movies/subtitles/%s/hd%s.txt", codes[language], movie);
    if (!khdays_mods_find(relative, path, sizeof(path))) {
        snprintf(path, sizeof(path), "/app0/data/cutscenes/cinematics/subtitles/%s/hd%s.txt", codes[language], movie);
    }
    if (!khdays_ps5_movie_subtitles_load_file(path, why, sizeof(why))) {
        fprintf(stderr, "movies: subtitles %s: %s: %s\n", movie, path, why);
    }
}

void khdays_movie_subtitles_release(void)
{
    if (pthread_mutex_lock(&subtitles_lock) == 0) {
        drop(&loaded);
        pthread_mutex_unlock(&subtitles_lock);
    }
}

static void blend(uint8_t *destination, const uint8_t source[4])
{
    const unsigned alpha = source[3];
    if (alpha == 0) { return; }
    const unsigned back = (unsigned)destination[3] * (255u - alpha);
    const unsigned total = alpha * 255u + back;
    for (unsigned channel = 0; channel < 3; ++channel) {
        destination[channel] = (uint8_t)(((unsigned)source[channel] * alpha * 255u +
                                          (unsigned)destination[channel] * back + total / 2u) / total);
    }
    destination[3] = (uint8_t)((total + 127u) / 255u);
}

static void sample(const Subtitles *s, const SubtitleLine *line, double x, double y, uint8_t out[4])
{
    /* Clamp to this line's rectangle so linear filtering cannot bleed
     * neighboring subtitles from another part of the atlas. */
    if (x < line->u) { x = line->u; }
    if (y < line->v) { y = line->v; }
    if (x > (double)line->u + line->w - 1) { x = (double)line->u + line->w - 1; }
    if (y > (double)line->v + line->h - 1) { y = (double)line->v + line->h - 1; }
    const int x0 = (int)x, y0 = (int)y;
    const int x1 = x0 + 1 < line->u + line->w ? x0 + 1 : x0;
    const int y1 = y0 + 1 < line->v + line->h ? y0 + 1 : y0;
    const unsigned fx = (unsigned)((x - x0) * 256.0), fy = (unsigned)((y - y0) * 256.0);
    const uint8_t *a = s->pixels + ((size_t)y0 * (size_t)s->atlas_w + (size_t)x0) * 4u;
    const uint8_t *b = s->pixels + ((size_t)y0 * (size_t)s->atlas_w + (size_t)x1) * 4u;
    const uint8_t *c = s->pixels + ((size_t)y1 * (size_t)s->atlas_w + (size_t)x0) * 4u;
    const uint8_t *d = s->pixels + ((size_t)y1 * (size_t)s->atlas_w + (size_t)x1) * 4u;
    for (unsigned channel = 0; channel < 4; ++channel) {
        out[channel] = (uint8_t)(((unsigned)a[channel] * (256u - fx) * (256u - fy) +
                                  (unsigned)b[channel] * fx * (256u - fy) +
                                  (unsigned)c[channel] * (256u - fx) * fy +
                                  (unsigned)d[channel] * fx * fy + 32768u) >> 16);
    }
}

int khdays_ps5_movie_subtitles_draw_rgba(uint8_t *rgba, size_t capacity,
                                      size_t stride, int width, int height,
                                      double time, double fps)
{
    int rendered = 0;
    if (rgba == NULL || width <= 0 || height <= 0 || width > MAX_DIMENSION || height > MAX_DIMENSION ||
        (size_t)width > SIZE_MAX / 4u || stride < (size_t)width * 4u ||
        (size_t)(height - 1) > (SIZE_MAX - (size_t)width * 4u) / stride ||
        capacity < (size_t)(height - 1) * stride + (size_t)width * 4u ||
        !finite_double(time) || !finite_double(fps) || time < 0.0) { return -1; }
    if (pthread_mutex_lock(&subtitles_lock) != 0) { return -1; }
    if (loaded.count == 0 || loaded.pixels == NULL) {
        pthread_mutex_unlock(&subtitles_lock);
        return 0;
    }
    const double difference = fps - loaded.rate;
    const double fractional = fps - loaded.rate * 1000.0 / 1001.0;
    const double rate = fps > 0.0 && ((difference > -0.01 && difference < 0.01) ||
                                    (fractional > -0.01 && fractional < 0.01)) ? fps : loaded.rate;
    const double when = time * rate + 0.5;
    if (!finite_double(when) || when > INT_MAX) {
        pthread_mutex_unlock(&subtitles_lock);
        return 0;
    }
    const int frame = (int)when;
    const double kx = (double)width / loaded.picture_w, ky = (double)height / loaded.picture_h;
    for (size_t i = 0; i < loaded.count; ++i) {
        const SubtitleLine *line = &loaded.lines[i];
        if (frame < line->first || frame >= line->end) { continue; }
        const double left = line->x * kx, top = line->y * ky;
        const double right = ((double)line->x + line->w) * kx;
        const double bottom = ((double)line->y + line->h) * ky;
        if (right <= 0.0 || bottom <= 0.0 || left >= width || top >= height) { continue; }
        const int x0 = left > 0.0 ? (int)left : 0, y0 = top > 0.0 ? (int)top : 0;
        const int x1 = right < width ? (int)right + 1 : width;
        const int y1 = bottom < height ? (int)bottom + 1 : height;
        int contributed = 0;
        for (int y = y0; y < y1; ++y) {
            const double local_y = (y + 0.5) / ky - line->y;
            if (local_y < 0.0 || local_y >= line->h) { continue; }
            for (int x = x0; x < x1; ++x) {
                const double local_x = (x + 0.5) / kx - line->x;
                if (local_x < 0.0 || local_x >= line->w) { continue; }
                uint8_t source[4];
                sample(&loaded, line, line->u + local_x - 0.5, line->v + local_y - 0.5, source);
                if (source[3] != 0) {
                    blend(rgba + (size_t)y * stride + (size_t)x * 4u, source);
                    contributed = 1;
                }
            }
        }
        rendered += contributed;
    }
    pthread_mutex_unlock(&subtitles_lock);
    return rendered;
}
