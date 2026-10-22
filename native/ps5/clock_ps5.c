/* PS5 monotonic clock for khdays-port v0.1.1's runtime contract. */
#include "clock.h"

#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <time.h>

static pthread_mutex_t state_lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t start_ns;
static uint64_t paused_at_ns;
static uint64_t paused_ns;
static unsigned reasons;
static int wake_pending;

static uint64_t counter_ns(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}
void khdays_clock_init(void)
{
    pthread_mutex_lock(&state_lock);
    start_ns = counter_ns();
    paused_at_ns = 0;
    paused_ns = 0;
    reasons = 0;
    wake_pending = 0;
    pthread_mutex_unlock(&state_lock);
}

u64 khdays_clock_cycles(void)
{
    uint64_t ticks;
    pthread_mutex_lock(&state_lock);
    ticks = (reasons ? paused_at_ns : counter_ns()) - paused_ns - start_ns;
    pthread_mutex_unlock(&state_lock);
    return ticks / 1000000000ull * KHDAYS_CLOCK_HZ +
           ticks % 1000000000ull * KHDAYS_CLOCK_HZ / 1000000000ull;
}

void khdays_clock_hold(unsigned reason, int on)
{
    pthread_mutex_lock(&state_lock);
    if (on) {
        if (reasons == 0) {
            paused_at_ns = counter_ns();
        }
        reasons |= reason;
    } else if ((reasons & reason) != 0) {
        reasons &= ~reason;
        if (reasons == 0) {
            paused_ns += counter_ns() - paused_at_ns;
        }
    }
    pthread_mutex_unlock(&state_lock);
}

int khdays_clock_held(unsigned reason)
{
    int held;
    pthread_mutex_lock(&state_lock);
    held = (reasons & reason) != 0;
    pthread_mutex_unlock(&state_lock);
    return held;
}

void khdays_clock_wake(void)
{
    pthread_mutex_lock(&state_lock);
    wake_pending = 1;
    pthread_mutex_unlock(&state_lock);
}

static int take_wake(void)
{
    int pending;
    pthread_mutex_lock(&state_lock);
    pending = wake_pending;
    wake_pending = 0;
    pthread_mutex_unlock(&state_lock);
    return pending;
}

static int clock_paused(void)
{
    int paused;
    pthread_mutex_lock(&state_lock);
    paused = reasons != 0;
    pthread_mutex_unlock(&state_lock);
    return paused;
}

static void sleep_until(u64 cycle, int wakeable)
{
    const struct timespec one_ms = {0, 1000000};
    for (;;) {
        const u64 now = khdays_clock_cycles();
        u64 remaining;
        if (now >= cycle || (wakeable && take_wake())) {
            return;
        }
        remaining = cycle - now;
        if (remaining > KHDAYS_CLOCK_HZ / 1000u || clock_paused()) {
            nanosleep(&one_ms, NULL);
        } else {
            sched_yield();
        }
    }
}

void khdays_clock_sleep_until(u64 cycle)
{
    sleep_until(cycle, 0);
}

void khdays_clock_sleep_until_or_woken(u64 cycle)
{
    sleep_until(cycle, 1);
}

