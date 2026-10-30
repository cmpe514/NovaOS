/* CMOS real-time clock, PCI configuration-space scan (mechanism #1), and QEMU power control. */
#include "kernel.h"

static uint8_t cmos(uint8_t r) { outb(0x70, r); return inb(0x71); }
static uint8_t bcd(uint8_t v, bool is_bcd) { return is_bcd ? (v & 15) + (v >> 4) * 10 : v; }

void rtc_read(nova_time *t) {
    uint8_t s, m, h, d, mo, y, regb;
    do {
        while (cmos(0x0a) & 0x80) {}
        s = cmos(0); m = cmos(2); h = cmos(4); d = cmos(7); mo = cmos(8); y = cmos(9);
    } while (s != cmos(0));
    regb = cmos(0x0b);
    bool b = !(regb & 4), pm = h & 0x80;
    h = bcd(h & 0x7f, b);
    if (!(regb & 2)) h = (h % 12) + (pm ? 12 : 0);   /* 12-hour mode */
    t->second = bcd(s, b); t->minute = bcd(m, b); t->hour = h; t->day = bcd(d, b); t->month = bcd(mo, b);
    t->year = 2000 + bcd(y, b);
    static const int k[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };   /* Sakamoto's weekday, 0 = Sunday */
    int yy = t->year - (t->month < 3);
    t->weekday = (yy + yy / 4 - yy / 100 + yy / 400 + k[(t->month - 1) % 12] + t->day) % 7;
}

static int pci_count;
uint32_t pci_read(int bus, int dev, int fn, int off) {
    outl(0xcf8, 0x80000000u | bus << 16 | dev << 11 | fn << 8 | (off & 0xfc));
    return inl(0xcfc);
}
void pci_write(int bus, int dev, int fn, int off, uint32_t v) {
    outl(0xcf8, 0x80000000u | bus << 16 | dev << 11 | fn << 8 | (off & 0xfc));
    outl(0xcfc, v);
}
void virtio_pci_probe(int bus, int dev, int fn, uint16_t device);
int pci_scan(void) {
    for (int bus = 0; bus < 8; bus++)
        for (int dev = 0; dev < 32; dev++)
            for (int fn = 0; fn < 8; fn++) {
                uint32_t id = pci_read(bus, dev, fn, 0);
                if ((id & 0xffff) == 0xffff) { if (fn == 0) break; continue; }
                uint32_t cls = pci_read(bus, dev, fn, 8);
                kprintf("pci %u:%u.%u %4x:%4x class %2x.%2x\n", bus, dev, fn, id & 0xffff, id >> 16, cls >> 24, (cls >> 16) & 0xff);
                pci_count++;
                if ((id & 0xffff) == 0x1af4) virtio_pci_probe(bus, dev, fn, id >> 16);
                if (fn == 0 && !(pci_read(bus, dev, 0, 12) >> 16 & 0x80)) break;   /* single-function device */
            }
    return pci_count;
}

/* No ACPI interpreter yet: these use the fixed ports of QEMU's PIIX4/ICH9 power management (S5 via PM1a_CNT
 * at 0x604) and the i8042 reset line. They are QEMU-specific and labelled as such in docs/DRIVERS.md. */
__attribute__((noreturn)) void power_off(void) {
    kprintf("NOVA_POWER_OFF\n");
    __asm__ volatile("cli");
    outw(0x604, 0x2000);
    outw(0xb004, 0x2000);
    for (;;) __asm__ volatile("hlt");
}
__attribute__((noreturn)) void power_reboot(void) {
    kprintf("NOVA_REBOOT\n");
    __asm__ volatile("cli");
    for (int i = 0; i < 100000 && (inb(0x64) & 2); i++) {}
    outb(0x64, 0xfe);
    for (;;) __asm__ volatile("hlt");
}
