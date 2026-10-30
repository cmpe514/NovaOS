/* User program entry: first argument register = argument from the parent. main(arg) returns the exit code. */
.section .text
.global _start
_start:
#if defined(__aarch64__)
    bl main
    mov x8, #0            /* SYS_EXIT, code already in x0 */
    svc #0
1:  b 1b
#else
    and $-16, %rsp
    call main
    mov %rax, %rdi
    mov $0, %rax          /* SYS_EXIT */
    int $0x80
1:  jmp 1b
#endif
