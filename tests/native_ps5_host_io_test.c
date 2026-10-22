/* Exercise the real adapter with an in-memory pad and audio device. The
 * pthread stub is deliberately single-threaded: device/lifecycle semantics
 * are checked here; thread scheduling and console APIs need a console run. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
static int test_nanosleep(const struct timespec *requested, struct timespec *remaining)
{
    (void)requested; (void)remaining;
    return 0;
}
#define nanosleep test_nanosleep
#include "../native/ps5/host_io_ps5.c"

int khdays_test_thread_create_calls, khdays_test_thread_join_calls, khdays_test_thread_error;
static struct pad test_pad;
static double test_time = 1.0;
static audio_fill_fn device_fill;
static void *device_user;
static int device_starts, device_stops, device_success = 1;
static int bridge_rate, bridge_ds, bridge_advance_calls;
static unsigned pad_reads;
static unsigned layout_changes;
void khdays_ps5_video_cycle_layout(void) { ++layout_changes; }

bool pad_open(void) { assert(input_lock == 1); return true; }
void pad_poll(struct pad *out)
{
    assert(input_lock == 1); /* Platform mutable buffers share snapshot's lock. */
    ++pad_reads;
    *out = test_pad;
}
double now_seconds(void) { return test_time; }
bool audio_start(audio_fill_fn fill, void *user)
{
    ++device_starts;
    device_fill = fill;
    device_user = user;
    return device_success != 0;
}
void audio_stop(void) { ++device_stops; }
u64 khdays_clock_cycles(void) { return 100; }
void khdays_sound_bridge_advance(u64 cycle) { (void)cycle; ++bridge_advance_calls; }
void khdays_sound_bridge_set_output(int ds, int rate) { bridge_ds = ds; bridge_rate = rate; }
void khdays_sound_bridge_set_volumes(int a, int b, int c, int d, int e)
{
    (void)a; (void)b; (void)c; (void)d; (void)e;
}

static void test_input(void)
{
    assert(khdays_ps5_host_io_init());
    assert(khdays_ps5_host_io_init());
    assert(device_starts == 1 && bridge_rate == 48000 && bridge_ds == 0);
    test_pad = (struct pad){.held = PAD_CIRCLE | PAD_CROSS | PAD_OPTIONS | PAD_L3,
                            .left_x = -1, .right_y = 1, .r2 = 1};
    khdays_ps5_host_io_poll();
    KhdaysInputSnapshot out;
    khdays_host_input(&out);
    assert(out.ds_buttons == (KHDAYS_KEY_A | KHDAYS_KEY_B | KHDAYS_KEY_START |
                              KHDAYS_KEY_SELECT | KHDAYS_KEY_LEFT));
    assert(out.ds_buttons_field == out.ds_buttons);
    assert(out.move_x == -32767 && out.camera_y == 32767);
    assert(out.actions == KHDAYS_ACTION_TARGET_NEXT);
    assert(khdays_host_buttons() == out.ds_buttons);
    test_pad.held = PAD_L3 | PAD_R3;
    khdays_host_input(&out);
    assert(layout_changes == 1 && !(out.ds_buttons & KHDAYS_KEY_SELECT));
    assert(!(out.actions & KHDAYS_ACTION_LOCK_ON));
    khdays_host_input(&out);
    assert(layout_changes == 1); /* held chord is not autorepeated */
    test_pad = (struct pad){.held = PAD_R3};
    khdays_host_input(&out);
    assert(out.actions == KHDAYS_ACTION_LOCK_ON && layout_changes == 1);
    khdays_ps5_host_io_enhancements(KHDAYS_ENH_DPAD_COMMAND_MENU);
    test_pad = (struct pad){.held = PAD_UP};
    khdays_ps5_host_io_poll();
    khdays_host_input(&out);
    assert(out.ds_buttons == KHDAYS_KEY_UP && out.ds_buttons_field == 0);
    assert(out.actions == KHDAYS_ACTION_MENU_UP && out.move_x == 0);
    test_pad = (struct pad){.held = PAD_TOUCH_PAD, .right_x = 1, .right_y = -1};
    test_time += 1; /* clamps a delayed host frame to 50 ms */
    khdays_ps5_host_io_poll();
    int x, y;
    assert(khdays_host_touch(&x, &y));
    assert(x == 137 && y == 86);
    khdays_ps5_host_io_pause(1);
    khdays_ps5_host_io_poll();
    khdays_host_input(&out);
    assert(out.ui_open && out.ds_buttons == 0 && !out.touching);
    khdays_ps5_host_io_pause(0);
}

