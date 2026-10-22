/* Runtime services for FreeBSD/PS5. Windows-shaped names are private source
 * compatibility for the pinned upstream; no Win32 DLL or SDL is required. */
#include "runtime_posix.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

enum { HANDLE_THREAD, HANDLE_EVENT, HANDLE_DIRECTORY };
struct KhdaysPosixHandle {
    int kind;
    pthread_t thread;
    int joined;
    DWORD (*entry)(void *);
    int (*int_entry)(void *);
    void *parameter;
    int result;
    pthread_mutex_t lock;
    pthread_cond_t condition;
    int manual_reset, signalled;
    DIR *directory;
    char directory_path[MAX_PATH];
};

static void check_pthread(int code)
{
    if (code != 0) {
        fprintf(stderr, "runtime pthread: %s\n", strerror(code));
        abort();
    }
}

/* Shared reads are serialized by a mutex. This is semantically stronger than
 * an SRW reader lock and permits the same condition-variable wait contract. */
void InitializeSRWLock(SRWLOCK *lock) { check_pthread(pthread_mutex_init(lock, NULL)); }
void AcquireSRWLockExclusive(SRWLOCK *lock) { check_pthread(pthread_mutex_lock(lock)); }
void ReleaseSRWLockExclusive(SRWLOCK *lock) { check_pthread(pthread_mutex_unlock(lock)); }
void AcquireSRWLockShared(SRWLOCK *lock) { AcquireSRWLockExclusive(lock); }
void ReleaseSRWLockShared(SRWLOCK *lock) { ReleaseSRWLockExclusive(lock); }
int TryAcquireSRWLockExclusive(SRWLOCK *lock) { return pthread_mutex_trylock(lock) == 0; }

static struct timespec deadline(DWORD ms)
{
    struct timespec until;
    if (clock_gettime(CLOCK_REALTIME, &until) != 0) abort();
    until.tv_sec += ms / 1000u;
    until.tv_nsec += (long)(ms % 1000u) * 1000000L;
    if (until.tv_nsec >= 1000000000L) { ++until.tv_sec; until.tv_nsec -= 1000000000L; }
    return until;
}

int SleepConditionVariableSRW(CONDITION_VARIABLE *condition, SRWLOCK *lock, DWORD ms, unsigned flags)
{
    int code;
    (void)flags; /* upstream passes 0 (exclusive lock) */
    if (ms == INFINITE) code = pthread_cond_wait(condition, lock);
    else { const struct timespec until = deadline(ms); code = pthread_cond_timedwait(condition, lock, &until); }
    if (code != 0 && code != ETIMEDOUT) check_pthread(code);
    return code == 0;
}
void WakeConditionVariable(CONDITION_VARIABLE *condition) { check_pthread(pthread_cond_signal(condition)); }
LONG InterlockedExchange(volatile LONG *p, LONG v) { return __atomic_exchange_n(p, v, __ATOMIC_SEQ_CST); }
LONG InterlockedCompareExchange(volatile LONG *p, LONG v, LONG expected)
{
    __atomic_compare_exchange_n(p, &expected, v, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return expected;
}
LONG InterlockedExchangeAdd(volatile LONG *p, LONG v) { return __atomic_fetch_add(p, v, __ATOMIC_SEQ_CST); }
LONG InterlockedIncrement(volatile LONG *p) { return __atomic_add_fetch(p, 1, __ATOMIC_SEQ_CST); }
uint64_t khdays_posix_ticks_ns(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) abort();
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
}
int QueryPerformanceCounter(LARGE_INTEGER *v) { v->QuadPart = (int64_t)khdays_posix_ticks_ns(); return 1; }
int QueryPerformanceFrequency(LARGE_INTEGER *v) { v->QuadPart = INT64_C(1000000000); return 1; }
void GetLocalTime(SYSTEMTIME *v)
{
    const time_t now = time(NULL);
    struct tm broken;
    if (localtime_r(&now, &broken) == NULL) { memset(v, 0, sizeof(*v)); return; }
    *v = (SYSTEMTIME){(unsigned)(broken.tm_year + 1900), (unsigned)(broken.tm_mon + 1),
                     (unsigned)broken.tm_mday, (unsigned)broken.tm_hour,
                     (unsigned)broken.tm_min, (unsigned)broken.tm_sec};
}
void Sleep(DWORD ms)
{
    struct timespec pause = {(time_t)(ms / 1000u), (long)(ms % 1000u) * 1000000L};
    while (nanosleep(&pause, &pause) != 0 && errno == EINTR) {}
}

