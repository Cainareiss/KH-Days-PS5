#ifndef KHDAYS_PS5_GAME_TABLES_H
#define KHDAYS_PS5_GAME_TABLES_H

#include <stddef.h>
#include <stdint.h>

/* These records cross the generated-game / host ABI boundary. The game is
 * otherwise packed to four bytes; the records deliberately use eight. */
#pragma pack(push, 8)
struct khdays_data_init {
    unsigned int address;
    const void *init;
    unsigned int size;
    int module;
};
struct khdays_ps5_function_record {
    unsigned int reference;
    void (*function)(void);
};
#pragma pack(pop)

_Static_assert(sizeof(struct khdays_data_init) == 24, "data table ABI");
_Static_assert(offsetof(struct khdays_data_init, init) == 8, "data pointer ABI");
_Static_assert(sizeof(struct khdays_ps5_function_record) == 16, "function table ABI");
_Static_assert(offsetof(struct khdays_ps5_function_record, function) == 8, "function pointer ABI");

extern const struct khdays_data_init __start_khdays_data[];
extern const struct khdays_data_init __stop_khdays_data[];
extern const struct khdays_ps5_function_record __start_khdays_functions[];
extern const struct khdays_ps5_function_record __stop_khdays_functions[];

#endif
