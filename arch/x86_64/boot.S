/* NovaOS x86-64 entry. GRUB (Multiboot2) enters here in 32-bit protected mode with paging off.
 * We identity-map the first 4 GiB with 2 MiB pages, enable long mode and call kmain(magic, mbi). */
.set MB2_MAGIC, 0xe85250d6
.set MB2_ARCH, 0

.section .multiboot, "a"
.align 8
mb2_header:
    .long MB2_MAGIC
    .long MB2_ARCH
    .long mb2_header_end - mb2_header
    .long -(MB2_MAGIC + MB2_ARCH + (mb2_header_end - mb2_header))
    .align 8                       /* framebuffer request: 1280x800x32 (GRUB picks the closest mode) */
    .short 5, 0
    .long 20
    .long 1280, 800, 32
    .align 8
    .short 0, 0                    /* end tag */
    .long 8
mb2_header_end:

.section .bss
.align 4096
.global boot_pml4, boot_pdpt, boot_pd
boot_pml4: .skip 4096
boot_pdpt: .skip 4096
boot_pd:   .skip 4096 * 4
.align 16
.global boot_stack_top
boot_stack: .skip 32768
boot_stack_top:

.section .rodata
.align 8
gdt32:
    .quad 0
    .quad 0x00af9a000000ffff       /* 0x08: 64-bit code */
    .quad 0x00cf92000000ffff       /* 0x10: data */
gdt32_ptr:
    .short gdt32_ptr - gdt32 - 1
    .long gdt32

.section .text
.code32
.global _start
_start:
    cli
    mov $boot_stack_top, %esp
    mov %eax, %edi                 /* multiboot2 magic */
    mov %ebx, %esi                 /* multiboot2 info (physical) */

    mov $boot_pdpt, %eax
    or $3, %eax
    mov %eax, boot_pml4
    mov $boot_pd, %eax
    or $3, %eax
    mov %eax, boot_pdpt
    add $4096, %eax
    mov %eax, boot_pdpt + 8
    add $4096, %eax
    mov %eax, boot_pdpt + 16
    add $4096, %eax
    mov %eax, boot_pdpt + 24

    xor %ecx, %ecx
1:  mov %ecx, %eax
    shl $21, %eax
    or $0x83, %eax                 /* present | writable | 2 MiB page */
    mov %eax, boot_pd(,%ecx,8)
    movl $0, boot_pd + 4(,%ecx,8)
    inc %ecx
    cmp $2048, %ecx
    jne 1b

    mov $boot_pml4, %eax
    mov %eax, %cr3
    mov %cr4, %eax
    or $(1 << 5), %eax             /* PAE */
    mov %eax, %cr4
    mov $0xc0000080, %ecx          /* EFER */
    rdmsr
    or $((1 << 8) | (1 << 11)), %eax   /* LME | NXE */
    wrmsr
    mov %cr0, %eax
    or $((1 << 31) | (1 << 16) | 1), %eax  /* PG | WP | PE */
    mov %eax, %cr0
    lgdt gdt32_ptr
    ljmp $0x08, $long_mode

.code64
long_mode:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    xor %ax, %ax
    mov %ax, %fs
    mov %ax, %gs
    mov $boot_stack_top, %rsp
    mov %edi, %edi                 /* zero-extend the 32-bit arguments */
    mov %esi, %esi
    call kmain
2:  cli
    hlt
    jmp 2b
