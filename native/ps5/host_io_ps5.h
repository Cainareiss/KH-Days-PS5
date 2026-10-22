/* Native host input and sound for khdays-port. MIT; see ../UPSTREAM-LICENSE. */
#ifndef KHDAYS_PS5_HOST_IO_H
#define KHDAYS_PS5_HOST_IO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The caller owns the host loop. Initialize before starting the game and
 * shut down after the game thread has joined. Explicit per-frame polling is
 * optional: each host input/buttons/touch read refreshes the controller even
 * when identical frames are not presented. Platform reads are serialized.
 * The sound bridge starts its own joinable thread through host_sound_start.
 * Initialization returns 0 if the audio output could not be opened; a missing
 * controller is recoverable (the platform service keeps watching for one). */
int khdays_ps5_host_io_init(void);
void khdays_ps5_host_io_poll(void);
void khdays_ps5_host_io_shutdown(void);

/* KHDAYS_ENH_* from host/app/host.h. Defaults enable analog movement, analog
 * camera and separate lock-on, retaining the DS face-button layout. */
void khdays_ps5_host_io_enhancements(uint16_t flags);
void khdays_ps5_host_io_pause(int pause);
void khdays_ps5_host_io_mute(int mute);

#ifdef __cplusplus
}
#endif
#endif
