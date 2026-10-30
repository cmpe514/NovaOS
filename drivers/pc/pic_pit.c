/* 8259A interrupt controllers (remapped to vectors 32-47) and the 8254 timer (IRQ 0). */
#include "kernel.h"
volatile uint64_t ticks;
void pic_init(void) {
    outb(0x20, 0x11); io_wait(); outb(0xa0, 0x11); io_wait();
    outb(0x21, 32); io_wait(); outb(0xa1, 40); io_wait();
    outb(0x21, 4); io_wait(); outb(0xa1, 2); io_wait();
    outb(0x21, 1); io_wait(); outb(0xa1, 1); io_wait();
    outb(0x21, 0xff); outb(0xa1, 0xff);   /* everything masked until a driver asks */
    pic_unmask(2);
}
void pic_unmask(int irq) {
    uint16_t port = irq < 8 ? 0x21 : 0xa1;
    outb(port, inb(port) & ~(1 << (irq & 7)));
}
void pic_eoi(int irq) { if (irq >= 8) outb(0xa0, 0x20); outb(0x20, 0x20); }
void pit_init(uint32_t hz) {
    uint32_t div = 1193182 / hz;
    outb(0x43, 0x36); outb(0x40, div & 0xff); outb(0x40, div >> 8);
    pic_unmask(0);
}
