/* CPU subtitles for optional HD movies. The original DS subtitles remain
 * part of the game's own frame and never pass through this service. */
#ifndef KHDAYS_MOVIE_SUBTITLES_PS5_H
#define KHDAYS_MOVIE_SUBTITLES_PS5_H

#include <stddef.h>
#include <stdint.h>
#include "host/movies/movie_subtitles.h"

/* Load a theater subtitle schedule plus its relative PNG atlas. Returns 1
 * only after the entire schedule and atlas validate. Failure clears the
 * previous movie's subtitles and explains why when a buffer is supplied.
 * The normal runtime uses khdays_movie_subtitles_load(movie, language). */
int khdays_ps5_movie_subtitles_load_file(const char *path, char *why, size_t size);

/* Composite the lines due at time over a caller-owned straight-alpha RGBA8
 * surface. stride/capacity are bytes; rows run top to bottom. Bilinear atlas
 * sampling and source-over blending preserve other frame content. Returns
 * the number of lines contributing pixels, 0 for no visible line, or -1
 * for invalid surface/time arguments. Thread-safe with load/release. */
int khdays_ps5_movie_subtitles_draw_rgba(uint8_t *rgba, size_t capacity,
                                      size_t stride, int width, int height,
                                      double time, double fps);

#endif
