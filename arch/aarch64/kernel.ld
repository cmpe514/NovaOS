ENTRY(_start)
SECTIONS {
    . = 0x40200000;              /* QEMU virt RAM starts at 0x40000000; the DTB sits below us */
    .text : { KEEP(*(.text.boot)) *(.text .text.*) }
    .rodata : ALIGN(4K) { *(.rodata .rodata.*) }
    .data : ALIGN(4K) { *(.data .data.*) }
    .bss : ALIGN(4K) { __bss_start = .; *(COMMON) *(.bss .bss.*) . = ALIGN(8); __bss_end = .; *(.bss.stack) }
    . = ALIGN(4K);
    _kernel_end = .;
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
