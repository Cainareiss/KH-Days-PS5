/* Adapted from khdays-port runtime/core/window.c.
 * Copyright (c) 2026 Yokimitsuro and contributors. MIT; see ../UPSTREAM-LICENSE.
 * ELF table boundaries replace COFF sentinels; process exit uses POSIX. */
#include "game_tables.h"
#include "window.h"
#include "runtime.h"
#include "session_log.h"
#include <stdio.h>
#include <stdlib.h>

/* The mapping in memory_ps5.c owns this shared reference window. */
extern unsigned char *khdays_window;
_Noreturn void khdays_hal_unimplemented(const char *what);
extern void *khdays_native_at(unsigned int address);

static _Noreturn void stop(const char *what, uintptr_t value, const void *caller)
{
    const char *function = khdays_diag_function_at(caller);
    fprintf(stderr, "khdays-native: %s 0x%llx (from %s, %p): stopped\n",
            what, (unsigned long long)value, function, caller);
    khdays_log("stop: %s 0x%llx (from %s, %p)", what,
               (unsigned long long)value, function, caller);
    khdays_log_close();
    fflush(stderr);
    exit(5);
}

int khdays_game_ram_ref(unsigned int reference)
{
    return khdays_game_ram(reference);
}

KhdaysRef khdays_reference(const void *host)
{
    if (host == NULL) {
        return 0;
    }
    const uintptr_t address = (uintptr_t)host, base = (uintptr_t)khdays_window;
    if (address < base || address - base > UINT32_MAX) {
        stop("host address outside the reference window:", address,
             __builtin_return_address(0));
    }
    return (KhdaysRef)(address - base);
}

typedef struct khdays_ps5_function_record FunctionRecord;
static FunctionRecord *numbered;
static size_t numbered_count;

static int by_reference(const void *a, const void *b)
{
    const KhdaysRef x = ((const FunctionRecord *)a)->reference;
    const KhdaysRef y = ((const FunctionRecord *)b)->reference;
    return x < y ? -1 : x > y ? 1 : 0;
}

static void sort_numbered(void)
{
    size_t n = 0;
    for (const FunctionRecord *r = __start_khdays_functions;
         r < __stop_khdays_functions; ++r) {
        n += r->function != NULL;
    }
    numbered = malloc((n ? n : 1) * sizeof(*numbered));
    if (numbered == NULL) {
        khdays_hal_unimplemented("no memory for the function references' table");
    }
    for (const FunctionRecord *r = __start_khdays_functions;
         r < __stop_khdays_functions; ++r) {
        if (r->function != NULL) {
            numbered[numbered_count++] = *r;
        }
    }
    qsort(numbered, numbered_count, sizeof(*numbered), by_reference);
}

#define CACHED 4096u
#define ITCM_BASE 0x01ff8000u
#define ITCM_END 0x02000000u
static struct {
    KhdaysRef reference;
    unsigned generation;
    khdays_function_pointer function;
} cached[CACHED];
static unsigned generation = 1;

void khdays_function_references_moved(void)
{
    ++generation;
    if (generation == 0) {
        /* Invalidate even after an unsigned generation counter wraps. */
        for (size_t i = 0; i < CACHED; ++i) {
            cached[i].generation = 0;
        }
        generation = 1;
    }
}

khdays_function_pointer khdays_function_lookup(KhdaysRef reference)
{
    const unsigned slot = (reference >> 1) & (CACHED - 1);
    if (reference && cached[slot].reference == reference &&
        cached[slot].generation == generation) {
        return cached[slot].function;
    }
    if (numbered == NULL) {
        sort_numbered();
    }
    size_t lo = 0, hi = numbered_count;
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (numbered[mid].reference < reference) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    const khdays_function_pointer native =
        lo < numbered_count && numbered[lo].reference == reference
        ? numbered[lo].function
        : (khdays_function_pointer)khdays_native_at(reference);
    if (native != NULL && !(reference >= ITCM_BASE && reference < ITCM_END)) {
        cached[slot].reference = reference;
        cached[slot].generation = generation;
        cached[slot].function = native;
    }
    return native;
}

khdays_function_pointer khdays_function(KhdaysRef reference)
{
    const khdays_function_pointer native = khdays_function_lookup(reference);
    if (native == NULL) {
        stop("function reference with no native function:", reference,
             __builtin_return_address(0));
    }
    return native;
}