static void *thread_main(void *argument)
{
    HANDLE h = argument;
    h->result = h->int_entry ? h->int_entry(h->parameter) : (int)h->entry(h->parameter);
    return NULL;
}

static HANDLE create_thread(DWORD (*entry)(void *), int (*int_entry)(void *), void *parameter, size_t stack_size)
{
    HANDLE h = calloc(1, sizeof(*h));
    pthread_attr_t attributes;
    int code;
    if (h == NULL) return NULL;
    h->kind = HANDLE_THREAD; h->entry = entry; h->int_entry = int_entry; h->parameter = parameter;
    check_pthread(pthread_attr_init(&attributes));
    if (stack_size != 0) check_pthread(pthread_attr_setstacksize(&attributes, stack_size));
    code = pthread_create(&h->thread, &attributes, thread_main, h);
    check_pthread(pthread_attr_destroy(&attributes));
    if (code != 0) { fprintf(stderr, "runtime thread: %s\n", strerror(code)); free(h); return NULL; }
    return h;
}
HANDLE CreateThread(void *attributes, size_t stack_size, DWORD (*entry)(void *), void *parameter,
                    DWORD flags, DWORD *thread_id)
{
    (void)attributes;
    /* Suspended starts/numeric Win32 thread IDs are not used by the runtime. */
    if (flags != 0 || thread_id != NULL) { errno = ENOTSUP; return NULL; }
    return create_thread(entry, NULL, parameter, stack_size);
}
HANDLE CreateEventA(void *attributes, int manual_reset, int initial_state, const char *name)
{
    HANDLE h;
    (void)attributes;
    if (name != NULL) { errno = ENOTSUP; return NULL; }
    h = calloc(1, sizeof(*h));
    if (h == NULL) return NULL;
    h->kind = HANDLE_EVENT; h->manual_reset = manual_reset; h->signalled = initial_state;
    check_pthread(pthread_mutex_init(&h->lock, NULL));
    check_pthread(pthread_cond_init(&h->condition, NULL));
    return h;
}
int SetEvent(HANDLE h)
{
    if (h == NULL || h->kind != HANDLE_EVENT) return 0;
    check_pthread(pthread_mutex_lock(&h->lock)); h->signalled = 1;
    check_pthread(h->manual_reset ? pthread_cond_broadcast(&h->condition) : pthread_cond_signal(&h->condition));
    check_pthread(pthread_mutex_unlock(&h->lock));
    return 1;
}
DWORD WaitForSingleObject(HANDLE h, DWORD ms)
{
    if (h == NULL || h == INVALID_HANDLE_VALUE) return WAIT_FAILED;
    if (h->kind == HANDLE_THREAD) {
        if (ms != INFINITE) { errno = ENOTSUP; return WAIT_FAILED; }
        if (!h->joined) { check_pthread(pthread_join(h->thread, NULL)); h->joined = 1; }
        return WAIT_OBJECT_0;
    }
    if (h->kind == HANDLE_EVENT) {
        int code = 0;
        const struct timespec until = ms != INFINITE ? deadline(ms) : (struct timespec){0, 0};
        check_pthread(pthread_mutex_lock(&h->lock));
        while (!h->signalled && code != ETIMEDOUT) {
            code = ms == INFINITE ? pthread_cond_wait(&h->condition, &h->lock)
                                  : pthread_cond_timedwait(&h->condition, &h->lock, &until);
            if (code != 0 && code != ETIMEDOUT) check_pthread(code);
        }
        const int signalled = h->signalled;
        if (signalled && !h->manual_reset) h->signalled = 0;
        check_pthread(pthread_mutex_unlock(&h->lock));
        return signalled ? WAIT_OBJECT_0 : WAIT_TIMEOUT;
    }
    return WAIT_FAILED;
}
int CloseHandle(HANDLE h)
{
    if (h == NULL || h == INVALID_HANDLE_VALUE) return 0;
    if (h->kind == HANDLE_THREAD && !h->joined) {
        /* The runtime closes movie workers only after joining. Avoid freeing
         * the trampoline's storage while a worker can still reference it. */
        if (WaitForSingleObject(h, INFINITE) != WAIT_OBJECT_0) return 0;
    } else if (h->kind == HANDLE_EVENT) {
        check_pthread(pthread_cond_destroy(&h->condition));
        check_pthread(pthread_mutex_destroy(&h->lock));
    } else if (h->kind == HANDLE_DIRECTORY && h->directory != NULL) closedir(h->directory);
    free(h); return 1;
}

