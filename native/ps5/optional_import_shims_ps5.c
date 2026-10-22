/* Optional libc compatibility imports emitted by the static C++ runtime.
 * They are weak on the PS5, so a title that does not use runtime dynamic
 * loading/network formatting can safely provide inert local definitions.
 * Keeping them local avoids asking ps5-native-tool to resolve private
 * private double-underscore loader/network names that are not public SDK
 * NIDs. */
#include <stddef.h>

void *__dlopen(const char *name, int mode)
{
    (void)name;
    (void)mode;
    return NULL;
}

void *__dlsym(void *handle, const char *name)
{
    (void)handle;
    (void)name;
    return NULL;
}

int __dladdr(const void *address, void *info)
{
    (void)address;
    (void)info;
    return 0;
}

int __dlclose(void *handle)
{
    (void)handle;
    return 0;
}

char *__dlerror(void)
{
    return NULL;
}

const char *__inet_ntop(int family, const void *source, char *destination,
                        unsigned long destination_size)
{
    (void)family;
    (void)source;
    (void)destination;
    (void)destination_size;
    return NULL;
}

int __inet_aton(const char *text, void *address)
{
    (void)text;
    (void)address;
    return 0;
}

int kernel_mprotect(int pid, long address, unsigned long size, int protection)
{
    (void)pid;
    (void)address;
    (void)size;
    (void)protection;
    return 0;
}
