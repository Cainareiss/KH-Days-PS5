#ifndef KHDAYS_ROM_PS5_H
#define KHDAYS_ROM_PS5_H

#include "rom.h"

/* Optional lifecycle extension for nonpersistent sound/asset readers.
 * Readers are thread-owned; delete only after that thread has stopped. */
void khdays_ps5_rom_reader_delete(KhdaysRomReader *reader);

#endif
