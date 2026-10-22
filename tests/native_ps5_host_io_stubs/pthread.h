/* Deterministic single-thread backend for the host I/O unit test only. */
#ifndef KHDAYS_TEST_PTHREAD_H
#define KHDAYS_TEST_PTHREAD_H
#include <assert.h>
#include <stddef.h>
typedef int pthread_mutex_t;
typedef int pthread_t;
#define PTHREAD_MUTEX_INITIALIZER 0
extern int khdays_test_thread_create_calls;
extern int khdays_test_thread_join_calls;
extern int khdays_test_thread_error;
static int pthread_mutex_lock(pthread_mutex_t *lock)
{
    assert(*lock == 0); /* Detect accidental recursive locking in this backend. */
    *lock = 1;
    return 0;
}
static int pthread_mutex_unlock(pthread_mutex_t *lock)
{
    assert(*lock == 1);
    *lock = 0;
    return 0;
}
static int pthread_create(pthread_t *thread, const void *attrs, void *(*entry)(void *), void *arg)
{
    (void)attrs; (void)entry; (void)arg;
    ++khdays_test_thread_create_calls;
    *thread = 1;
    return khdays_test_thread_error;
}
static int pthread_join(pthread_t thread, void **result)
{
    (void)thread; (void)result;
    ++khdays_test_thread_join_calls;
    return 0;
}
#endif
