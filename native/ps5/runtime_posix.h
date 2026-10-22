/* POSIX services used by the copied khdays-port runtime. No Win32 imports.
 * The names below preserve the pinned runtime's small synchronization contract;
 * their implementation uses pthreads, clock_gettime and the POSIX filesystem. */
#ifndef KHDAYS_PS5_RUNTIME_POSIX_H
#define KHDAYS_PS5_RUNTIME_POSIX_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <pthread.h>

typedef int32_t LONG;
typedef int64_t LONG64;
typedef int64_t LONGLONG;
typedef uint32_t DWORD;
typedef int BOOLEAN;
typedef void *LPVOID;
typedef struct { int64_t QuadPart; } LARGE_INTEGER;
typedef struct { unsigned wYear, wMonth, wDay, wHour, wMinute, wSecond; } SYSTEMTIME;
typedef pthread_mutex_t SRWLOCK;
typedef pthread_cond_t CONDITION_VARIABLE;
#define SRWLOCK_INIT PTHREAD_MUTEX_INITIALIZER
#define CONDITION_VARIABLE_INIT PTHREAD_COND_INITIALIZER
#define WINAPI
#define FALSE 0
#define TRUE 1
#define MAX_PATH 4096
#define INFINITE UINT32_MAX
#define WAIT_OBJECT_0 0u
#define WAIT_TIMEOUT 258u
#define WAIT_FAILED UINT32_MAX
#define FILE_ATTRIBUTE_DIRECTORY 0x10u
#define INVALID_FILE_ATTRIBUTES UINT32_MAX
#define _TRUNCATE SIZE_MAX
#define _stricmp strcasecmp
#define _strnicmp strncasecmp

typedef struct KhdaysPosixHandle *HANDLE;
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
typedef struct { DWORD dwFileAttributes; char cFileName[MAX_PATH]; } WIN32_FIND_DATAA;

void InitializeSRWLock(SRWLOCK *lock);
void AcquireSRWLockExclusive(SRWLOCK *lock);
void ReleaseSRWLockExclusive(SRWLOCK *lock);
void AcquireSRWLockShared(SRWLOCK *lock);
void ReleaseSRWLockShared(SRWLOCK *lock);
int TryAcquireSRWLockExclusive(SRWLOCK *lock);
int SleepConditionVariableSRW(CONDITION_VARIABLE *condition, SRWLOCK *lock, DWORD ms, unsigned flags);
void WakeConditionVariable(CONDITION_VARIABLE *condition);
LONG InterlockedExchange(volatile LONG *target, LONG value);
LONG InterlockedCompareExchange(volatile LONG *target, LONG value, LONG expected);
LONG InterlockedExchangeAdd(volatile LONG *target, LONG value);
LONG InterlockedIncrement(volatile LONG *target);
int QueryPerformanceCounter(LARGE_INTEGER *value);
int QueryPerformanceFrequency(LARGE_INTEGER *value);
void GetLocalTime(SYSTEMTIME *value);
void Sleep(DWORD ms);
HANDLE CreateThread(void *attributes, size_t stack_size, DWORD (*entry)(void *), void *parameter,
                    DWORD flags, DWORD *thread_id);
HANDLE CreateEventA(void *attributes, int manual_reset, int initial_state, const char *name);
int SetEvent(HANDLE event);
DWORD WaitForSingleObject(HANDLE handle, DWORD ms);
int CloseHandle(HANDLE handle);
HANDLE FindFirstFileA(const char *pattern, WIN32_FIND_DATAA *entry);
int FindNextFileA(HANDLE handle, WIN32_FIND_DATAA *entry);
int FindClose(HANDLE handle);
DWORD GetFileAttributesA(const char *path);
int CreateDirectoryA(const char *path, void *attributes);
DWORD GetFullPathNameA(const char *path, DWORD size, char *output, char **file_part);
DWORD GetEnvironmentVariableA(const char *name, char *output, DWORD size);
int strncpy_s(char *destination, size_t size, const char *source, size_t count);
int strncat_s(char *destination, size_t size, const char *source, size_t count);
int fopen_s(FILE **file, const char *path, const char *mode);

/* The runtime's former SDL dependency here was only threading, ticks and
 * byte-file I/O. Keep those operations explicit rather than depending on a
 * desktop window library in the native runtime. */
typedef pthread_mutex_t KhdaysPosixMutex;
typedef HANDLE KhdaysPosixThread;
KhdaysPosixMutex *khdays_posix_mutex_create(void);
void khdays_posix_mutex_lock(KhdaysPosixMutex *mutex);
void khdays_posix_mutex_unlock(KhdaysPosixMutex *mutex);
KhdaysPosixThread khdays_posix_thread_create(int (*entry)(void *), const char *name, void *parameter);
void khdays_posix_thread_wait(KhdaysPosixThread thread, int *result);
uint64_t khdays_posix_ticks_ns(void);
void *khdays_posix_load_file(const char *path, size_t *size);
int khdays_posix_save_file(const char *path, const void *data, size_t size);

#endif
