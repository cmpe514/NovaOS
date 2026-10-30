/* PL011 UART at 0x09000000 (QEMU virt): kernel log and test markers. QEMU needs no baud-rate setup. */
#include "kernel.h"
#define UART 0x09000000ull
void serial_putc(char c) {
    if (c == '\n') serial_putc('\r');
    for (int i = 0; i < 100000 && (mmio_r32(UART + 0x18) & (1 << 5)); i++) {}   /* TX FIFO full */
    mmio_w32(UART, (uint8_t)c);
}
