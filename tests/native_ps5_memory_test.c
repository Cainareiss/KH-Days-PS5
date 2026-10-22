/* Exercise production memory routing with mocked device APIs. No console
 * allocation success is claimed. Fake addresses are never dereferenced. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *test_memset(void *destination, int value, size_t bytes)
{
    assert(value == 0xff && bytes == 0x10000u);
    return destination;
}
#define memset test_memset
#include "../native/ps5/memory_ps5.c"
#undef memset

static int reserve_error, create_error, commit_error, view_error, wrong_view;
static int ds_memory, commits, views, decommits;
static uintptr_t ds_base;
static void *(*stack_allocate)(size_t);
static void (*stack_release)(void *, size_t);
static const uintptr_t fake_base = UINT64_C(0x1000000000);

int khdays_game_heap_ds_memory(void) { return ds_memory; }
void khdays_snd7_native_set_ds_base(uintptr_t base) { ds_base = base; }
void khdays_context_stack_source(void *(*allocate)(size_t), void (*release)(void *, size_t))
{
    stack_allocate = allocate; stack_release = release;
}
int ps5_vrange_reserve(size_t bytes, void *hint, size_t alignment, void **out)
{
    assert(bytes == 0x80000000u && hint == NULL && alignment == 0x10000u);
    *out = (void *)fake_base;
    return reserve_error;
}
int ps5_shm_create(size_t bytes, struct ps5_shm *object)
{
    assert(bytes == 0x400000u);
    object->bytes = bytes; object->direct_start = 0x100000;
    return create_error;
}
int ps5_shm_map(const struct ps5_shm *object, size_t offset, size_t bytes,
                void *address, int protection, unsigned flags, void **out)
{
    assert(object == &main_ram && object->bytes == 0x400000u);
    assert(offset + bytes <= object->bytes && offset % 0x4000u == 0 && bytes % 0x4000u == 0);
    assert(protection == (PS5_SHM_READ | PS5_SHM_WRITE) && flags == PS5_SHM_FIXED);
    ++views;
    *out = wrong_view ? (unsigned char *)address + 0x10000u : address;
    return view_error;
}
int ps5_vrange_commit(void *address, size_t bytes, int protection)
{
    const uintptr_t offset = (uintptr_t)address - fake_base;
    assert(offset % 0x10000u == 0 && bytes % 0x10000u == 0);
    assert(offset + bytes <= 0x80000000u);
    assert(protection == (PS5_SHM_READ | PS5_SHM_WRITE));
    ++commits;
    return commit_error;
}
int ps5_vrange_decommit(void *address, size_t bytes)
{
    assert((uintptr_t)address == fake_base + KHDAYS_HEAPS_BASE);
    assert(bytes == KHDAYS_HEAPS_END - KHDAYS_HEAPS_BASE);
    ++decommits;
    return 0;
}
int32_t sceKernelMprotect(const void *address, size_t bytes, int protection)
{
    const uintptr_t offset = (uintptr_t)address - fake_base;
    assert(offset == 0x08000000u || offset == 0x0a000000u);
    assert(bytes == 0x10000u && protection == PS5_KERNEL_PROT_CPU_READ);
    return 0;
}

int main(void)
{
    assert(khdays_memory_map());
    assert(ds_base == fake_base && stack_allocate && stack_release);
    assert(views == 3 && khdays_io_host == at(KHDAYS_IO_VIEW));
    assert(khdays_heaps_committed() == 0x60000000u);
    const int before = commits;
    void *stack = stack_allocate(0x1000u);
    assert((uintptr_t)stack == fake_base + STACKS_BASE + GRANULE);
    assert(commits == before + 1);
    stack_release(stack, 0x1000u);
    assert(stack_allocate(0x1000u) == stack && commits == before + 1);
    khdays_heaps_reset();
    assert(decommits == 1 && commits == before + 2);
    ds_memory = 1; views = 0;
    assert(khdays_memory_map() && views == 5);
    reserve_error = -7; assert(!khdays_memory_map()); reserve_error = 0;
    create_error = -8; assert(!khdays_memory_map()); create_error = 0;
    view_error = -9; assert(!khdays_memory_map()); view_error = 0;
    wrong_view = 1; assert(!khdays_memory_map()); wrong_view = 0;
    commit_error = -10; assert(!khdays_memory_map());
    puts("native PS5 memory routing: PASS (direct mappings, mirrors, stacks, reset, failures)");
    return 0;
}
