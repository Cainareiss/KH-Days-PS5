/* POSIX diagnostics for the native runtime. Script/update hooks are copied
 * from the pinned upstream into diag_hooks.c by the preparation script. */
#include "runtime/core/runtime.h"
#include "runtime/core/window.h"
#include "diag_ps5.h"
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#if !defined(__linux__)
#include <pthread_np.h>
#endif
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>

static int crash_log_fd = -1;
static uintptr_t executable_base;
static volatile sig_atomic_t current_frame = -1, current_scene = -1;
static unsigned scene_reports;
extern const unsigned char __khdays_text_base[];

static void persist(const char *line, size_t size)
{
    (void)write(STDERR_FILENO, line, size);
    if (crash_log_fd >= 0) (void)write(crash_log_fd, line, size);
}

int khdays_ps5_diag_attach_log(const char *path)
{
    const int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd < 0) return 0;
    if (crash_log_fd >= 0) close(crash_log_fd);
    crash_log_fd = fd;
    /* dladdr does not describe this native title on every firmware. The
     * linker-defined text base is relocated with the image, including ASLR. */
    executable_base = (uintptr_t)__khdays_text_base;
    return 1;
}

void khdays_ps5_diag_state(uint32_t frame, uint32_t scene)
{
    const int changed = (uint32_t)current_scene != scene;
    current_frame = (sig_atomic_t)frame;
    current_scene = (sig_atomic_t)scene;
    if (changed && scene_reports < 16) {
        char line[160];
        const int length = snprintf(line, sizeof(line),
            "RUNTIME scene=%u ds=%u executable_base=%016llx\n", scene, frame,
            (unsigned long long)executable_base);
        if (length > 0 && (size_t)length < sizeof(line)) persist(line, (size_t)length);
        ++scene_reports;
    }
}

void khdays_ps5_diag_note(const char *kind, const char *what)
{
    char line[768];
    const int length = snprintf(line, sizeof(line), "RUNTIME_STOP kind=%s ds=%u scene=%u reason=%.600s\n",
        kind, (unsigned)current_frame, (unsigned)current_scene, what);
    if (length > 0 && (size_t)length < sizeof(line)) persist(line, (size_t)length);
}

static size_t add_text(char *out, size_t used, const char *text)
{
    while (*text && used < 480) out[used++] = *text++;
    return used;
}
static size_t add_hex(char *out, size_t used, uintptr_t value)
{
    used = add_text(out, used, "0x");
    for (unsigned digit = sizeof(value) * 2; digit != 0; --digit) {
        if (used < 480) out[used++] = "0123456789abcdef"[(value >> (4 * (digit - 1))) & 15];
    }
    return used;
}

/* Async-signal-safe reporting: no allocator, locks, stdio or symbol lookup
 * while a fatal signal may have interrupted any of those operations. */
void khdays_ps5_diag_fault(int signal_number, const void *fault, uintptr_t instruction, const char *reason)
{
    char line[512]; size_t used = 0;
    used = add_text(line, used, "khdays-native PS5: ");
    used = add_text(line, used, reason);
    used = add_text(line, used, "; signal ");
    used = add_hex(line, used, (uintptr_t)signal_number);
    used = add_text(line, used, ", address "); used = add_hex(line, used, (uintptr_t)fault);
    used = add_text(line, used, ", PC "); used = add_hex(line, used, instruction);
    used = add_text(line, used, ", ELF+");
    used = add_hex(line, used, executable_base != 0 && instruction >= executable_base ? instruction - executable_base : 0);
    used = add_text(line, used, ", frame "); used = add_hex(line, used, (unsigned)current_frame);
    used = add_text(line, used, ", scene "); used = add_hex(line, used, (unsigned)current_scene);
    line[used++] = '\n';
    persist(line, used);
}

static uintptr_t signal_pc(const void *context)
{
    const ucontext_t *c = context;
#if defined(__linux__)
    return (uintptr_t)c->uc_mcontext.gregs[REG_RIP];
#else
    return (uintptr_t)c->uc_mcontext.mc_rip;
#endif
}

/* A call to a NULL function pushes its return address before RIP becomes
 * zero. Record that address too: a zero PC alone cannot locate the caller.
 * This formatter stays async-signal-safe, like the main fatal record. */
void khdays_ps5_diag_null_caller(uintptr_t stack, uintptr_t caller)
{
    char line[512]; size_t used = 0;
    used = add_text(line, used, "NULL_CALL stack "); used = add_hex(line, used, stack);
    used = add_text(line, used, ", return "); used = add_hex(line, used, caller);
    used = add_text(line, used, ", return_ELF+");
    used = add_hex(line, used, executable_base != 0 && caller >= executable_base ? caller - executable_base : 0);
    line[used++] = '\n';
    persist(line, used);
}

