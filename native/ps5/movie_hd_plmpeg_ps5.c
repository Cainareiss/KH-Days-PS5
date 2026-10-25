/* Native replacement for movie_hd_win.c. The prepared MPEG-1/MP2 cutscenes
 * use the same pinned pl_mpeg decoder as the port's movie-mod path. */
#include "host/movies/movie_hd.h"
#include "host_io_ps5.h"

#include <stdio.h>
#include <pl_mpeg.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_MOVIE_WIDTH 4095u
#define MAX_MOVIE_HEIGHT 4095u

static pthread_mutex_t movie_lock = PTHREAD_MUTEX_INITIALIZER;
static plm_t *video_decoder;
static plm_t *audio_decoder;
static uint8_t *nv12;
static unsigned width, height;
static double fps, next_video_time, audio_decoded_until;
static uint64_t started_ns, paused_at_ns, paused_ns;
static float gain = 1.0f;
static int started, paused, first_picture, ended;

static uint64_t monotonic_ns(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0;
    return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}

static void explain(char *why, size_t size, const char *message)
{
    if (why != NULL && size != 0) (void)snprintf(why, size, "%s", message ? message : "");
}

static void clear_locked(void)
{
    if (video_decoder != NULL) plm_destroy(video_decoder);
    if (audio_decoder != NULL) plm_destroy(audio_decoder);
    video_decoder = audio_decoder = NULL;
    free(nv12);
    nv12 = NULL;
    width = height = 0;
    fps = next_video_time = audio_decoded_until = 0.0;
    started_ns = paused_at_ns = paused_ns = 0;
    started = paused = first_picture = ended = 0;
    khdays_ps5_host_io_movie_audio_stop();
}

static int copy_frame_locked(plm_frame_t *frame)
{
    if (frame == NULL || frame->width == 0 || frame->height == 0 ||
        frame->width > MAX_MOVIE_WIDTH || frame->height > MAX_MOVIE_HEIGHT ||
        (frame->width & 1u) != 0 || (frame->height & 1u) != 0 ||
        frame->y.data == NULL || frame->cr.data == NULL || frame->cb.data == NULL ||
        frame->y.width < frame->width || frame->y.height < frame->height ||
        frame->cr.width < frame->width / 2u || frame->cr.height < frame->height / 2u ||
        frame->cb.width < frame->width / 2u || frame->cb.height < frame->height / 2u)
        return 0;

    const size_t y_bytes = (size_t)frame->width * frame->height;
    const size_t total = y_bytes + y_bytes / 2u;
    uint8_t *buffer = realloc(nv12, total);
    if (buffer == NULL) return 0;
    nv12 = buffer;
    width = frame->width;
    height = frame->height;
    for (unsigned y = 0; y < height; ++y) {
        memcpy(nv12 + (size_t)y * width,
               frame->y.data + (size_t)y * frame->y.width, width);
    }
    uint8_t *chroma = nv12 + y_bytes;
    for (unsigned y = 0; y < height / 2u; ++y) {
        const uint8_t *cr = frame->cr.data + (size_t)y * frame->cr.width;
        const uint8_t *cb = frame->cb.data + (size_t)y * frame->cb.width;
        uint8_t *row = chroma + (size_t)y * width;
        for (unsigned x = 0; x < width / 2u; ++x) {
            row[2u * x] = cb[x];
            row[2u * x + 1u] = cr[x];
        }
    }
    next_video_time = frame->time + (fps > 0.0 ? 1.0 / fps : 1.0 / 30.0);
    return 1;
}

int khdays_hd_open(const char *path, KhdaysHdInfo *info, char *why, size_t size)
{
    explain(why, size, "");
    if (info != NULL) memset(info, 0, sizeof(*info));
    if (path == NULL || *path == '\0') {
        explain(why, size, "empty HD movie path");
        return 0;
    }
    pthread_mutex_lock(&movie_lock);
    clear_locked();
    video_decoder = plm_create_with_filename(path);
    if (video_decoder == NULL || !plm_has_headers(video_decoder) ||
        plm_get_num_video_streams(video_decoder) < 1) {
        clear_locked();
        pthread_mutex_unlock(&movie_lock);
        explain(why, size, "pl_mpeg could not read the MPEG-1 video stream");
        return 0;
    }
    plm_set_audio_enabled(video_decoder, 0);
    width = (unsigned)plm_get_width(video_decoder);
    height = (unsigned)plm_get_height(video_decoder);
    fps = plm_get_framerate(video_decoder);
    if (width < 2 || height < 2 || width > MAX_MOVIE_WIDTH || height > MAX_MOVIE_HEIGHT ||
        (width & 1u) || (height & 1u) || fps <= 0.0 || fps > 120.0) {
        clear_locked();
        pthread_mutex_unlock(&movie_lock);
        explain(why, size, "unsupported HD movie dimensions or frame rate");
        return 0;
    }
    audio_decoder = plm_create_with_filename(path);
    int channels = 0, rate = 0;
    if (audio_decoder != NULL && plm_has_headers(audio_decoder) &&
        plm_get_num_audio_streams(audio_decoder) > 0) {
        plm_set_video_enabled(audio_decoder, 0);
        rate = plm_get_samplerate(audio_decoder);
        if (rate == 48000) channels = 2;
        else { plm_destroy(audio_decoder); audio_decoder = NULL; }
    } else if (audio_decoder != NULL) {
        plm_destroy(audio_decoder);
        audio_decoder = NULL;
    }
    plm_frame_t *first = plm_decode_video(video_decoder);
    if (first == NULL || !copy_frame_locked(first)) {
        clear_locked();
        pthread_mutex_unlock(&movie_lock);
        explain(why, size, "pl_mpeg could not decode the first HD movie frame");
        return 0;
    }
    first_picture = 1;
    if (info != NULL) {
        info->width = (int)width;
        info->height = (int)height;
        info->fps = fps;
        info->channels = channels;
        info->rate = rate;
    }
    pthread_mutex_unlock(&movie_lock);
    return 1;
}

