/* PS5 memory window for khdays-port v0.1.1.
 * Keep the DS mirrors in a native virtual reservation, backed by direct
 * memory. POSIX anonymous/shared mappings charge the title's small flexible
 * budget, which cannot back the port's 1.5 GiB expanded heaps. */
#define _DEFAULT_SOURCE

#include "game_heap.h"
#include "io.h"
#include "runtime.h"
#include "window.h"
#include "context.h"
#include "snd7_native.h"
#include "khdays_data_area.h"

#include <ps5platform/kernel.h>
#include <ps5platform/shm.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAIN_RAM_SIZE 0x400000u
#define DTCM_BASE 0x027e0000u
#define GRANULE 0x10000u
#define ARM7_WRAM_BASE 0x037f8000u
#define ARM7_WRAM_END 0x03810000u
#define STACKS_BASE 0x0c000000u
#define STACKS_SIZE 0x10000000u
#define WINDOW_SIZE ((size_t)KHDAYS_HEAPS_END)

_Static_assert(STACKS_BASE + STACKS_SIZE <= KHDAYS_HEAPS_BASE,
               "the stacks area runs into the game's heaps");

unsigned char *khdays_window;
u8 *khdays_io_host;
u8 *khdays_io2_host;
static struct ps5_shm main_ram;
static int heaps_mapped;

static unsigned char *at(unsigned address)
{
    return khdays_window + address;
}
static int fail(const char *what, unsigned address, int result)
{
    fprintf(stderr, "memory map: %s at 0x%08x (host %p) failed: PS5 0x%08x\n",
            what, address, (void *)at(address), (unsigned)result);
    return 0;
}

static int map_anon(const char *what, unsigned address, unsigned size,
                    int protection)
{
    const unsigned base = address & ~(GRANULE - 1u);
    const unsigned end = (address + size + GRANULE - 1u) & ~(GRANULE - 1u);
    const int result = ps5_vrange_commit(at(base), end - base, protection);
    return result == 0 ? 1 : fail(what, base, result);
}

static int map_view(const struct ps5_shm *object, const char *what, unsigned address,
                    unsigned offset, unsigned size)
{
    void *mapped = NULL;
    const int result = ps5_shm_map(object, offset, size, at(address),
                                  PS5_SHM_READ | PS5_SHM_WRITE, PS5_SHM_FIXED, &mapped);
    return result == 0 && mapped == at(address) ? 1 :
           fail(what, address, result != 0 ? result : PS5_SHM_NO_PLACE);
}

static int empty_gba_slot(void)
{
    const unsigned addresses[] = {0x08000000u, 0x0a000000u};
    for (size_t i = 0; i < sizeof(addresses) / sizeof(addresses[0]); ++i) {
        if (!map_anon("empty GBA slot", addresses[i], GRANULE,
                      PS5_SHM_READ | PS5_SHM_WRITE)) {
            return 0;
        }
        memset(at(addresses[i]), 0xff, GRANULE);
        const int result = sceKernelMprotect(at(addresses[i]), GRANULE, PS5_KERNEL_PROT_CPU_READ);
        if (result != 0) {
            return fail("protect empty GBA slot", addresses[i], result);
        }
    }
    return 1;
}

static unsigned stacks_next = STACKS_BASE;
static struct {
    unsigned address;
    size_t size;
} *returned;
static size_t returned_count;
static size_t returned_room;

static void *window_stack(size_t size)
{
    const unsigned rounded =
        (unsigned)((size + GRANULE - 1u) & ~(size_t)(GRANULE - 1u));
    unsigned address;
    for (size_t i = 0; i < returned_count; ++i) {
        if (returned[i].size == rounded) {
            address = returned[i].address;
            returned[i] = returned[--returned_count];
            return at(address);
        }
    }
    if ((size_t)stacks_next + GRANULE + rounded >
        (size_t)STACKS_BASE + STACKS_SIZE) {
        fprintf(stderr, "memory map: the context stack area is full\n");
        return NULL;
    }
    address = stacks_next + GRANULE;
    if (!map_anon("a context stack", address, rounded,
                  PS5_SHM_READ | PS5_SHM_WRITE)) {
        return NULL;
    }
    stacks_next = address + rounded;
    return at(address);
}

static void window_stack_release(void *stack, size_t size)
{
    if (returned_count == returned_room) {
        const size_t room = returned_room ? returned_room * 2u : 16u;
        void *grown = realloc(returned, room * sizeof(*returned));
        if (grown == NULL) {
            return;
        }
        returned = grown;
        returned_room = room;
    }
    returned[returned_count].address =
        (unsigned)((unsigned char *)stack - khdays_window);
    returned[returned_count].size =
        (size + GRANULE - 1u) & ~(size_t)(GRANULE - 1u);
    ++returned_count;
}

