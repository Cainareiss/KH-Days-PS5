#ifndef KHDAYS_PS5_DIAG_H
#define KHDAYS_PS5_DIAG_H
#include <stdint.h>
int khdays_ps5_diag_attach_log(const char *path);
void khdays_ps5_diag_state(uint32_t frame, uint32_t scene);
void khdays_ps5_diag_note(const char *kind, const char *what);
#endif
