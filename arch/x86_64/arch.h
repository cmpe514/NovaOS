/* x86-64 definitions used by the portable kernel. */
#pragma once
#include <stdint.h>
#include "io.h"

typedef struct frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8, rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vec, err, rip, cs, rflags, rsp, ss;
} frame_t;

#define SYSCALL_NR(f)      ((f)->rax)
#define SYSCALL_ARG0(f)    ((f)->rdi)
#define SYSCALL_ARG1(f)    ((f)->rsi)
#define SYSCALL_ARG2(f)    ((f)->rdx)
#define SYSCALL_RET(f)     ((f)->rax)
#define SYSCALL_RESTART(f) ((f)->rip -= 2)      /* re-execute `int $0x80` (rax still holds the number) */

#define KERNEL_RAM_BASE  0x0ull
#define KERNEL_RAM_LIMIT 0x40000000ull          /* RAM below 1 GiB is identity mapped, supervisor-only */

static inline void irq_disable(void) { __asm__ volatile("cli" ::: "memory"); }
static inline void irq_enable(void) { __asm__ volatile("sti" ::: "memory"); }
static inline void cpu_wait(void) { __asm__ volatile("hlt"); }
