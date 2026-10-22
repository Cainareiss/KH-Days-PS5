#ifndef KHDAYS_START_PS5_H
#define KHDAYS_START_PS5_H

/* Bring up the complete native runtime and enter the recompiled game's
 * NitroMain on a DS-addressable execution context. A startup failure
 * returns nonzero; successful gameplay does not return. */
int khdays_ps5_boot(int argc, char **argv);

#endif