void khdays_hd_start(void)
{
    pthread_mutex_lock(&movie_lock);
    if (video_decoder != NULL && !started) {
        started_ns = monotonic_ns();
        started = 1;
    }
    pthread_mutex_unlock(&movie_lock);
}

void khdays_hd_pause(int pause)
{
    pthread_mutex_lock(&movie_lock);
    if (started && pause && !paused) {
        paused_at_ns = monotonic_ns();
        paused = 1;
    } else if (started && !pause && paused) {
        const uint64_t now = monotonic_ns();
        if (now >= paused_at_ns) paused_ns += now - paused_at_ns;
        paused = 0;
    }
    pthread_mutex_unlock(&movie_lock);
}

void khdays_hd_close(void)
{
    pthread_mutex_lock(&movie_lock);
    clear_locked();
    pthread_mutex_unlock(&movie_lock);
}

int khdays_hd_status(char *why, size_t size)
{
    pthread_mutex_lock(&movie_lock);
    if (video_decoder == NULL) {
        explain(why, size, "");
        pthread_mutex_unlock(&movie_lock);
        return 2;
    }
    /* Only picture_begin can declare the end, after every buffered picture
     * has been consumed on its presentation clock. Demux EOF alone can occur
     * before pl_mpeg has returned its final decoded frame. */
    const int status = ended;
    if (status) explain(why, size, "");
    pthread_mutex_unlock(&movie_lock);
    return status ? 1 : 0;
}

void khdays_hd_set_gain(float value)
{
    if (value < 0.0f) value = 0.0f;
    if (value > 1.0f) value = 1.0f;
    pthread_mutex_lock(&movie_lock);
    gain = value;
    pthread_mutex_unlock(&movie_lock);
}

int khdays_hd_picture_begin(KhdaysHdPicture *picture)
{
    if (picture != NULL) memset(picture, 0, sizeof(*picture));
    if (picture == NULL) return 0;
    pthread_mutex_lock(&movie_lock);
    if (video_decoder == NULL || !started || paused || ended) {
        pthread_mutex_unlock(&movie_lock);
        return 0;
    }
    const uint64_t now = monotonic_ns();
    const uint64_t elapsed = now >= started_ns + paused_ns ? now - started_ns - paused_ns : 0;
    const double time = (double)elapsed / 1000000000.0;
    int ready = 0;
    if (first_picture) {
        first_picture = 0;
        ready = 1;
    }
    while (time + 0.0005 >= next_video_time) {
        plm_frame_t *frame = plm_decode_video(video_decoder);
        if (frame == NULL) {
            if (plm_has_ended(video_decoder)) ended = 1;
            break;
        }
        if (!copy_frame_locked(frame)) {
            ended = 1;
            break;
        }
        ready = 1;
    }
    if (audio_decoder != NULL) {
        for (unsigned decoded = 0; decoded < 8 && audio_decoded_until < time + 0.12; ++decoded) {
            plm_samples_t *samples = plm_decode_audio(audio_decoder);
            if (samples == NULL) break;
            if (samples->count == 0 || samples->count > PLM_AUDIO_SAMPLES_PER_FRAME) break;
#ifdef PLM_AUDIO_SEPARATE_CHANNELS
            float interleaved[PLM_AUDIO_SAMPLES_PER_FRAME * 2u];
            for (unsigned i = 0; i < samples->count; ++i) {
                interleaved[2u * i] = samples->left[i];
                interleaved[2u * i + 1u] = samples->right[i];
            }
            khdays_ps5_host_io_movie_audio(interleaved, (int)samples->count, gain);
#else
            khdays_ps5_host_io_movie_audio(samples->interleaved, (int)samples->count, gain);
#endif
            audio_decoded_until = samples->time + (double)samples->count / 48000.0;
        }
    }
    if (ready) {
        picture->luma = nv12;
        picture->chroma = nv12 + (size_t)width * height;
        picture->pitch = (int)width;
        picture->width = (int)width;
        picture->height = (int)height;
        picture->time = next_video_time - (fps > 0.0 ? 1.0 / fps : 1.0 / 30.0);
        picture->fps = fps;
        /* Keep movie_lock until picture_done so close/reopen cannot free the
         * planes while the Vulkan upload is copying them. */
        return 1;
    }
    pthread_mutex_unlock(&movie_lock);
    return 0;
}

void khdays_hd_picture_done(void)
{
    pthread_mutex_unlock(&movie_lock);
}
