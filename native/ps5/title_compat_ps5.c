/* Native-title filesystem limits used by the static C++ runtime.
 * Matches the PS5 Melon Mix title adapter, without payload syscall shims. */
#include <errno.h>
#include <unistd.h>

long pathconf(const char *path, int name)
{
    if (path == NULL) {
        errno = EINVAL;
        return -1;
    }
    switch (name) {
    case _PC_PATH_MAX: return 1024;
    case _PC_NAME_MAX: return 255;
    case _PC_LINK_MAX: return 1;
    case _PC_NO_TRUNC: return 1;
    default: errno = EINVAL; return -1;
    }
}
