/* GDT, TSS and IDT for x86-64. */
#include <stdint.h>
#include "kernel.h"

struct tss { uint32_t r0; uint64_t rsp0, rsp1, rsp2, r1, ist[7], r2; uint16_t r3, iomap; } __attribute__((packed));
struct idt_entry { uint16_t off0, sel; uint8_t ist, attr; uint16_t off1; uint32_t off2, zero; } __attribute__((packed));
struct dtr { uint16_t limit; uint64_t base; } __attribute__((packed));

static uint64_t gdt[7];
static struct tss tss;
static struct idt_entry idt[256];
extern const uint64_t isr_table[48];
extern const uint64_t isr_syscall;
static uint8_t df_stack[8192] __attribute__((aligned(16)));

static void idt_set(int v, uint64_t addr, int dpl, int ist) {
    idt[v] = (struct idt_entry){ (uint16_t)addr, KCODE_SEL, (uint8_t)ist, (uint8_t)(0x8e | (dpl << 5)),
                                 (uint16_t)(addr >> 16), (uint32_t)(addr >> 32), 0 };
}

void cpu_init(void) {
    gdt[0] = 0;
    gdt[1] = 0x00af9a000000ffffull;   /* 0x08 kernel code */
    gdt[2] = 0x00cf92000000ffffull;   /* 0x10 kernel data */
    gdt[3] = 0x00cff2000000ffffull;   /* 0x18 user data (DPL 3) */
    gdt[4] = 0x00affa000000ffffull;   /* 0x20 user code (DPL 3) */
    uint64_t b = (uint64_t)&tss, l = sizeof tss - 1;
    gdt[5] = (l & 0xffff) | ((b & 0xffffff) << 16) | (0x89ull << 40) | (((l >> 16) & 0xf) << 48) | (((b >> 24) & 0xff) << 56);
    gdt[6] = b >> 32;
    tss.iomap = sizeof tss;           /* no I/O permission bitmap: user mode has no port access */
    tss.ist[0] = (uint64_t)(df_stack + sizeof df_stack);
    struct dtr g = { sizeof gdt - 1, (uint64_t)gdt };
    __asm__ volatile("lgdt %0\n"
                     "pushq $0x08\n lea 1f(%%rip), %%rax\n pushq %%rax\n lretq\n1:\n"
                     "mov $0x10, %%ax\n mov %%ax, %%ds\n mov %%ax, %%es\n mov %%ax, %%ss\n"
                     "mov $0x28, %%ax\n ltr %%ax" :: "m"(g) : "rax", "memory");
    for (int v = 0; v < 48; v++) idt_set(v, isr_table[v], 0, v == 8 ? 1 : 0);
    idt_set(0x80, isr_syscall, 3, 0);  /* only the syscall gate is callable from ring 3 */
    struct dtr i = { sizeof idt - 1, (uint64_t)idt };
    __asm__ volatile("lidt %0" :: "m"(i));
}

void cpu_set_kernel_stack(uint64_t top) { tss.rsp0 = top; }
