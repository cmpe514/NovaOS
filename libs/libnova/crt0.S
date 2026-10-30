/* User program entry: rdi = argument from the parent. main(arg) returns the exit code. */
.section .text
.global _start
_start:
    and $-16, %rsp
    call main
    mov %rax, %rdi
    mov $0, %rax          /* SYS_EXIT */
    int $0x80
1:  jmp 1b
