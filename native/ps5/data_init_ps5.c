/* ELF counterpart of khdays-port runtime/core/data_init.c. */
#include "game_tables.h"
#include "window.h"
#include <string.h>

void khdays_overlay_started(int id);

void khdays_data_init(int module)
{
    if (module >= 0) {
        khdays_overlay_started(module);
    }
    for (const struct khdays_data_init *entry = __start_khdays_data;
         entry < __stop_khdays_data; ++entry) {
        if (entry->address != 0 && entry->module == module) {
            memcpy(KHDAYS_W(entry->address), entry->init, entry->size);
        }
    }
}
