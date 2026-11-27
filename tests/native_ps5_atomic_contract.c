#include "runtime_posix.h"

#include <stdatomic.h>

int main(void)
{
    atomic_int slot = 5;
    if (khdays_posix_set_atomic_int(&slot, 9) != 5) return 1;
    if (atomic_load(&slot) != 9) return 2;
    if (khdays_posix_set_atomic_int(&slot, 2) != 9) return 3;
    if (atomic_load(&slot) != 2) return 4;
    return 0;
}
