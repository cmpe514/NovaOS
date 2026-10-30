/* AArch64 definitions used by the portable kernel. */
#pragma once
#include <stdint.h>

typedef struct frame {
    uint64_t x[31];          /* x0..x30 */
    uint64_t sp_el0, elr, spsr;
} frame_t;

#define SYSCALL_NR(f)      ((f)->x[8])
#define SYSCALL_ARG0(f)    ((f)->x[0])
#define SYSCALL_ARG1(f)    ((f)->x[1])
#define SYSCALL_ARG2(f)    ((f)->x[2])
#define SYSCALL_RET(f)     ((f)->x[0])
#define SYSCALL_RESTART(f) ((f)->elr -= 4)      /* re-execute `svc #0` (x0..x2 and x8 are untouched) */

/* QEMU virt: devices below 1 GiB, RAM from 1 GiB. The kernel identity-maps 0-2 GiB, EL1-only. */
#define KERNEL_RAM_BASE  0x40000000ull
#define KERNEL_RAM_LIMIT 0x80000000ull

static inline void irq_disable(void) { __asm__ volatile("msr daifset, #2" ::: "memory"); }
static inline void irq_enable(void) { __asm__ volatile("msr daifclr, #2" ::: "memory"); }
static inline void cpu_wait(void) { __asm__ volatile("wfi"); }

static inline uint32_t mmio_r32(uint64_t a) { return *(volatile uint32_t *)a; }
static inline void mmio_w32(uint64_t a, uint32_t v) { *(volatile uint32_t *)a = v; }
#define dsb() __asm__ volatile("dsb sy" ::: "memory")
