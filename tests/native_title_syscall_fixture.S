/* Audit must inspect instructions, not the 0f 05 byte pattern in data. */
.text
#ifndef FIXTURE_DATA_ONLY
.global native_title_fixture
.type native_title_fixture,@function
native_title_fixture:
#ifdef FIXTURE_FORBIDDEN
    syscall
#endif
    ret
#endif
.section .rodata
    .byte 0x0f, 0x05
