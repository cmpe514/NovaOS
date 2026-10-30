/* Interrupt entry stubs. Each pushes (error code, vector), saves all registers and calls
 * isr_dispatch(frame), which returns the frame to resume (possibly another task's). */
.section .text
.code64
isr0:
    push $0
    push $0
    jmp isr_common
isr1:
    push $0
    push $1
    jmp isr_common
isr2:
    push $0
    push $2
    jmp isr_common
isr3:
    push $0
    push $3
    jmp isr_common
isr4:
    push $0
    push $4
    jmp isr_common
isr5:
    push $0
    push $5
    jmp isr_common
isr6:
    push $0
    push $6
    jmp isr_common
isr7:
    push $0
    push $7
    jmp isr_common
isr8:
    push $8
    jmp isr_common
isr9:
    push $0
    push $9
    jmp isr_common
isr10:
    push $10
    jmp isr_common
isr11:
    push $11
    jmp isr_common
isr12:
    push $12
    jmp isr_common
isr13:
    push $13
    jmp isr_common
isr14:
    push $14
    jmp isr_common
isr15:
    push $0
    push $15
    jmp isr_common
isr16:
    push $0
    push $16
    jmp isr_common
isr17:
    push $17
    jmp isr_common
isr18:
    push $0
    push $18
    jmp isr_common
isr19:
    push $0
    push $19
    jmp isr_common
isr20:
    push $0
    push $20
    jmp isr_common
isr21:
    push $21
    jmp isr_common
isr22:
    push $0
    push $22
    jmp isr_common
isr23:
    push $0
    push $23
    jmp isr_common
isr24:
    push $0
    push $24
    jmp isr_common
isr25:
    push $0
    push $25
    jmp isr_common
isr26:
    push $0
    push $26
    jmp isr_common
isr27:
    push $0
    push $27
    jmp isr_common
isr28:
    push $0
    push $28
    jmp isr_common
isr29:
    push $29
    jmp isr_common
isr30:
    push $30
    jmp isr_common
isr31:
    push $0
    push $31
    jmp isr_common
isr32:
    push $0
    push $32
    jmp isr_common
isr33:
    push $0
    push $33
    jmp isr_common
isr34:
    push $0
    push $34
    jmp isr_common
isr35:
    push $0
    push $35
    jmp isr_common
isr36:
    push $0
    push $36
    jmp isr_common
isr37:
    push $0
    push $37
    jmp isr_common
isr38:
    push $0
    push $38
    jmp isr_common
isr39:
    push $0
    push $39
    jmp isr_common
isr40:
    push $0
    push $40
    jmp isr_common
isr41:
    push $0
    push $41
    jmp isr_common
isr42:
    push $0
    push $42
    jmp isr_common
isr43:
    push $0
    push $43
    jmp isr_common
isr44:
    push $0
    push $44
    jmp isr_common
isr45:
    push $0
    push $45
    jmp isr_common
isr46:
    push $0
    push $46
    jmp isr_common
isr47:
    push $0
    push $47
    jmp isr_common
isr128:
    push $0
    push $128
    jmp isr_common
isr_common:
    push %rax
    push %rbx
    push %rcx
    push %rdx
    push %rsi
    push %rdi
    push %rbp
    push %r8
    push %r9
    push %r10
    push %r11
    push %r12
    push %r13
    push %r14
    push %r15
    mov %rsp, %rdi
    cld
    call isr_dispatch
    mov %rax, %rsp
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rbp
    pop %rdi
    pop %rsi
    pop %rdx
    pop %rcx
    pop %rbx
    pop %rax
    add $16, %rsp
    iretq

.section .rodata
.align 8
.global isr_table
isr_table:
    .quad isr0
    .quad isr1
    .quad isr2
    .quad isr3
    .quad isr4
    .quad isr5
    .quad isr6
    .quad isr7
    .quad isr8
    .quad isr9
    .quad isr10
    .quad isr11
    .quad isr12
    .quad isr13
    .quad isr14
    .quad isr15
    .quad isr16
    .quad isr17
    .quad isr18
    .quad isr19
    .quad isr20
    .quad isr21
    .quad isr22
    .quad isr23
    .quad isr24
    .quad isr25
    .quad isr26
    .quad isr27
    .quad isr28
    .quad isr29
    .quad isr30
    .quad isr31
    .quad isr32
    .quad isr33
    .quad isr34
    .quad isr35
    .quad isr36
    .quad isr37
    .quad isr38
    .quad isr39
    .quad isr40
    .quad isr41
    .quad isr42
    .quad isr43
    .quad isr44
    .quad isr45
    .quad isr46
    .quad isr47
.global isr_syscall
isr_syscall: .quad isr128