static int normalized_path(const char *input, char *output, size_t size)
{
    const size_t length = strlen(input);
    if (length >= size) { errno = ENAMETOOLONG; return 0; }
    for (size_t i = 0; i <= length; ++i) output[i] = input[i] == '\\' ? '/' : input[i];
    return 1;
}
DWORD GetFileAttributesA(const char *path)
{
    char clean[MAX_PATH]; struct stat info;
    return !normalized_path(path, clean, sizeof(clean)) || stat(clean, &info) != 0
           ? INVALID_FILE_ATTRIBUTES : S_ISDIR(info.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : 0;
}
int CreateDirectoryA(const char *path, void *attributes)
{
    char clean[MAX_PATH]; (void)attributes;
    return normalized_path(path, clean, sizeof(clean)) && mkdir(clean, 0700) == 0;
}
int FindNextFileA(HANDLE h, WIN32_FIND_DATAA *entry)
{
    struct dirent *found;
    if (h == NULL || h == INVALID_HANDLE_VALUE || h->kind != HANDLE_DIRECTORY) return 0;
    while ((found = readdir(h->directory)) != NULL) {
        char path[MAX_PATH];
        if (snprintf(path, sizeof(path), "%s/%s", h->directory_path, found->d_name) >= (int)sizeof(path)) continue;
        entry->dwFileAttributes = GetFileAttributesA(path);
        if (entry->dwFileAttributes == INVALID_FILE_ATTRIBUTES) continue;
        snprintf(entry->cFileName, sizeof(entry->cFileName), "%s", found->d_name);
        return 1;
    }
    return 0;
}
HANDLE FindFirstFileA(const char *pattern, WIN32_FIND_DATAA *entry)
{
    char clean[MAX_PATH]; HANDLE h; size_t length;
    if (!normalized_path(pattern, clean, sizeof(clean))) return INVALID_HANDLE_VALUE;
    length = strlen(clean);
    /* The pinned runtime enumerates complete directories, with a final star. */
    if (length < 2 || strcmp(clean + length - 2, "/*") != 0) { errno = ENOTSUP; return INVALID_HANDLE_VALUE; }
    clean[length - 2] = '\0';
    h = calloc(1, sizeof(*h)); if (h == NULL) return INVALID_HANDLE_VALUE;
    h->kind = HANDLE_DIRECTORY;
    snprintf(h->directory_path, sizeof(h->directory_path), "%s", clean[0] ? clean : "/");
    h->directory = opendir(h->directory_path);
    if (h->directory == NULL || !FindNextFileA(h, entry)) { CloseHandle(h); return INVALID_HANDLE_VALUE; }
    return h;
}
int FindClose(HANDLE h) { return CloseHandle(h); }

DWORD GetFullPathNameA(const char *path, DWORD size, char *output, char **file_part)
{
    char clean[MAX_PATH], absolute[MAX_PATH], cwd[MAX_PATH];
    if (!normalized_path(path, clean, sizeof(clean))) return 0;
    if (clean[0] == '/') snprintf(absolute, sizeof(absolute), "%s", clean);
    else {
        if (getcwd(cwd, sizeof(cwd)) == NULL) return 0;
        if (snprintf(absolute, sizeof(absolute), "%s/%s", cwd, clean) >= (int)sizeof(absolute)) return 0;
    }
    /* Lexically canonicalize even a destination that does not exist yet. */
    char canonical[MAX_PATH] = "/"; size_t used = 1;
    char *save, *part = strtok_r(absolute, "/", &save);
    while (part != NULL) {
        if (strcmp(part, "..") == 0) {
            while (used > 1 && canonical[used - 1] != '/') --used;
            if (used > 1) --used;
            canonical[used] = '\0';
        } else if (strcmp(part, ".") != 0) {
            const size_t n = strlen(part);
            if (used > 1) canonical[used++] = '/';
            if (used + n >= sizeof(canonical)) return 0;
            memcpy(canonical + used, part, n + 1); used += n;
        }
        part = strtok_r(NULL, "/", &save);
    }
    if (used + 1 > size) return (DWORD)used + 1;
    memcpy(output, canonical, used + 1);
    if (file_part != NULL) *file_part = strrchr(output, '/') + 1;
    return (DWORD)used;
}
DWORD GetEnvironmentVariableA(const char *name, char *output, DWORD size)
{
    const char *value = getenv(name); size_t length;
    if (value == NULL) return 0;
    length = strlen(value);
    if (length + 1 > size) return (DWORD)length + 1;
    memcpy(output, value, length + 1); return (DWORD)length;
}
int strncpy_s(char *destination, size_t size, const char *source, size_t count)
{
    if (size == 0) return ERANGE;
    size_t n = strlen(source); if (count != _TRUNCATE && n > count) n = count;
    if (n >= size) n = size - 1;
    memcpy(destination, source, n); destination[n] = '\0'; return 0;
}
int strncat_s(char *destination, size_t size, const char *source, size_t count)
{
    const size_t used = strnlen(destination, size);
    return used == size ? ERANGE : strncpy_s(destination + used, size - used, source, count);
}
int fopen_s(FILE **file, const char *path, const char *mode)
{
    if (file == NULL || path == NULL || mode == NULL) return EINVAL;
    *file = fopen(path, mode);
    return *file != NULL ? 0 : errno;
}
KhdaysPosixMutex *khdays_posix_mutex_create(void)
{
    KhdaysPosixMutex *mutex = malloc(sizeof(*mutex));
    if (mutex != NULL) check_pthread(pthread_mutex_init(mutex, NULL));
    return mutex;
}
void khdays_posix_mutex_lock(KhdaysPosixMutex *mutex) { AcquireSRWLockExclusive(mutex); }
void khdays_posix_mutex_unlock(KhdaysPosixMutex *mutex) { ReleaseSRWLockExclusive(mutex); }
KhdaysPosixThread khdays_posix_thread_create(int (*entry)(void *), const char *name, void *parameter)
{
    (void)name; return create_thread(NULL, entry, parameter, 0);
}
void khdays_posix_thread_wait(KhdaysPosixThread thread, int *result)
{
    if (thread != NULL && WaitForSingleObject(thread, INFINITE) == WAIT_OBJECT_0) {
        if (result != NULL) *result = thread->result;
        CloseHandle(thread);
    }
}
void *khdays_posix_load_file(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb"); void *data; long length;
    if (file == NULL) return NULL;
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file); return NULL;
    }
    data = malloc((size_t)length + 1);
    if (data == NULL || fread(data, 1, (size_t)length, file) != (size_t)length) {
        free(data); fclose(file); return NULL;
    }
    ((unsigned char *)data)[length] = 0;
    fclose(file); if (size != NULL) *size = (size_t)length; return data;
}
int khdays_posix_save_file(const char *path, const void *data, size_t size)
{
    FILE *file = fopen(path, "wb"); int ok;
    if (file == NULL) return 0;
    ok = fwrite(data, 1, size, file) == size;
    return fclose(file) == 0 && ok;
}
