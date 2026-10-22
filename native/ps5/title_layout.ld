/* PS5 native title layout, matched to the working Melon Mix title script. */
OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)
PHDRS {
    text PT_LOAD FLAGS(5);
    ro PT_LOAD FLAGS(4);
    relro PT_LOAD FLAGS(6);
    data PT_LOAD FLAGS(6);
    dynamic PT_DYNAMIC FLAGS(6);
}
SECTIONS {
    .text 0 : ALIGN(0x4000) { *(.text .text.*) *(.plt .plt.*) } : text
    PROVIDE_HIDDEN(__khdays_text_base = ADDR(.text));
    .eh_frame_hdr : ALIGN(0x4000) {
        PROVIDE(__eh_frame_hdr_start = .); KEEP(*(.eh_frame_hdr))
        PROVIDE(__eh_frame_hdr_end = .);
    } : ro
    .eh_frame : {
        PROVIDE(__eh_frame_start = .); KEEP(*(.eh_frame))
        PROVIDE(__eh_frame_end = .);
    } : ro
    .gcc_except_table : { *(.gcc_except_table .gcc_except_table.*) } : ro
    .rodata : { *(.rodata .rodata.*) } : ro
    . = ALIGN(0x4000);
    .data.rel.ro : { *(.data.rel.ro .data.rel.ro.*) } : relro
    .got : { *(.got .got.plt) } : relro
    .preinit_array : {
        PROVIDE_HIDDEN(__preinit_array_start = .); KEEP(*(.preinit_array))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } : relro
    .init_array : {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*) SORT_BY_INIT_PRIORITY(.ctors.*)))
        KEEP(*(.init_array .ctors)) PROVIDE_HIDDEN(__init_array_end = .);
    } : relro
    .fini_array : {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*) SORT_BY_INIT_PRIORITY(.dtors.*)))
        KEEP(*(.fini_array .dtors)) PROVIDE_HIDDEN(__fini_array_end = .);
    } : relro
    .dynsym : { *(.dynsym) } : relro
    .gnu.hash : { *(.gnu.hash) } : relro
    .dynstr : { *(.dynstr) } : relro
    .rela.dyn : { *(.rela.dyn) } : relro
    .rela.plt : { *(.rela.plt) } : relro
    /* Room for the converter's process parameters and NID imports. */
    . = ALIGN(0x4000) + 0x10000;
    .dynamic : { *(.dynamic) } : data : dynamic
    .data : { *(.data .data.*) } : data
    .tdata : { *(.tdata .tdata.*) } : data
    .tbss : { *(.tbss .tbss.*) } : data
    .bss : { *(.bss .bss.*) *(COMMON) } : data
}
