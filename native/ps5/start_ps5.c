/* PS5 counterpart of khdays-port v0.1.1 runtime/core/start.c.
 * This boots the recompiled game, including its real crt0/autoload sequence;
 * it does not enter the earlier prototype gameplay or smoke test. */
#include "start_ps5.h"
#include "diag_ps5.h"
#include "paths_ps5.h"
#include "context.h"
#include "events.h"
#include "host.h"
#include "instance.h"
#include "io.h"
#include "movie_hd.h"
#include "net_session.h"
#include "rom.h"
#include "runtime.h"
#include "session_log.h"
#include "sound_bridge.h"
#include "threads.h"
#include "window.h"
#include "platform.h"
#include "khdays_data_area.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

extern void khdays_NitroMain(void);
extern void _fp_init(void);
extern void NitroStartUp(void);
extern void __call_static_initializers(void);
extern void AutoloadCallback(void);
extern void MIi_UncompressBackward(KhdaysRef bottom);
extern void khdays_itcm_keep(void);
extern void khdays_overlays_reset(void);

#define MODULE_PARAMS ((volatile u32 *)KHDAYS_W(0x02000b68u))

static u32 reset_parameter;
static int boot_entered;
static char boot_report_path[1200];
static void run_game(void *reset);

static void boot_step(const char *step)
{
    fprintf(stderr, "khdays-native boot: %s\n", step);
    fflush(stderr);
    if (boot_report_path[0] != '\0') {
        FILE *report = fopen(boot_report_path, "a");
        if (report != NULL) {
            fprintf(report, "%s\n", step);
            fclose(report);
        }
    }
}

static u32 le32(const u8 *at)
{
    return (u32)at[0] | ((u32)at[1] << 8) |
           ((u32)at[2] << 16) | ((u32)at[3] << 24);
}

static int within(u32 start, u32 size, u32 base, u32 end)
{
    return start >= base && start <= end && size <= end - start;
}

static int boot_memory(u32 start, u32 size)
{
    return within(start, size, 0x01ff8000u, 0x02400000u) ||
           within(start, size, 0x027e0000u, 0x027e4000u);
}

static void require_boot(int condition, const char *what)
{
    if (!condition) {
        khdays_hal_unimplemented(what);
    }
}

static void clear32(u32 value, u32 address, u32 size)
{
    volatile u32 *destination = (volatile u32 *)KHDAYS_W(address);
    require_boot(((address | size) & 3u) == 0, "unaligned crt0 clear range");
    for (u32 i = 0; i < size / 4u; ++i) {
        destination[i] = value;
    }
}

static void load_static_module(void)
{
    const u8 *header = khdays_rom_header();
    const u32 rom = le32(header + 0x20);
    const u32 ram = le32(header + 0x28);
    const u32 size = le32(header + 0x2c);
    require_boot(ram == 0x02000000u && size >= 0xb80u &&
                     within(ram, size, 0x02000000u, 0x02400000u),
                 "ARM9 load range does not match the compiled YKGP game");
    khdays_rom_read(rom, (u8 *)KHDAYS_W(ram), size);
}

static void autoload(void)
{
    const u32 begin = MODULE_PARAMS[0], end = MODULE_PARAMS[1];
    u32 source = MODULE_PARAMS[2];
    require_boot(end >= begin && (end - begin) % 12u == 0 &&
                     (begin & 3u) == 0 && (source & 3u) == 0 &&
                     within(begin, end - begin, 0x02000000u, 0x02400000u),
                 "invalid crt0 autoload table");
    for (u32 at = begin; at < end; at += 12u) {
        const volatile u32 *info = (const volatile u32 *)KHDAYS_W(at);
        const u32 destination = info[0], bytes = info[1], bss = info[2];
        require_boot(bytes <= ~0u - bss && ((destination | bytes | bss) & 3u) == 0 &&
                         boot_memory(destination, bytes + bss) &&
                         within(source, bytes, 0x02000000u, 0x02400000u),
                     "invalid crt0 autoload block");
        memcpy(KHDAYS_W(destination), KHDAYS_W(source), bytes);
        clear32(0, destination + bytes, bss);
        source += bytes;
    }
    AutoloadCallback();
}

/* Reset uses a fresh execution context while every game's old thread is
 * discarded. The save chip lives outside these reset ranges. */
static void reset_boot(void *unused)
{
    (void)unused;
    khdays_threads_reset();
    khdays_sound_bridge_stop();
    khdays_net_end();
    khdays_movie_hd_reset();
    memset(KHDAYS_W(0x02000000u), 0, 0x400000u);
    khdays_heaps_reset();
    memset(KHDAYS_W(0x03000000u), 0, 0x8000u);
    memset(KHDAYS_W(KHDAYS_DATA_AREA), 0, KHDAYS_DATA_AREA_END - KHDAYS_DATA_AREA);
    khdays_overlays_reset();
    khdays_events_reset();
    khdays_boot_reset_parameter(reset_parameter);
    khdays_boot_environment();
    khdays_io_reset();
    run_game((void *)1);
}

