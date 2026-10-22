/* Test-only single-thread lock for the Windows parser/compositor harness. */
#ifndef KHDAYS_SUBTITLE_TEST_PTHREAD_H
#define KHDAYS_SUBTITLE_TEST_PTHREAD_H
typedef int pthread_mutex_t;
#define PTHREAD_MUTEX_INITIALIZER 0
static inline int pthread_mutex_lock(pthread_mutex_t *lock) { (void)lock; return 0; }
static inline int pthread_mutex_unlock(pthread_mutex_t *lock) { (void)lock; return 0; }
#endif
