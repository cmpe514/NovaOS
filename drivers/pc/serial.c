/* 16550 UART on COM1 (0x3f8): kernel log and test markers. Polled output, 115200 8N1. */
#include "kernel.h"
#define COM1 0x3f8
static bool present;
void serial_init(void) {
    outb(COM1 + 1, 0); outb(COM1 + 3, 0x80); outb(COM1 + 0, 1); outb(COM1 + 1, 0);
    outb(COM1 + 3, 0x03); outb(COM1 + 2, 0xc7); outb(COM1 + 4, 0x0b);
    outb(COM1 + 7, 0x5a);
    present = inb(COM1 + 7) == 0x5a;   /* scratch register test */
}
void serial_putc(char c) {
    if (!present) return;
    if (c == '\n') serial_putc('\r');
    for (int i = 0; i < 100000 && !(inb(COM1 + 5) & 0x20); i++) {}
    outb(COM1, (uint8_t)c);
}
