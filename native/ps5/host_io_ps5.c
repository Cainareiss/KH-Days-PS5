/* Native PS5 host input and sound for khdays-port v0.1.1.
 * MIT; see ../UPSTREAM-LICENSE.
 *
 * The public PS5 Vulkan Template platform owns scePad and sceAudioOut. This
 * adapter owns the snapshot, a bounded audio ring and the sound-bridge thread.
 * Neither the render loop nor the console's audio callback runs the bridge:
 * it can enter recompiled game code and must remain off those threads. */
#include "host_io_ps5.h"
#include "host.h"
#include "platform.h"
#include "clock.h"
#include "sound_bridge.h"
#include "sound_output.h"
#include "profile.h"
#include "runtime/input/input.h"
#include "sound/mix_ds.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define OUTPUT_RATE 48000u
#define AUDIO_LEAD (OUTPUT_RATE * 30u / 1000u)
#define AUDIO_LIMIT (OUTPUT_RATE / 10u)
#define AUDIO_CAPACITY 8192u

static pthread_mutex_t input_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t audio_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t lifecycle_lock = PTHREAD_MUTEX_INITIALIZER;
static KhdaysInputSnapshot snapshot;
static uint16_t enhancements = KHDAYS_ENH_ANALOG_CAMERA |
                               KHDAYS_ENH_ANALOG_MOVEMENT |
                               KHDAYS_ENH_SEPARATE_LOCK_ON;
static float touch_x = 128.0f, touch_y = 96.0f;
static double previous_poll;
static int layout_chord;
extern void khdays_ps5_video_cycle_layout(void);
static atomic_bool initialized;
static atomic_bool audio_open;
static atomic_bool paused;
static atomic_bool muted;
static atomic_bool sound_running;
static pthread_t sound_thread;
static int sound_joinable;
static int16_t ring[AUDIO_CAPACITY * 2u];
static unsigned ring_read, ring_count;
static int needs_lead = 1;
static int input_format; /* 0 unset, 1 DS PCM16, 2 native-rate float */
static int have_previous;
static int16_t previous_sample[2];
static unsigned resample_phase;

static float clamp_axis(float value)
{
    return value < -1.0f ? -1.0f : value > 1.0f ? 1.0f : value;
}

static int16_t axis_value(float value)
{
    value = clamp_axis(value);
    return (int16_t)(value * 32767.0f);
}

static int16_t pcm16(float value)
{
    /* A corrupt mod's NaN must not reach an undefined float-to-int cast. */
    if (value != value) return 0;
    if (value >= 1.0f) return INT16_MAX;
    if (value <= -1.0f) return INT16_MIN;
    return (int16_t)(value * 32767.0f);
}

void khdays_ps5_host_io_enhancements(uint16_t flags)
{
    pthread_mutex_lock(&input_lock);
    enhancements = flags & (KHDAYS_ENH_ANALOG_CAMERA | KHDAYS_ENH_SEPARATE_LOCK_ON |
                            KHDAYS_ENH_DPAD_COMMAND_MENU | KHDAYS_ENH_ANALOG_MOVEMENT);
    pthread_mutex_unlock(&input_lock);
}

