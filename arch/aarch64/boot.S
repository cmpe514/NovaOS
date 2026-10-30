/* NovaOS AArch64 entry. QEMU virt loads the kernel ELF and starts here at EL1 (or EL2, which we leave). */
.section .text.boot, "ax"
.global _start
_start:
    mrs x1, CurrentEL
    lsr x1, x1, #2
    cmp x1, #2
    b.ne 1f
    mov x1, #(1 << 31)                 /* EL2 -> EL1: EL1 is AArch64 */
    msr hcr_el2, x1
    mrs x1, cnthctl_el2
    orr x1, x1, #3                     /* EL1 may use the counters and timers */
    msr cnthctl_el2, x1
    msr cntvoff_el2, xzr
    mov x1, #0x3c5                     /* EL1h, interrupts masked */
    msr spsr_el2, x1
    adr x1, 1f
    msr elr_el2, x1
    eret
1:  ldr x1, =boot_stack_top
    mov sp, x1
    ldr x1, =__bss_start
    ldr x2, =__bss_end
2:  cmp x1, x2
    b.hs 3f
    str xzr, [x1], #8
    b 2b
3:  bl kmain
4:  wfi
    b 4b
.ltorg

.section .bss.stack, "aw", %nobits
.balign 16
boot_stack: .skip 32768
.global boot_stack_top
boot_stack_top:
