/* Protects the same DS I/O ranges as upstream io_guard.c. The memory backend
 * reserves these PROT_NONE; accesses missed by rewriting fault synchronously. */
#include "runtime/core/io.h"
#include "runtime/core/window.h"
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>
#include <unistd.h>

void khdays_ps5_diag_fault(int number, const void *fault, uintptr_t pc, const char *reason);
static struct sigaction previous_segv, previous_bus;

static void guard(int number, siginfo_t *info, void *context)
{
    const uintptr_t host = (uintptr_t)info->si_addr;
    const uintptr_t window = (uintptr_t)khdays_window;
    const int in_window = window != 0 && host >= window && host - window <= UINT32_MAX;
    const uintptr_t address = in_window ? host - window : host;
    const int io = (address >= KHDAYS_IO_BASE && address < KHDAYS_IO_BASE + KHDAYS_IO_SIZE) ||
                   (address >= KHDAYS_IO2_BASE && address < KHDAYS_IO2_BASE + KHDAYS_IO2_SIZE);
    const int raw_ds = window != 0 && !in_window && host >= 0x01ff8000u && host < 0x0b000000u;
    if (io || raw_ds) {
        const ucontext_t *c = context;
#if defined(__linux__)
        const uintptr_t pc = (uintptr_t)c->uc_mcontext.gregs[REG_RIP];
#else
        const uintptr_t pc = (uintptr_t)c->uc_mcontext.mc_rip;
#endif
        khdays_ps5_diag_fault(number, info->si_addr, pc,
                              io ? "unrewritten DS I/O access" : "raw DS address outside reference window");
        _exit(io ? 4 : 6);
    }
    const struct sigaction *previous = number == SIGBUS ? &previous_bus : &previous_segv;
    if (previous->sa_flags & SA_SIGINFO) {
        previous->sa_sigaction(number, info, context);
    } else if (previous->sa_handler == SIG_IGN) {
        /* Ignoring a synchronous fault would retry the same instruction. */
        _exit(128 + number);
    } else if (previous->sa_handler != SIG_DFL) {
        previous->sa_handler(number);
    } else {
        sigaction(number, previous, NULL);
        raise(number);
    }
}
void khdays_io_guard_init(void)
{
    struct sigaction action = {0};
    action.sa_sigaction = guard; action.sa_flags = SA_SIGINFO;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGSEGV, &action, &previous_segv) != 0 ||
        sigaction(SIGBUS, &action, &previous_bus) != 0) {
        perror("khdays-native: I/O guard sigaction"); abort();
    }
}
