/* A data-only object: nothing refers to these names at ELF link time. */
#include "game_tables.h"
static const unsigned int bg_registers[8] = {
    0x0b100008, 0x0b10000a, 0x0b10000c, 0x0b10000e,
    0x0b101008, 0x0b10100a, 0x0b10100c, 0x0b10100e
};
__attribute__((section("khdays_data"), aligned(8), used))
static const struct khdays_data_init bg_table = {0x02041ac0, bg_registers, sizeof(bg_registers), -1};
static void callback(void) {}
__attribute__((section("khdays_functions"), aligned(8), used))
static const struct khdays_ps5_function_record function = {0xfe000001, callback};
