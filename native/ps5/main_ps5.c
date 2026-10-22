/* Minimal title entrypoint for the Prospero executable link.
 * The actual boot sequence lives in start_ps5.c so it can also be exercised
 * by host-side contract tests without duplicating runtime initialization. */
#include "start_ps5.h"

int main(int argc, char **argv)
{
    return khdays_ps5_boot(argc, argv);
}
