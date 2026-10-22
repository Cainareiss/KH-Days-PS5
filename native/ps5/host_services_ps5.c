/* Single-title host services for khdays-port. No desktop instance manager. */
#include "host.h"
#include "instance.h"
#include "profile.h"
#include "paths_ps5.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

static int player_slot;

static int make_directories(const char *path)
{
    char copy[1024];
    const int length = snprintf(copy, sizeof(copy), "%s", path);
    if (length <= 0 || (size_t)length >= sizeof(copy)) return 0;
    for (char *p = copy + 1; ; ++p) {
        if (*p == '/' || *p == '\0') {
            const char saved = *p;
            *p = '\0';
            if (mkdir(copy, 0755) != 0 && errno != EEXIST) return 0;
            *p = saved;
            if (!saved) break;
        }
    }
    return 1;
}

int khdays_host_player_slot(void)
{
    if (!make_directories(khdays_ps5_save_root())) {
        fprintf(stderr, "host: cannot create save directory %s: %s\n",
                khdays_ps5_save_root(), strerror(errno));
        return 0;
    }
    player_slot = 1;
    return player_slot;
}

int khdays_host_player_slot_taken(void) { return player_slot; }
int khdays_host_other_copies(void) { return 0; }
const char *khdays_host_user_folder(void) { return khdays_ps5_save_root(); }

void khdays_host_automatic_name(char *out, size_t size)
{
    if (size) snprintf(out, size, "PlayerPS5");
}

int khdays_host_player_name(const char *nickname, unsigned short *out, int room)
{
    const char *name = getenv("KHDAYS_NAME");
    int length = 0;
    if (!name || !*name) name = nickname;
    if (!name || !*name) name = "PlayerPS5";
    for (const unsigned char *p = (const unsigned char *)name; *p && length < room; ++p) {
        if (*p >= 32 && *p <= 126) out[length++] = *p;
    }
    return length;
}

int khdays_settings_game_language(void)
{
    static const char *const codes[] = {"en", "fr", "de", "it", "es"};
    const char *value = getenv("KHDAYS_LANGUAGE");
    if (value) {
        for (int i = 0; i < 5; ++i) {
            if (!strncasecmp(value, codes[i], 2) ||
                (value[0] == '1' + i && value[1] == '\0')) return i + 1;
        }
    }
    return 1;
}

void khdays_settings_nickname(char *nickname, int size)
{
    const char *value = getenv("KHDAYS_NAME");
    if (size > 0) snprintf(nickname, (size_t)size, "%s", value ? value : "PlayerPS5");
}

int khdays_settings_server(char *server, int size)
{
    const char *value = getenv("KHDAYS_SERVER");
    if (size > 0) snprintf(server, (size_t)size, "%s", value ? value : "");
    return value && *value;
}

void khdays_host_user_path(const char *name, char *path, int size)
{
    if (size > 0) snprintf(path, (size_t)size, "%s/%s", khdays_ps5_save_root(), name);
}

void khdays_settings_path(char *path, int size)
{
    const char *value = getenv("KHDAYS_SETTINGS");
    if (value && size > 0) snprintf(path, (size_t)size, "%s", value);
    else khdays_host_user_path("settings.ini", path, size);
}