void khdays_ps5_host_io_poll(void)
{
    struct pad pad = {0};
    KhdaysInputSnapshot next = {0};
    /* Both the render thread and the game's pad readers can request an
     * update. The platform pad service has mutable global buffers, so the
     * entire read and timestamp update must be serialized, not only publish. */
    pthread_mutex_lock(&input_lock);
    if (!atomic_load(&initialized)) {
        pthread_mutex_unlock(&input_lock);
        return;
    }
    pad_poll(&pad);
    const int chord = (pad.held & (PAD_L3 | PAD_R3)) == (PAD_L3 | PAD_R3);
    if (chord && !layout_chord) khdays_ps5_video_cycle_layout();
    layout_chord = chord;
    if (chord) pad.held &= ~(PAD_L3 | PAD_R3);
    const double now = now_seconds();
    double dt = previous_poll > 0.0 ? now - previous_poll : 1.0 / 60.0;
    previous_poll = now;
    if (dt < 0.0) dt = 0.0;
    if (dt > 0.05) dt = 0.05;

    next.enhancements = enhancements;
    next.generation = snapshot.generation + 1u;
    next.camera_speed = 100;
    next.run_threshold = 75;
    next.walk_speed = 50;
    if (pad.held & PAD_CIRCLE) next.ds_buttons |= KHDAYS_KEY_A;
    if (pad.held & PAD_CROSS) next.ds_buttons |= KHDAYS_KEY_B;
    if (pad.held & PAD_TRIANGLE) next.ds_buttons |= KHDAYS_KEY_X;
    if (pad.held & PAD_SQUARE) next.ds_buttons |= KHDAYS_KEY_Y;
    if (pad.held & PAD_OPTIONS) next.ds_buttons |= KHDAYS_KEY_START;
    if (pad.held & PAD_L3) next.ds_buttons |= KHDAYS_KEY_SELECT;
    if (pad.held & PAD_L1) next.ds_buttons |= KHDAYS_KEY_L;
    if (pad.held & PAD_R1) next.ds_buttons |= KHDAYS_KEY_R;
    if (pad.held & PAD_UP) next.ds_buttons |= KHDAYS_KEY_UP;
    if (pad.held & PAD_DOWN) next.ds_buttons |= KHDAYS_KEY_DOWN;
    if (pad.held & PAD_LEFT) next.ds_buttons |= KHDAYS_KEY_LEFT;
    if (pad.held & PAD_RIGHT) next.ds_buttons |= KHDAYS_KEY_RIGHT;

    next.ds_buttons_field = next.ds_buttons;
    if (enhancements & KHDAYS_ENH_DPAD_COMMAND_MENU) {
        next.ds_buttons_field &= (uint16_t)~(KHDAYS_KEY_UP | KHDAYS_KEY_DOWN |
                                           KHDAYS_KEY_LEFT | KHDAYS_KEY_RIGHT);
        if (pad.held & PAD_UP) next.actions |= KHDAYS_ACTION_MENU_UP;
        if (pad.held & PAD_DOWN) next.actions |= KHDAYS_ACTION_MENU_DOWN;
        if (pad.held & PAD_LEFT) next.actions |= KHDAYS_ACTION_MENU_LEFT;
        if (pad.held & PAD_RIGHT) next.actions |= KHDAYS_ACTION_MENU_RIGHT;
    }
    /* Stick directions also navigate non-field menus, which do not consume
     * the analog movement enhancement. Keep them in the field fallback too. */
    uint16_t stick = 0;
    if (pad.left_x < -0.3f) stick |= KHDAYS_KEY_LEFT;
    if (pad.left_x > 0.3f) stick |= KHDAYS_KEY_RIGHT;
    if (pad.left_y < -0.3f) stick |= KHDAYS_KEY_UP;
    if (pad.left_y > 0.3f) stick |= KHDAYS_KEY_DOWN;
    next.ds_buttons |= stick;
    next.ds_buttons_field |= stick;
    if (enhancements & KHDAYS_ENH_ANALOG_MOVEMENT) {
        next.move_x = axis_value(pad.left_x);
        next.move_y = axis_value(-pad.left_y);
    }
    if (enhancements & KHDAYS_ENH_SEPARATE_LOCK_ON) {
        if (pad.held & PAD_R3) next.actions |= KHDAYS_ACTION_LOCK_ON;
        if (pad.r2 > 0.5f) next.actions |= KHDAYS_ACTION_TARGET_NEXT;
        if (pad.l2 > 0.5f) next.actions |= KHDAYS_ACTION_TARGET_PREV;
    }
    /* The template's exported pad struct has no touch coordinates. Holding
     * its touchpad click makes the right stick a DS stylus (192 px/s), so
     * every touch-only menu remains reachable with public pad APIs. */
    if (pad.held & PAD_TOUCH_PAD) {
        touch_x += clamp_axis(pad.right_x) * (float)(dt * 192.0);
        touch_y += clamp_axis(pad.right_y) * (float)(dt * 192.0);
        if (touch_x < 0.0f) touch_x = 0.0f;
        if (touch_x > 255.0f) touch_x = 255.0f;
        if (touch_y < 0.0f) touch_y = 0.0f;
        if (touch_y > 191.0f) touch_y = 191.0f;
        next.touching = 1;
    } else if (enhancements & KHDAYS_ENH_ANALOG_CAMERA) {
        next.camera_x = axis_value(pad.right_x);
        next.camera_y = axis_value(pad.right_y);
    }
    next.touch_x = (uint16_t)touch_x;
    next.touch_y = (uint16_t)touch_y;
    if (atomic_load(&paused)) {
        const uint32_t generation = next.generation;
        memset(&next, 0, sizeof(next));
        next.generation = generation;
        next.ui_open = 1;
    }
    snapshot = next;
    pthread_mutex_unlock(&input_lock);
}

