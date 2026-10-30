/* Exception vectors. Each entry saves x0/x1, puts the entry kind in x1 and branches to the common path,
 * which saves the full frame, calls arm_exception(frame, kind) and resumes the frame it returns. */
.section .text
.balign 2048
.global vectors
vectors:
.balign 128
    sub sp, sp, #272
    stp x0, x1, [sp, #0]
    mov x1, #0
    b exc_common
.balign 128
    sub sp, sp, #272
    stp x0, x1, [sp, #0]
    mov x1, #1
    b exc_common
.balign 128
    sub sp, sp, #272
    stp x0, x1, [sp, #0]
    mov x1, #2
    b exc_common
.balign 128
    sub sp, sp, #272
    stp x0, x1, [sp, #0]
    mov x1, #3
    b exc_common
.balign 128
    sub sp, sp, #272
    stp x0, x1, [sp, #0]
    mov x1, #4
    b exc_common
.balign 128
    sub sp, sp, #272
    stp x0, x1, [sp, #0]
    mov x1, #5
    b exc_common
.balign 128
    sub sp, sp, #272
    stp x0, x1, [sp, #0]
    mov x1, #6
    b exc_common
.balign 128
    sub sp, sp, #272
    stp x0, x1, [sp, #0]
    mov x1, #7
    b exc_common
.balign 128
    sub sp, sp, #272
    stp x0, x1, [sp, #0]
    mov x1, #8
    b exc_common
.balign 128
    sub sp, sp, #272
    stp x0, x1, [sp, #0]
    mov x1, #9
    b exc_common
.balign 128
    sub sp, sp, #272
    stp x0, x1, [sp, #0]
    mov x1, #10
    b exc_common
.balign 128
    sub sp, sp, #272
    stp x0, x1, [sp, #0]
    mov x1, #11
    b exc_common
.balign 128
    sub sp, sp, #272
    stp x0, x1, [sp, #0]
    mov x1, #12
    b exc_common
.balign 128
    sub sp, sp, #272
    stp x0, x1, [sp, #0]
    mov x1, #13
    b exc_common
.balign 128
    sub sp, sp, #272
    stp x0, x1, [sp, #0]
    mov x1, #14
    b exc_common
.balign 128
    sub sp, sp, #272
    stp x0, x1, [sp, #0]
    mov x1, #15
    b exc_common
exc_common:
    stp x2, x3, [sp, #16]
    stp x4, x5, [sp, #32]
    stp x6, x7, [sp, #48]
    stp x8, x9, [sp, #64]
    stp x10, x11, [sp, #80]
    stp x12, x13, [sp, #96]
    stp x14, x15, [sp, #112]
    stp x16, x17, [sp, #128]
    stp x18, x19, [sp, #144]
    stp x20, x21, [sp, #160]
    stp x22, x23, [sp, #176]
    stp x24, x25, [sp, #192]
    stp x26, x27, [sp, #208]
    stp x28, x29, [sp, #224]
    str x30, [sp, #240]
    mrs x2, sp_el0
    str x2, [sp, #248]
    mrs x2, elr_el1
    mrs x3, spsr_el1
    stp x2, x3, [sp, #256]
    mov x0, sp
    bl arm_exception
    mov sp, x0
    ldr x2, [sp, #248]
    msr sp_el0, x2
    ldp x2, x3, [sp, #256]
    msr elr_el1, x2
    msr spsr_el1, x3
    ldp x0, x1, [sp, #0]
    ldp x2, x3, [sp, #16]
    ldp x4, x5, [sp, #32]
    ldp x6, x7, [sp, #48]
    ldp x8, x9, [sp, #64]
    ldp x10, x11, [sp, #80]
    ldp x12, x13, [sp, #96]
    ldp x14, x15, [sp, #112]
    ldp x16, x17, [sp, #128]
    ldp x18, x19, [sp, #144]
    ldp x20, x21, [sp, #160]
    ldp x22, x23, [sp, #176]
    ldp x24, x25, [sp, #192]
    ldp x26, x27, [sp, #208]
    ldp x28, x29, [sp, #224]
    ldr x30, [sp, #240]
    add sp, sp, #272
    eret
