/* PS/2 keyboard through the i8042 controller: scancode set 1 (controller translation on), IRQ 1.
 * Scancodes are translated to Linux key codes for kernel/input.c. */
#include "kernel.h"
static bool ext;
void pic_unmask(int irq);

static bool wait_in(void) { for (int i = 0; i < 100000; i++) if (!(inb(0x64) & 2)) return true; return false; }
static bool wait_out(void) { for (int i = 0; i < 100000; i++) if (inb(0x64) & 1) return true; return false; }

/* PS/2 mouse on the i8042 auxiliary port (IRQ 12): 3-byte packets, relative motion. */
static bool mouse_ok;
static uint8_t pkt[3];
static int pkt_n;
static bool aux_cmd(uint8_t c) {
    if (!wait_in()) return false;
    outb(0x64, 0xd4);
    if (!wait_in()) return false;
    outb(0x60, c);
    return wait_out() && inb(0x60) == 0xfa;                   /* ACK */
}
static void mouse_init(void) {
    if (!wait_in()) return;
    outb(0x64, 0xa8);                                         /* enable the aux port */
    if (!wait_in()) return;
    outb(0x64, 0x20);
    if (!wait_out()) return;
    uint8_t cfg = (inb(0x60) | 2) & ~0x20;                    /* IRQ 12 on, aux clock on */
    if (!wait_in()) return;
    outb(0x64, 0x60);
    if (!wait_in()) return;
    outb(0x60, cfg);
    if (!aux_cmd(0xf6) || !aux_cmd(0xf4)) { kprintf("ps2: no mouse\n"); return; }
    mouse_ok = true;
    pic_unmask(12);
    kprintf("ps2: mouse ready\n");
}
static void mouse_byte(uint8_t b) {
    if (pkt_n == 0 && !(b & 0x08)) return;                    /* resynchronise on the always-1 bit */
    pkt[pkt_n++] = b;
    if (pkt_n < 3) return;
    pkt_n = 0;
    int dx = pkt[1] - ((pkt[0] & 0x10) ? 256 : 0), dy = pkt[2] - ((pkt[0] & 0x20) ? 256 : 0);
    input_pointer_rel(dx, -dy, pkt[0] & 1);
}

bool kbd_init(void) {
    if (inb(0x64) == 0xff) return false;                      /* no controller */
    while (inb(0x64) & 1) inb(0x60);
    if (!wait_in()) return false;
    outb(0x64, 0x20);
    if (!wait_out()) return false;
    uint8_t cfg = inb(0x60);
    cfg |= 1 | 0x40;                                          /* IRQ 1 on, set-1 translation on */
    cfg &= ~0x10;                                             /* keyboard clock enabled */
    if (!wait_in()) return false;
    outb(0x64, 0x60);
    if (!wait_in()) return false;
    outb(0x60, cfg);
    if (!wait_in()) return false;
    outb(0x64, 0xae);                                         /* enable first port */
    pic_unmask(1);
    mouse_init();
    return true;
}

void kbd_irq(void) {
    static const uint8_t ext_map[][2] = { { 0x48, 103 }, { 0x50, 108 }, { 0x4b, 105 }, { 0x4d, 106 }, { 0x47, 102 },
                                          { 0x4f, 107 }, { 0x53, 111 }, { 0x1c, 96 }, { 0x5b, 125 }, { 0x5c, 126 } };
    uint8_t st;
    while ((st = inb(0x64)) & 1) {
        if (st & 0x20) { uint8_t b = inb(0x60); if (mouse_ok) mouse_byte(b); continue; }   /* aux (mouse) byte */
        uint8_t sc = inb(0x60);
        if (sc == 0xe0) { ext = true; continue; }
        bool pressed = !(sc & 0x80);
        uint8_t code = sc & 0x7f;
        if (ext) {
            ext = false;
            for (unsigned i = 0; i < sizeof ext_map / sizeof *ext_map; i++)
                if (ext_map[i][0] == code) input_key(ext_map[i][1], pressed);
            continue;
        }
        input_key(code, pressed);   /* set-1 codes below 0x59 equal Linux key codes */
    }
}