void khdays_host_input(KhdaysInputSnapshot *out)
{
    if (out == NULL) return;
    /* Upstream skips presenting identical frames, including title/menu
     * frames awaiting a button. Reading input must itself refresh the pad. */
    khdays_ps5_host_io_poll();
    pthread_mutex_lock(&input_lock);
    *out = snapshot;
    pthread_mutex_unlock(&input_lock);
}

uint16_t khdays_host_buttons(void)
{
    KhdaysInputSnapshot current;
    khdays_host_input(&current);
    return current.ds_buttons;
}

int khdays_host_touch(int *x, int *y)
{
    KhdaysInputSnapshot current;
    khdays_host_input(&current);
    if (x) *x = current.touch_x;
    if (y) *y = current.touch_y;
    return current.touching;
}

/* All ring operations below are under audio_lock; producer calls are already
 * serialized by the sound bridge's own lock. The audio callback never takes
 * that lock, keeping its 256-frame device deadline independent of gameplay. */
static void reset_audio(void)
{
    ring_read = ring_count = 0;
    needs_lead = 1;
    input_format = have_previous = 0;
    resample_phase = 0;
}

static void enqueue(int16_t left, int16_t right)
{
    if (ring_count == AUDIO_CAPACITY) return;
    const unsigned at = (ring_read + ring_count) % AUDIO_CAPACITY;
    ring[2u * at] = left;
    ring[2u * at + 1u] = right;
    ++ring_count;
}

static int prepare_audio(int format)
{
    if (!atomic_load(&audio_open) || atomic_load(&paused) || ring_count > AUDIO_LIMIT) return 0;
    if (input_format != format) {
        input_format = format;
        have_previous = 0;
        resample_phase = 0;
    }
    if (ring_count == 0) needs_lead = 1;
    if (needs_lead) {
        needs_lead = 0;
        for (unsigned i = 0; i < AUDIO_LEAD; ++i) enqueue(0, 0);
    }
    return 1;
}

static void audio_fill(int16_t *frames, int count, void *unused)
{
    (void)unused;
    if (frames == NULL || count <= 0) return;
    pthread_mutex_lock(&audio_lock);
    const int silent = atomic_load(&muted) || atomic_load(&paused);
    for (int i = 0; i < count; ++i) {
        if (ring_count != 0) {
            frames[2 * i] = silent ? 0 : ring[2u * ring_read];
            frames[2 * i + 1] = silent ? 0 : ring[2u * ring_read + 1u];
            ring_read = (ring_read + 1u) % AUDIO_CAPACITY;
            --ring_count;
        } else {
            frames[2 * i] = frames[2 * i + 1] = 0;
            needs_lead = 1;
        }
    }
    pthread_mutex_unlock(&audio_lock);
}

void khdays_host_audio(const int16_t *frames, int count)
{
    if (frames == NULL || count <= 0) return;
    pthread_mutex_lock(&audio_lock);
    if (prepare_audio(1)) {
        for (int i = 0; i < count; ++i) {
            const int16_t left = frames[2 * i], right = frames[2 * i + 1];
            if (have_previous) {
                while (resample_phase < OUTPUT_RATE) {
                    const float t = (float)resample_phase / (float)OUTPUT_RATE;
                    enqueue((int16_t)(previous_sample[0] + (left - previous_sample[0]) * t),
                            (int16_t)(previous_sample[1] + (right - previous_sample[1]) * t));
                    resample_phase += KHDAYS_SOUND_RATE;
                }
                resample_phase -= OUTPUT_RATE;
            }
            previous_sample[0] = left;
            previous_sample[1] = right;
            have_previous = 1;
        }
    }
    pthread_mutex_unlock(&audio_lock);
}

void khdays_host_audio_float(const float *frames, int count)
{
    if (frames == NULL || count <= 0) return;
    pthread_mutex_lock(&audio_lock);
    if (prepare_audio(2)) {
        for (int i = 0; i < count; ++i) enqueue(pcm16(frames[2 * i]), pcm16(frames[2 * i + 1]));
    }
    pthread_mutex_unlock(&audio_lock);
}

