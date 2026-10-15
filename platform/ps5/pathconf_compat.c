/* The PS5 payload SDK exposes pathconf only through libkernel_sys, which the
 * title does not load.  libc++ filesystem may query these limits while opening
 * game assets; provide conservative, path-independent limits locally. */
#include <errno.h>
#include <unistd.h>

long pathconf(const char* path, int name) {
    if (path == 0) {
        errno = EINVAL;
        return -1;
    }
    switch (name) {
    case _PC_PATH_MAX: return 1024;
    case _PC_NAME_MAX: return 255;
    case _PC_LINK_MAX: return 1;
    case _PC_NO_TRUNC: return 1;
    default:
        errno = EINVAL;
        return -1;
    }
}