unsigned long long khdays_heaps_committed(void)
{
    return heaps_mapped ? KHDAYS_HEAPS_END - KHDAYS_HEAPS_BASE : 0;
}

static int reserve_heaps(void)
{
    const int result = map_anon("the game's heaps", KHDAYS_HEAPS_BASE,
                                KHDAYS_HEAPS_END - KHDAYS_HEAPS_BASE,
                                PS5_SHM_READ | PS5_SHM_WRITE);
    heaps_mapped = result;
    return result;
}

void khdays_heaps_reset(void)
{
    const int result = ps5_vrange_decommit(at(KHDAYS_HEAPS_BASE),
                                          KHDAYS_HEAPS_END - KHDAYS_HEAPS_BASE);
    if (result != 0) {
        fail("reset the game's heaps", KHDAYS_HEAPS_BASE, result);
        abort();
    }
    heaps_mapped = 0;
    if (!reserve_heaps()) {
        abort();
    }
}

static int reserve_window(void)
{
    void *window = NULL;
    const int result = ps5_vrange_reserve(WINDOW_SIZE, NULL, GRANULE, &window);
    if (result != 0) {
        fprintf(stderr, "memory map: no 0x%llx-byte virtual window: PS5 0x%08x\n",
                (unsigned long long)WINDOW_SIZE, (unsigned)result);
        return 0;
    }
    khdays_window = (unsigned char *)window;
    khdays_snd7_native_set_ds_base((uintptr_t)khdays_window);
    khdays_context_stack_source(window_stack, window_stack_release);
    return 1;
}

int khdays_memory_map(void)
{
    if (!reserve_window()) {
        return 0;
    }
    const int result = ps5_shm_create(MAIN_RAM_SIZE, &main_ram);
    if (result != 0) {
        return fail("main RAM object", 0x02000000u, result);
    }
    if (!map_view(&main_ram, "main RAM", 0x02000000u, 0, MAIN_RAM_SIZE) ||
        !map_view(&main_ram, "main RAM mirror", 0x02400000u, 0,
                  DTCM_BASE - 0x02400000u) ||
        !map_view(&main_ram, "main RAM mirror shared area", DTCM_BASE + GRANULE,
                  DTCM_BASE + GRANULE - 0x02400000u,
                  0x02800000u - (DTCM_BASE + GRANULE))) {
        return 0;
    }
    if (khdays_game_heap_ds_memory()) {
        if (!map_view(&main_ram, "main RAM mirror", 0x02800000u, 0,
                      MAIN_RAM_SIZE) ||
            !map_view(&main_ram, "main RAM mirror", 0x02c00000u, 0,
                      MAIN_RAM_SIZE)) {
            return 0;
        }
    } else if (!map_anon("the headers' heaps", KHDAYS_HEADERS_BASE,
                         0x03000000u - KHDAYS_HEADERS_BASE,
                         PS5_SHM_READ | PS5_SHM_WRITE)) {
        return 0;
    }

    if (!(map_anon("ITCM", 0x01ff8000u, 0x8000u,
                   PS5_SHM_READ | PS5_SHM_WRITE) &&
          map_anon("DTCM", DTCM_BASE, 0x4000u,
                   PS5_SHM_READ | PS5_SHM_WRITE) &&
          map_anon("shared WRAM", 0x03000000u, 0x8000u,
                   PS5_SHM_READ | PS5_SHM_WRITE) &&
          map_anon("ARM7 WRAM", ARM7_WRAM_BASE,
                   ARM7_WRAM_END - ARM7_WRAM_BASE,
                   PS5_SHM_READ | PS5_SHM_WRITE) &&
          map_anon("palettes", 0x05000000u, 0x800u,
                   PS5_SHM_READ | PS5_SHM_WRITE) &&
          map_anon("VRAM", 0x06000000u, 0x008a4000u,
                   PS5_SHM_READ | PS5_SHM_WRITE) &&
          map_anon("OAM", 0x07000000u, 0x800u,
                   PS5_SHM_READ | PS5_SHM_WRITE) &&
          empty_gba_slot())) {
        return 0;
    }
    if (!(map_anon("the register model's memory", KHDAYS_IO_VIEW,
                   KHDAYS_IO_SIZE + KHDAYS_IO2_SIZE,
                   PS5_SHM_READ | PS5_SHM_WRITE) &&
          (KHDAYS_DATA_AREA_END == KHDAYS_DATA_AREA ||
           map_anon("the data area", KHDAYS_DATA_AREA,
                    KHDAYS_DATA_AREA_END - KHDAYS_DATA_AREA,
                    PS5_SHM_READ | PS5_SHM_WRITE)))) {
        return 0;
    }
    if (!reserve_heaps()) {
        return 0;
    }
    khdays_io_host = at(KHDAYS_IO_VIEW);
    khdays_io2_host = at(KHDAYS_IO2_VIEW);
    return 1;
}

