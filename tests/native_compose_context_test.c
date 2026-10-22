/* Pixel equivalence and lookup counts, with the actual upstream emulated-TLS
 * compositor as reference. No console FPS is inferred from this host test. */
#include "gfx/describe.h"
#include <assert.h>
#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <windows.h>

typedef struct EmuControl { uintptr_t size, align, index; void *value; } EmuControl;
static __declspec(thread) struct { EmuControl *control; void *value; } bindings[16];
static __declspec(thread) unsigned binding_count;
static unsigned long long tls_lookups;
void *__emutls_get_address(EmuControl *control)
{
    ++tls_lookups;
    for (unsigned i = 0; i < binding_count; ++i)
        if (bindings[i].control == control) return bindings[i].value;
    assert(binding_count < 16);
    void *value = _aligned_malloc(control->size, control->align < 16 ? 16 : control->align);
    assert(value);
    if (control->value) memcpy(value, control->value, control->size);
    else memset(value, 0, control->size);
    bindings[binding_count].control = control;
    bindings[binding_count++].value = value;
    return value;
}
void reference_compose(const uint32_t *, const KhdaysGfxMemory *, uint32_t *);
void reference_rect(const uint32_t *, const KhdaysGfxMemory *, uint32_t *, int, int, int, int);
uint32_t reference_pixel(const uint32_t *, const KhdaysGfxMemory *, int, int);
void reference_wide(uint32_t *, const KhdaysGfxMemory *, int);

static uint32_t desc[KHDAYS_DESC_WORDS], reference[256 * 192], actual[256 * 192];
static uint8_t palette[0x800], memory_bytes[0x80000];
static uint32_t layer3d[256 * 192];
static KhdaysVramPages pages;
static KhdaysGfxMemory memory;

static void initialize(void)
{
    memory.vram = &pages; memory.palette = palette; memory.layer3d = layer3d;
    for (unsigned n = 0; n < sizeof(memory_bytes); ++n) memory_bytes[n] = (uint8_t)(n * 17u + 3u);
    for (unsigned n = 0; n < 0x400; ++n) {
        unsigned color = n * 109u & 0x7fff;
        palette[2 * n] = (uint8_t)color; palette[2 * n + 1] = (uint8_t)(color >> 8);
    }
    for (unsigned area = 0; area < KHDAYS_AREA_COUNT; ++area)
        for (unsigned page = 0; page < KHDAYS_AREA_MAX_PAGES; ++page)
            pages.page[area][page] = memory_bytes + page * 0x4000;
    for (unsigned n = 0; n < 256 * 192; ++n) layer3d[n] = 0x1f03093fu;
    for (unsigned n = 0; n < 4; ++n) memory.bank[n] = memory_bytes;
}

static void compare(void)
{
    reference_compose(desc, &memory, reference);
    unsigned long long before = tls_lookups;
    khdays_gfx_compose_cpu(desc, &memory, actual);
    assert(tls_lookups == before); /* no per-pixel TLS in the adapted path */
    assert(!memcmp(reference, actual, sizeof(actual)));
    for (int y = -1; y <= 192; y += 11)
        for (int x = -1; x <= 256; x += 13)
            assert(reference_pixel(desc, &memory, x, y) == khdays_gfx_pixel_cpu(desc, &memory, x, y));
    memset(reference, 0xa5, sizeof(reference)); memset(actual, 0xa5, sizeof(actual));
    reference_rect(desc, &memory, reference, 17, 31, 135, 165);
    khdays_gfx_compose_cpu_rect(desc, &memory, actual, 17, 31, 135, 165);
    assert(!memcmp(reference, actual, sizeof(actual)));
}

static DWORD WINAPI threaded_pixels(void *argument)
{
    const unsigned color = (unsigned)(uintptr_t)argument;
    uint32_t own[KHDAYS_DESC_WORDS] = {0};
    own[KD_FLAGS] = 1; own[KD_BACKDROP] = color;
    const uint32_t expected = khdays_gfx_pixel_cpu(own, &memory, 0, 0);
    for (unsigned i = 0; i < 30000; ++i)
        assert(khdays_gfx_pixel_cpu(own, &memory, i & 255u, i % 192u) == expected);
    return 0;
}