static void fatal_signal(int number, siginfo_t *info, void *context)
{
    const uintptr_t pc = signal_pc(context);
    khdays_ps5_diag_fault(number, info->si_addr, pc, "fatal runtime fault");
    if (pc == 0) {
        const ucontext_t *c = context;
#if defined(__linux__)
        const uintptr_t stack = (uintptr_t)c->uc_mcontext.gregs[REG_RSP];
#else
        const uintptr_t stack = (uintptr_t)c->uc_mcontext.mc_rsp;
#endif
        /* With a successful CALL to address zero the stack's first word is
         * readable. Skip evidently invalid stacks; the fatal record above
         * has already been written if the fault was instead stack damage. */
        if (stack >= 0x10000u && (stack & (sizeof(uintptr_t) - 1u)) == 0)
            khdays_ps5_diag_null_caller(stack, *(const volatile uintptr_t *)stack);
    }
    /* Restore the default disposition and redeliver on this thread. Besides
     * preserving the platform crash report this prevents a fault loop. */
    struct sigaction action = {0};
    action.sa_handler = SIG_DFL; sigemptyset(&action.sa_mask);
    sigaction(number, &action, NULL);
    raise(number);
}
void khdays_diag_init(void)
{
    const int signals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGSYS};
    struct sigaction action = {0};
    action.sa_sigaction = fatal_signal; action.sa_flags = SA_SIGINFO;
    sigemptyset(&action.sa_mask);
    for (size_t i = 0; i < sizeof(signals) / sizeof(signals[0]); ++i) {
        if (sigaction(signals[i], &action, NULL) != 0) {
            perror("khdays-native: sigaction"); abort();
        }
    }
    if (getenv("KHDAYS_WATCH") != NULL || getenv("KHDAYS_STALL_SECONDS") != NULL) {
        fprintf(stderr, "diag: PS5 supports fatal PC/address reports and current-thread stacks; "
                        "Windows hardware watchpoints and suspended-thread sampling are unavailable\n");
    }
}
void khdays_debugf(const char *format, ...)
{
    va_list args; va_start(args, format); vfprintf(stderr, format, args); va_end(args); fflush(stderr);
}
unsigned long long khdays_diag_symbol(const char *name)
{
    return (unsigned long long)(uintptr_t)dlsym(RTLD_DEFAULT, name);
}
const char *khdays_diag_function_at(const void *address)
{
    Dl_info info;
    if (address == NULL) return "-";
    return dladdr(address, &info) != 0 && info.dli_sname != NULL ? info.dli_sname : "?";
}

static int stack_bounds(uintptr_t frame, uintptr_t *low, uintptr_t *high)
{
    const uintptr_t window = (uintptr_t)khdays_window;
    /* Cooperative DS contexts live in the explicitly allocated stack area. */
    if (window != 0 && frame >= window + 0x0c000000u && frame < window + 0x1c000000u) {
        *low = frame; *high = (frame + 0x10000u) & ~(uintptr_t)0xffffu;
        return 1;
    }
    pthread_attr_t attributes;
    void *base; size_t size; int result;
#if defined(__linux__)
    result = pthread_getattr_np(pthread_self(), &attributes);
#else
    if (pthread_attr_init(&attributes) != 0) return 0;
    result = pthread_attr_get_np(pthread_self(), &attributes);
#endif
    if (result != 0) return 0;
    result = pthread_attr_getstack(&attributes, &base, &size);
    pthread_attr_destroy(&attributes);
    if (result != 0) return 0;
    *low = (uintptr_t)base; *high = *low + size;
    return frame >= *low && frame < *high;
}
static size_t callers(const void **out, size_t count)
{
    const uintptr_t *frame = __builtin_frame_address(0);
    uintptr_t low, high;
    size_t used = 0;
    if (!stack_bounds((uintptr_t)frame, &low, &high)) return 0;
    while (used < count && (uintptr_t)frame >= low && (uintptr_t)frame <= high - 2 * sizeof(uintptr_t) &&
           ((uintptr_t)frame & (sizeof(uintptr_t) - 1)) == 0) {
        const uintptr_t next = frame[0];
        if (frame[1] == 0) break;
        out[used++] = (const void *)frame[1];
        if (next <= (uintptr_t)frame) break;
        frame = (const uintptr_t *)next;
    }
    return used;
}
void khdays_diag_stack(void)
{
    const void *frames[32]; const size_t count = callers(frames, 32);
    for (size_t i = 1; i < count; ++i) {
        fprintf(stderr, "  #%zu %p %s\n", i - 1, frames[i], khdays_diag_function_at(frames[i]));
    }
}
void khdays_diag_callers(char *out, int size, int skip)
{
    const void *frames[32]; const size_t count = callers(frames, 32); size_t used = 0;
    if (size <= 0) return;
    out[0] = '\0';
    for (size_t i = (size_t)(skip < 0 ? 1 : skip + 1); i < count && used < (size_t)size - 1; ++i) {
        const int written = snprintf(out + used, (size_t)size - used, "%s%s", used ? " < " : "",
                                     khdays_diag_function_at(frames[i]));
        if (written < 0 || (size_t)written >= (size_t)size - used) break;
        used += (size_t)written;
    }
}