void khdays_sound_output_open(void)
{
    if (atomic_load(&audio_open)) return;
    pthread_mutex_lock(&audio_lock);
    reset_audio();
    pthread_mutex_unlock(&audio_lock);
    /* audio_start may immediately call audio_fill on the device's thread. */
    atomic_store(&audio_open, audio_start(audio_fill, NULL));
    if (!atomic_load(&audio_open)) fprintf(stderr, "khdays-native: PS5 audio output unavailable\n");
    khdays_sound_bridge_set_output(0, OUTPUT_RATE);
    khdays_sound_bridge_set_volumes(100, 100, 100, 100, 100);
}

void khdays_sound_output_set(const KhdaysSound *settings)
{
    if (settings == NULL) return;
    khdays_sound_bridge_set_output(settings->ds_exact != 0, OUTPUT_RATE);
    khdays_sound_bridge_set_volumes(settings->volume, settings->music, settings->effects,
                                    settings->voices, settings->movies);
}

void khdays_sound_output_close(void)
{
    if (atomic_exchange(&audio_open, 0)) audio_stop();
    pthread_mutex_lock(&audio_lock);
    reset_audio();
    pthread_mutex_unlock(&audio_lock);
}

void khdays_ps5_host_io_pause(int pause)
{
    atomic_store(&paused, pause != 0);
    pthread_mutex_lock(&audio_lock);
    reset_audio();
    pthread_mutex_unlock(&audio_lock);
}

void khdays_sound_output_pause(int pause) { khdays_ps5_host_io_pause(pause); }
void khdays_ps5_host_io_mute(int mute) { atomic_store(&muted, mute != 0); }
void khdays_sound_output_mute(int mute) { khdays_ps5_host_io_mute(mute); }

static void *sound_main(void *unused)
{
    const struct timespec one_ms = {0, 1000000};
    (void)unused;
    while (atomic_load(&sound_running)) {
        nanosleep(&one_ms, NULL);
        if (atomic_load(&sound_running)) khdays_sound_bridge_advance(khdays_clock_cycles());
    }
    return NULL;
}

void khdays_host_sound_start(void)
{
    pthread_mutex_lock(&lifecycle_lock);
    if (!sound_joinable && atomic_load(&initialized)) {
        atomic_store(&sound_running, 1);
        const int error = pthread_create(&sound_thread, NULL, sound_main, NULL);
        if (error != 0) {
            atomic_store(&sound_running, 0);
            fprintf(stderr, "khdays-native: cannot start sound thread (%d)\n", error);
        } else {
            sound_joinable = 1;
        }
    }
    pthread_mutex_unlock(&lifecycle_lock);
}

int khdays_ps5_host_io_init(void)
{
    pthread_mutex_lock(&lifecycle_lock);
    if (!atomic_load(&initialized)) {
        pthread_mutex_lock(&input_lock);
        memset(&snapshot, 0, sizeof(snapshot));
        touch_x = 128.0f;
        touch_y = 96.0f;
        previous_poll = 0.0;
        atomic_store(&paused, 0);
        atomic_store(&muted, 0);
        if (!pad_open()) fprintf(stderr, "khdays-native: waiting for the player's controller\n");
        pthread_mutex_unlock(&input_lock);
        khdays_sound_output_open();
        pthread_mutex_lock(&input_lock);
        atomic_store(&initialized, 1);
        pthread_mutex_unlock(&input_lock);
    }
    const int result = atomic_load(&audio_open);
    pthread_mutex_unlock(&lifecycle_lock);
    return result;
}

void khdays_ps5_host_io_shutdown(void)
{
    pthread_mutex_lock(&lifecycle_lock);
    /* Drain any in-flight platform pad read before publishing shutdown.
     * No reader can enter pad_poll again until a subsequent init completes.
     * lifecycle -> input is the only nested lock order for these services;
     * neither input polling nor the sound worker acquires lifecycle_lock. */
    pthread_mutex_lock(&input_lock);
    atomic_store(&initialized, 0);
    memset(&snapshot, 0, sizeof(snapshot));
    pthread_mutex_unlock(&input_lock);
    atomic_store(&sound_running, 0);
    if (sound_joinable) {
        pthread_join(sound_thread, NULL);
        sound_joinable = 0;
    }
    khdays_sound_output_close();
    pthread_mutex_unlock(&lifecycle_lock);
}