int main(void)
{
    initialize();
    for (unsigned engine = 0; engine < 2; ++engine) {
        memset(desc, 0, sizeof(desc));
        desc[KD_FLAGS] = 1u | (engine ? KDF_ENGINE_B : 0);
        desc[KD_BACKDROP] = 0x2137;
        compare();
        desc[KD_FLAGS] |= 0xfu << KDF_BG_ON_SHIFT | KDF_OBJ_ON;
        for (unsigned b = 0; b < 4; ++b) {
            unsigned base = KD_LAYERS + b * KD_LAYER_WORDS;
            desc[base + KDL_KIND] = KDK_TEXT | KDL_256COLOURS | b << 8;
            desc[base + KDL_WIDTH] = desc[base + KDL_HEIGHT] = 256;
            desc[base + KDL_MAP] = 0x10000 + b * 0x800;
        }
        for (unsigned s = 0; s < 32; ++s) {
            unsigned base = KD_SPRITES + s * KD_SPRITE_WORDS;
            desc[base + KDS_X] = s * 9u & 255u;
            desc[base + KDS_BOX] = (s * 5u % 160u) | 16u << 8 | 16u << 16;
            desc[base + KDS_SIZE] = 16u | 16u << 8 | KDS_SHOWN;
            desc[base + KDS_TILE] = s;
        }
        compare();
        desc[KD_MASTER] = 0x4008; compare();
        desc[KD_MASTER] = 0x8008; compare();
        desc[KD_MASTER] = 0;
        desc[KD_FLAGS] |= KDF_BG0_IS_3D;
        desc[KD_LAYERS + KDL_KIND] = KDK_3D;
        compare();
        desc[KD_FLAGS] |= KDF_SPLIT; compare();
        desc[KD_FLAGS] &= ~KDF_SPLIT;
        uint32_t wide_reference[KHDAYS_DESC_WORDS];
        memcpy(wide_reference, desc, sizeof(desc));
        reference_wide(wide_reference, &memory, 384);
        khdays_gfx_describe_wide(desc, &memory, 384);
        assert(!memcmp(desc, wide_reference, sizeof(desc)));
    }
    clock_t began = clock();
    tls_lookups = 0;
    for (int i = 0; i < 3; ++i) reference_compose(desc, &memory, reference);
    double original_ms = (double)(clock() - began) * 1000 / CLOCKS_PER_SEC / 3;
    unsigned long long original_lookups = tls_lookups / 3;
    began = clock(); tls_lookups = 0;
    for (int i = 0; i < 3; ++i) khdays_gfx_compose_cpu(desc, &memory, actual);
    double adapted_ms = (double)(clock() - began) * 1000 / CLOCKS_PER_SEC / 3;
    assert(tls_lookups == 0 && original_lookups > 49152);
    HANDLE threads[2] = {
        CreateThread(NULL, 0, threaded_pixels, (void *)(uintptr_t)0x001f, 0, NULL),
        CreateThread(NULL, 0, threaded_pixels, (void *)(uintptr_t)0x7c00, 0, NULL)
    };
    assert(threads[0] && threads[1]);
    assert(WaitForMultipleObjects(2, threads, TRUE, 30000) == WAIT_OBJECT_0);
    CloseHandle(threads[0]); CloseHandle(threads[1]);
    printf("context compositor: PASS, identical pixels/rectangles/3D/wide/fades/concurrent calls; "
           "TLS lookups/frame=%llu -> 0; host reference=%.2fms adapted=%.2fms (not PS5 FPS)\n",
           original_lookups, original_ms, adapted_ms);
    for (unsigned i = 0; i < binding_count; ++i) _aligned_free(bindings[i].value);
    return 0;
}
