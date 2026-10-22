/* PS5 files and local time for khdays-port v0.1.1's host contract. */
#include "files.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static int make_folders(const char *path)
{
    char *copy = strdup(path);
    if (copy == NULL) {
        return 0;
    }
    for (char *at = copy + 1; *at != '\0'; ++at) {
        if (*at == '/') {
            *at = '\0';
            if (mkdir(copy, 0755) != 0 && errno != EEXIST) {
                free(copy);
                return 0;
            }
            *at = '/';
        }
    }
    free(copy);
    return 1;
}
int khdays_host_write_file_atomic(const char *path, const void *data,
                                  size_t size)
{
    const size_t length = strlen(path) + 32u;
    char *temporary = (char *)malloc(length);
    const unsigned char *at = (const unsigned char *)data;
    size_t left = size;
    int descriptor = -1;
    int ok = 0;
    if (temporary == NULL ||
        snprintf(temporary, length, "%s.%ld.tmp", path, (long)getpid()) < 0 ||
        !make_folders(path)) {
        free(temporary);
        return 0;
    }
    descriptor = open(temporary, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (descriptor < 0) {
        fprintf(stderr, "host: cannot write beside %s: %s\n", path,
                strerror(errno));
        free(temporary);
        return 0;
    }
    while (left > 0) {
        const ssize_t written = write(descriptor, at, left);
        if (written <= 0) {
            break;
        }
        at += written;
        left -= (size_t)written;
    }
    ok = left == 0 && fsync(descriptor) == 0;
    if (close(descriptor) != 0) {
        ok = 0;
    }
    if (ok && rename(temporary, path) != 0) {
        ok = 0;
    }
    if (!ok) {
        fprintf(stderr, "host: cannot write %s: %s; it stays as it was\n",
                path, strerror(errno));
        unlink(temporary);
    }
    free(temporary);
    return ok;
}

long khdays_host_read_file(const char *path, void *data, size_t room)
{
    unsigned char *at = (unsigned char *)data;
    size_t total = 0;
    int descriptor = open(path, O_RDONLY);
    if (descriptor < 0) {
        return -1;
    }
    while (total < room) {
        const ssize_t got = read(descriptor, at + total, room - total);
        if (got <= 0) {
            break;
        }
        total += (size_t)got;
    }
    close(descriptor);
    return (long)total;
}

void khdays_host_local_time(int *hour, int *minute, int *second)
{
    const time_t now = time(NULL);
    struct tm local;
    if (now != (time_t)-1 && localtime_r(&now, &local) != NULL) {
        *hour = local.tm_hour;
        *minute = local.tm_min;
        *second = local.tm_sec;
    } else {
        *hour = *minute = *second = 0;
    }
}