static void test_audio(void)
{
    float input[] = {2.0f, -2.0f, 0.5f, NAN};
    int16_t output[2 * (AUDIO_LEAD + 1500)];
    khdays_host_audio_float(input, 2);
    device_fill(output, AUDIO_LEAD + 2, device_user);
    for (unsigned i = 0; i < AUDIO_LEAD * 2; ++i) assert(output[i] == 0);
    assert(output[AUDIO_LEAD * 2] == 32767);
    assert(output[AUDIO_LEAD * 2 + 1] == -32768);
    assert(output[AUDIO_LEAD * 2 + 2] == 16383);
    assert(output[AUDIO_LEAD * 2 + 3] == 0);

    int16_t source[2000], whole[3000];
    for (unsigned i = 0; i < 2000; ++i) source[i] = (int16_t)(i * 11 - 10000);
    reset_audio();
    khdays_host_audio(source, 1000);
    const unsigned produced = ring_count - AUDIO_LEAD;
    assert(produced == 1466);
    device_fill(output, AUDIO_LEAD + produced, device_user);
    memcpy(whole, output + AUDIO_LEAD * 2, produced * 2 * sizeof(int16_t));
    reset_audio();
    khdays_host_audio(source, 271);
    khdays_host_audio(source + 542, 729);
    assert(ring_count - AUDIO_LEAD == produced);
    device_fill(output, AUDIO_LEAD + produced, device_user);
    assert(memcmp(whole, output + AUDIO_LEAD * 2, produced * 2 * sizeof(int16_t)) == 0);

    reset_audio();
    khdays_host_audio_float(input, 2);
    khdays_ps5_host_io_mute(1);
    device_fill(output, AUDIO_LEAD + 2, device_user);
    for (unsigned i = 0; i < (AUDIO_LEAD + 2) * 2; ++i) assert(output[i] == 0);
    khdays_ps5_host_io_mute(0);
    assert(ring_count == 0);
    khdays_host_audio_float(input, 2);
    assert(ring_count == AUDIO_LEAD + 2); /* underrun restores the lead */
}

/* No explicit poll/present occurs here: the game can sit on an unchanged
 * frame while the player presses/releases a button or uses the stylus. */
static void test_input_without_present(void)
{
    KhdaysInputSnapshot before, after;
    khdays_host_input(&before);
    unsigned reads = pad_reads;
    test_pad = (struct pad){.held = PAD_SQUARE | PAD_TRIANGLE};
    test_time += 1.0 / 60.0;
    khdays_host_input(&after);
    assert(pad_reads == reads + 1);
    assert(after.generation > before.generation);
    assert(after.ds_buttons == (KHDAYS_KEY_X | KHDAYS_KEY_Y));
    test_pad = (struct pad){0};
    assert(khdays_host_buttons() == 0); /* Releases are refreshed too. */

    int x, y;
    assert(!khdays_host_touch(&x, &y));
    const int old_x = x, old_y = y;
    test_pad = (struct pad){.held = PAD_TOUCH_PAD, .right_x = 1, .right_y = 1};
    test_time += 0.05;
    assert(khdays_host_touch(&x, &y));
    assert(x > old_x && y > old_y);
    test_pad = (struct pad){0};
    assert(!khdays_host_touch(&x, &y));

    khdays_ps5_host_io_pause(1);
    test_pad = (struct pad){.held = PAD_CIRCLE};
    khdays_host_input(&after);
    assert(after.ui_open && after.ds_buttons == 0);
    khdays_ps5_host_io_pause(0);
    assert(khdays_host_buttons() == KHDAYS_KEY_A);
}

static void test_lifecycle(void)
{
    khdays_host_sound_start();
    khdays_host_sound_start();
    assert(khdays_test_thread_create_calls == 1);
    assert(bridge_advance_calls == 0); /* the test backend never runs a worker */
    khdays_ps5_host_io_shutdown();
    assert(khdays_test_thread_join_calls == 1 && device_stops == 1);
    const unsigned reads = pad_reads;
    assert(khdays_host_buttons() == 0);
    KhdaysInputSnapshot closed;
    khdays_host_input(&closed);
    assert(closed.ds_buttons == 0 && closed.generation == 0);
    assert(!khdays_host_touch(NULL, NULL));
    assert(pad_reads == reads); /* Shutdown prevents further platform access. */
    khdays_ps5_host_io_shutdown();
    assert(khdays_test_thread_join_calls == 1 && device_stops == 1);
    khdays_host_sound_start();
    assert(khdays_test_thread_create_calls == 1);
    device_success = 0;
    assert(!khdays_ps5_host_io_init());
    khdays_test_thread_error = 12;
    khdays_host_sound_start();
    assert(!atomic_load(&sound_running) && !sound_joinable);
    khdays_ps5_host_io_shutdown();
}

int main(void)
{
    test_input();
    test_input_without_present();
    test_audio();
    test_lifecycle();
    puts("native PS5 host I/O: PASS (input without present, resampling, buffering, lifecycle)");
    return 0;
}
