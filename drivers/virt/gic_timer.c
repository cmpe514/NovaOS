/* GICv2 (distributor 0x08000000, CPU interface 0x08010000) and the EL1 virtual generic timer (PPI 27). */
#include "kernel.h"
#define GICD 0x08000000ull
#define GICC 0x08010000ull
#define TIMER_IRQ 27
void virtio_irq(int intid);
static uint64_t timer_reload;

void gic_enable(int id) {
    mmio_w32(GICD + 0x100 + 4 * (id / 32), 1u << (id % 32));
    *(volatile uint8_t *)(GICD + 0x400 + id) = 0xa0;            /* priority */
    if (id >= 32) *(volatile uint8_t *)(GICD + 0x800 + id) = 1; /* target CPU 0 */
}

void gic_init(void) {
    mmio_w32(GICD, 0);
    mmio_w32(GICD, 1);
    mmio_w32(GICC + 0x4, 0xff);   /* priority mask: allow all */
    mmio_w32(GICC, 1);
}

void timer_init(uint32_t hz) {
    uint64_t frq;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frq));
    timer_reload = frq / hz;
    __asm__ volatile("msr cntv_tval_el0, %0\n msr cntv_ctl_el0, %1\n isb" :: "r"(timer_reload), "r"(1ull));
    gic_enable(TIMER_IRQ);
}

frame_t *gic_irq(frame_t *f) {
    uint32_t iar = mmio_r32(GICC + 0x0c), id = iar & 0x3ff;
    if (id >= 1020) return f;                                    /* spurious */
    if (id == TIMER_IRQ) __asm__ volatile("msr cntv_tval_el0, %0" :: "r"(timer_reload));
    else virtio_irq((int)id);
    mmio_w32(GICC + 0x10, iar);
    return id == TIMER_IRQ ? kernel_tick(f) : f;
}
