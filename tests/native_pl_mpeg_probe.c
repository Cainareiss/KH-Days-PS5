/* Run the same pinned decoder as the PS5 movie replacement, independently of
 * FFmpeg's ability to read its own output. Usage: probe file.mpg [frame limit]. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg.h"

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 3) return 2;
    const int limit = argc == 3 ? atoi(argv[2]) : 1;
    plm_t *video = plm_create_with_filename(argv[1]);
    plm_t *audio = plm_create_with_filename(argv[1]);
    if (video == NULL || audio == NULL || !plm_has_headers(video)) return 3;
    plm_set_audio_enabled(video, 0);
    plm_set_video_enabled(audio, 0);
    plm_set_audio_enabled(audio, 1);
    const int width = plm_get_width(video), height = plm_get_height(video);
    if (width <= 0 || height <= 0 || (size_t)width * height > 4096u * 4096u) return 6;
    uint8_t *rgba = malloc((size_t)width * height * 4u);
    if (rgba == NULL) return 7;
    unsigned frames = 0, audio_samples = 0;
    uint32_t checksum = 2166136261u;
    const clock_t started = clock();
    for (; limit <= 0 || frames < (unsigned)limit; ++frames) {
        plm_frame_t *frame = plm_decode_video(video);
        if (frame == NULL) break;
        if (frame->width != (unsigned)width || frame->height != (unsigned)height || frame->y.data == NULL) return 4;
        /* The native replacement path does this conversion before upload. */
        plm_frame_to_rgba(frame, rgba, width * 4);
        for (unsigned y = 0; y < frame->y.height; y += 17) {
            for (unsigned x = 0; x < frame->y.width; x += 17) {
                checksum = (checksum ^ frame->y.data[(size_t)y * frame->y.width + x]) * 16777619u;
            }
        }
    }
    const plm_samples_t *samples = plm_decode_audio(audio);
    if (samples != NULL) audio_samples = samples->count;
    const double elapsed = (double)(clock() - started) / CLOCKS_PER_SEC;
    printf("{\"width\":%d,\"height\":%d,\"fps\":%.9f,\"video_frames\":%u,"
           "\"audio_samples\":%u,\"luma_checksum\":%u,\"decode_rgba_seconds\":%.6f}\n",
           width, height, plm_get_framerate(video), frames, audio_samples, checksum, elapsed);
    free(rgba);
    plm_destroy(video);
    plm_destroy(audio);
    return frames > 0 ? 0 : 5;
}
