/* Minimal copy of the PS5 title CRT's floating-point setup.
 * This is local to the entrypoint. OS, heap, and threading services are
 * provided by libps5platform and the native title's module imports. */
#include <stdint.h>

void ps5_fp_ieee(void)
{
    const uint32_t mxcsr = 0x1f80u;
    const uint16_t x87cw = 0x037fu;
    __asm__ volatile("ldmxcsr %0\n\t"
                     "fldcw %1\n\t"
                     :
                     : "m"(mxcsr), "m"(x87cw));
}
