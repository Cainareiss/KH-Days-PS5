/* Link-layout test only; never part of a game executable. */
#include "game_tables.h"

int fixture_keep_data = 1;
static const unsigned char bytes[] = {1, 2, 3, 4};
static void fixture_handler(void) {}

__attribute__((section("khdays_data"), aligned(8), used))
const struct khdays_data_init fixture_data = {0x02000000, bytes, sizeof(bytes), -1};
__attribute__((section("khdays_functions"), aligned(8), used))
const struct khdays_ps5_function_record fixture_function = {0xfe000000, fixture_handler};

_Static_assert(sizeof(int) == 4, "DS word");
_Static_assert(sizeof(void *) == 8, "host pointer");
