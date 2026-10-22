/* /app0 is read-only; saves must remain outside the mounted game image. */
#include "paths_ps5.h"

#include <stddef.h>
#include <string.h>

#define PATH_ROOM 1024u
static char data_root[PATH_ROOM] = "/app0/data/extracted";
static char save_root[PATH_ROOM] = "/data/khdays-native/saves";
static char mods_root[PATH_ROOM] = "/app0/mods";

static int valid(const char *path)
{
    if (path == NULL) {
        return 1;
    }
    /* Drive-prefixed absolute paths also permit the same parser to be
     * exercised by a host-side harness without changing its source. */
    return strlen(path) < PATH_ROOM &&
           (path[0] == '/' ||
            (path[0] != '\0' && path[1] == ':' &&
             (path[2] == '/' || path[2] == '\\')));
}

static void take(char *out, const char *path)
{
    if (path != NULL) {
        size_t length = strlen(path);
        memcpy(out, path, length + 1u);
        while (length > 1u && !(length == 3u && out[1] == ':') &&
               (out[length - 1u] == '/' || out[length - 1u] == '\\')) {
            out[--length] = '\0';
        }
    }
}

int khdays_ps5_set_paths(const char *data, const char *saves, const char *mods)
{
    if (!valid(data) || !valid(saves) || !valid(mods)) {
        return 0;
    }
    take(data_root, data);
    take(save_root, saves);
    take(mods_root, mods);
    return 1;
}

const char *khdays_ps5_data_root(void) { return data_root; }
const char *khdays_ps5_save_root(void) { return save_root; }
const char *khdays_ps5_mods_root(void) { return mods_root; }