void khdays_reset_system(u32 parameter)
{
    KhdaysContext *boot;
    khdays_log("reset: restarting the game with parameter 0x%08x", parameter);
    reset_parameter = parameter;
    boot = khdays_context_create(reset_boot, NULL);
    require_boot(boot != NULL, "no context for the boot after a reset");
    khdays_context_switch(boot);
    khdays_hal_unimplemented("a thread of the game that reset ran again");
}

int khdays_ps5_boot(int argc, char **argv)
{
    KhdaysContext *game;
    if (boot_entered) {
        fprintf(stderr, "khdays-native: the runtime boot was already entered\n");
        return 1;
    }
    boot_entered = 1;
    khdays_diag_init();
    /* Start stderr-to-klog capture before the first memory/syscall setup so
     * failures during the earliest boot path can be diagnosed on-console. */
    platform_init("Kingdom Hearts 358/2 Days Native");
    fprintf(stderr, "khdays-native: entering PS5 boot\n");
    fflush(stderr);
    /* Creates the writable save/log directory before profile/session I/O. */
    if (khdays_host_player_slot() <= 0) {
        return 1;
    }
    snprintf(boot_report_path, sizeof(boot_report_path), "%s/boot.log", khdays_ps5_save_root());
    FILE *initial_report = fopen(boot_report_path, "w");
    if (initial_report != NULL) fclose(initial_report);
    if (!khdays_ps5_diag_attach_log(boot_report_path))
        boot_step("WARNING: could not attach the persistent crash descriptor");
    boot_step("native-vulkan-019: mapping the DS window using PS5 direct memory");
    /* Reserve the window before host graphics/audio allocation. The save
     * directory and checkpoint file do not require game memory. */
    if (!khdays_memory_map()) {
        boot_step("FAILED: PS5 memory map; see klog for the native error code");
        return 1;
    }
    boot_step("direct memory ready; validating the extracted cartridge");
    if (!khdays_rom_init()) {
        boot_step("FAILED: extracted cartridge data was rejected");
        return 1;
    }
    boot_step("cartridge ready; starting runtime, VideoOut, input and audio");
    khdays_net_settings_source(khdays_settings_server);
    if (!khdays_runtime_init(argc, argv)) {
        boot_step("FAILED: runtime/VideoOut/input/audio initialization");
        khdays_host_close();
        return 1;
    }
    boot_step("runtime ready; creating the game's execution context");
    khdays_log_open();
    khdays_log("PS5 native runtime: data %s, saves %s", khdays_ps5_data_root(),
               khdays_ps5_save_root());
    khdays_log("the game's reference window is at %p", (void *)khdays_window);
    game = khdays_context_create(run_game, NULL);
    require_boot(game != NULL, "no context for the game");
    boot_step("switching to the game's execution context");
    khdays_context_switch(game);
    khdays_hal_unimplemented("the game's context switched back to the process's");
}

static void run_game(void *reset)
{
    u32 bss_start, bss_end;
    boot_step("loading ARM9");
    load_static_module();
    IO32(0x208) = 0x04000000u;
    clear32(0, 0x027e0000u, 0x4000u);
    clear32(0, 0x05000000u, 0x400u);
    clear32(0x0200u, 0x07000000u, 0x400u);
    if (MODULE_PARAMS[5] != 0) {
        require_boot(MODULE_PARAMS[5] >= 0x02000008u &&
                         MODULE_PARAMS[5] <= 0x02400000u,
                     "invalid compressed ARM9 module range");
        MIi_UncompressBackward(MODULE_PARAMS[5]);
    }
    boot_step("running ARM9 autoloads");
    autoload();
    bss_start = MODULE_PARAMS[3];
    bss_end = MODULE_PARAMS[4];
    require_boot(bss_end >= bss_start &&
                     within(bss_start, bss_end - bss_start, 0x02000000u, 0x02400000u),
                 "invalid static ARM9 BSS range");
    clear32(0, bss_start, bss_end - bss_start);
    boot_step("initializing native game data");
    khdays_data_init(-1);
    khdays_data_init(-2);
    khdays_data_init(-3);
    khdays_data_init(-4);
    khdays_itcm_keep();
    if (reset != NULL) {
        khdays_sound_bridge_restart();
    } else {
        (void)khdays_sound_bridge_start();
    }
    *(volatile u32 *)KHDAYS_W(0x027fff9cu) = 0;
    *(volatile u32 *)KHDAYS_W(0x027e3ffcu) = 0x01ff8148u;
    _fp_init();
    NitroStartUp();
    __call_static_initializers();
    boot_step("entering NitroMain");
    khdays_NitroMain();
    khdays_hal_unimplemented("NitroMain returned");
}
