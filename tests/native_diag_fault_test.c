/* The included functions are extracted verbatim from diag_ps5.c. Verify the
 * crash record reaches the persistent descriptor and carries an ELF offset. */
#include <assert.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define STDERR_FILENO 2
static int crash_log_fd = 77;
static uintptr_t executable_base = 0x10000000u;
static volatile sig_atomic_t current_frame = 0x4321, current_scene = 2;
static char persistent_line[1024], error_line[1024];
static unsigned writes;
static int capture_write(int fd, const void *data, size_t size)
{
    assert(size < sizeof(persistent_line));
    char *line = fd == STDERR_FILENO ? error_line : persistent_line;
    assert(fd == STDERR_FILENO || fd == crash_log_fd);
    memcpy(line, data, size);
    line[size] = '\0';
    ++writes;
    return (int)size;
}
#define write capture_write
#include "diag_fault_functions.h"
#undef write

int main(void)
{
    khdays_ps5_diag_fault(11, (void *)(uintptr_t)0x1234, 0x10001234u, "test fault");
    assert(writes == 2 && !strcmp(error_line, persistent_line));
    assert(strstr(persistent_line, "ELF+0x0000000000001234"));
    assert(strstr(persistent_line, "frame 0x0000000000004321"));
    assert(strstr(persistent_line, "scene 0x0000000000000002"));
    khdays_ps5_diag_note("ABI gap", "first-world fixture");
    assert(writes == 4 && strstr(persistent_line, "RUNTIME_STOP kind=ABI gap"));
    assert(strstr(persistent_line, "ds=17185 scene=2 reason=first-world fixture"));
    char long_reason[2048];
    memset(long_reason, 'x', sizeof(long_reason) - 1);
    long_reason[sizeof(long_reason) - 1] = '\0';
    khdays_ps5_diag_fault(11, NULL, 0, long_reason);
    assert(writes == 6 && strlen(persistent_line) <= 481);
    assert(persistent_line[strlen(persistent_line) - 1] == '\n');
    crash_log_fd = -1;
    khdays_ps5_diag_fault(11, NULL, 0, "no file");
    assert(writes == 7);
    crash_log_fd = 77;
    khdays_ps5_diag_null_caller(0x40001000u, 0x1000abcd);
    assert(writes == 9);
    assert(strstr(persistent_line, "NULL_CALL stack 0x0000000040001000"));
    assert(strstr(persistent_line, "return_ELF+0x000000000000abcd"));
    khdays_ps5_diag_null_caller(0x40001000u, 0);
    assert(writes == 11 && strstr(persistent_line, "return_ELF+0x0000000000000000"));
    puts("persistent crash: PASS (file descriptor, ELF offset, scene/frame, runtime stop, bounded signal record)");
    return 0;
}
