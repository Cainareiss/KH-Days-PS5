/* Execute the complete adapted movie state machine. Only the filesystem mount,
 * decoder, clock and synchronization services are simulated on the host. */
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "host/movies/movie_hd.h"

typedef int32_t LONG;
typedef unsigned DWORD;
typedef int SRWLOCK;
#define SRWLOCK_INIT 0
#define INVALID_FILE_ATTRIBUTES UINT32_MAX
#define KHDAYS_HOST_MOVIES_HD 8
#define KHDAYS_KEY_START 8
#define KHDAYS_HOLD_MOVIE 2

static const char *app_root, *save_root;
static int graphics_flags = KHDAYS_HOST_MOVIES_HD;
static int decoder_opens, decoder_starts, decoder_closes;
static int decoder_ok = 1, decoder_status, mod_movie;
static int held, muted;
static char opened_path[1024];

static void AcquireSRWLockExclusive(SRWLOCK *lock) { (void)lock; }
static void ReleaseSRWLockExclusive(SRWLOCK *lock) { (void)lock; }
static LONG InterlockedExchange(volatile LONG *at, LONG value)
{ const LONG before = *at; *at = value; return before; }
static LONG InterlockedCompareExchange(volatile LONG *at, LONG value, LONG expected)
{ const LONG before = *at; if (before == expected) *at = value; return before; }
static int khdays_host_graphics_flags(void) { return graphics_flags; }
static const char *khdays_host_user_folder(void) { return save_root; }
static int khdays_mods_find(const char *relative, char *out, int size)
{
    assert(strncmp(relative, "movies/hd", 9) == 0);
    assert(strstr(relative, ".mpg") != NULL);
    if (!mod_movie) return 0;
    snprintf(out, (size_t)size, "%s/mod-hd802.mpg", app_root);
    return 1;
}
static DWORD GetFileAttributesA(const char *path)
{
    char mounted[2048];
    const char *actual = path;
    struct stat info;
    if (strncmp(path, "/app0/", 6) == 0) {
        snprintf(mounted, sizeof(mounted), "%s/%s", app_root, path + 6);
        actual = mounted;
    }
    return stat(actual, &info) == 0 ? 0 : INVALID_FILE_ATTRIBUTES;
}
static void khdays_log(const char *format, ...) { (void)format; } /* disabled */
static void khdays_clock_hold(unsigned reason, int on)
{ assert(reason == KHDAYS_HOLD_MOVIE); held = on; }
static void khdays_sound_output_mute(int on) { muted = on; }

int khdays_hd_open(const char *path, KhdaysHdInfo *info, char *why, size_t size)
{
    ++decoder_opens;
    snprintf(opened_path, sizeof(opened_path), "%s", path);
    if (!decoder_ok) {
        snprintf(why, size, "test decoder rejected video headers");
        return 0;
    }
    *info = (KhdaysHdInfo){1920, 1080, 30000.0 / 1001.0, 2, 48000};
    return 1;
}
void khdays_hd_start(void) { ++decoder_starts; }
void khdays_hd_close(void) { ++decoder_closes; }
int khdays_hd_status(char *why, size_t size)
{ (void)why; (void)size; return decoder_status; }

#include "movie_runtime_body.h"

int main(int argc, char **argv)
{
    assert(argc == 3);
    app_root = argv[1];
    save_root = argv[2];
    /* The process runs in a directory without a data/ subtree. Relative paths
     * would silently miss this mounted movie before the decoder is called. */
    assert(GetFileAttributesA("data/cutscenes/cinematics/hd802.mpg") == INVALID_FILE_ATTRIBUTES);
    khdays_movie_hd_stream(1, "/mv/802.mods", 0);
    assert(decoder_opens == 1 && decoder_starts == 1);
    assert(strcmp(opened_path, "/app0/data/cutscenes/cinematics/hd802.mpg") == 0);
    assert(khdays_movie_hd_showing() && muted);
    khdays_movie_hd_stopped();
    assert(held && khdays_movie_hd_showing());
    decoder_status = 1;
    khdays_movie_hd_poll();
    assert(!khdays_movie_hd_showing() && !held && !muted);
    assert(decoder_closes == 1);

    /* Missing files stay on DS playback and log only once per movie id. */
    khdays_movie_hd_stream(1, "mv/999.mods", 0);
    khdays_movie_hd_stream(1, "mv/999.mods", 0);
    assert(decoder_opens == 1 && !khdays_movie_hd_showing());

    decoder_ok = 0;
    khdays_movie_hd_stream(1, "mv/803.mods", 0);
    assert(decoder_opens == 2 && !khdays_movie_hd_showing());

    /* Preserve mod priority and the user's HD off switch. */
    decoder_ok = 1;
    decoder_status = 0;
    mod_movie = 1;
    khdays_movie_hd_stream(1, "mv/802.mods", 0);
    assert(decoder_opens == 3 && strstr(opened_path, "/mod-hd802.mpg") != NULL);
    khdays_movie_hd_reset();
    assert(!khdays_movie_hd_showing());
    graphics_flags = 0;
    khdays_movie_hd_stream(1, "mv/802.mods", 0);
    graphics_flags = KHDAYS_HOST_MOVIES_HD;
    khdays_movie_hd_stream(1, "mv/802.mods", 1);
    assert(decoder_opens == 3);
    puts("native PS5 HD movie paths and persistent fallback: PASS");
    return 0;
}
