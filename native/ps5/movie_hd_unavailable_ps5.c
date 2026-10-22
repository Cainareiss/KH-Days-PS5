/* Optional host HD decoder capability for the native PS5 runtime.
 *
 * The Windows implementation uses Media Foundation. That decoder has not
 * been ported to this native runtime. Report the capability as unavailable
 * so runtime/movies/movie_hd.c can keep the original DS MobiClip path.
 * None of these functions claims to open/play/finish an HD video. */
#include "host/movies/movie_hd.h"

#include <stdio.h>
#include <string.h>

static const char unavailable[] =
    "HD movie decoding is unavailable in this PS5 runtime; use the original DS movies";

static void explain(char *why, size_t size)
{
    if (why != NULL && size != 0) {
        (void)snprintf(why, size, "%s", unavailable);
    }
}

int khdays_hd_open(const char *path, KhdaysHdInfo *info, char *why, size_t size)
{
    (void)path;
    if (info != NULL) {
        memset(info, 0, sizeof(*info));
    }
    explain(why, size);
    return 0;
}

int khdays_hd_status(char *why, size_t size)
{
    /* The contract's 2 means no video is open, rather than an ended video. */
    explain(why, size);
    return 2;
}

int khdays_hd_picture_begin(KhdaysHdPicture *picture)
{
    if (picture != NULL) {
        memset(picture, 0, sizeof(*picture));
    }
    return 0;
}

/* Every open fails, hence there is no live picture/audio/clock to change. */
void khdays_hd_start(void) {}
void khdays_hd_pause(int pause) { (void)pause; }
void khdays_hd_close(void) {}
void khdays_hd_set_gain(float gain) { (void)gain; }
void khdays_hd_picture_done(void) {}
