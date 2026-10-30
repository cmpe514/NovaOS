/* PS/2 keyboard through the i8042 controller: scancode set 1 (controller translation on), IRQ 1.
 * Scancodes are translated to Linux key codes for kernel/input.c. */
#include "kernel.h"
static bool ext;
void pic_unmask(int irq);

static bool wait_in(void) { for (int i = 0; i < 100000; i++) if (!(inb(0x64) & 2)) return true; return false; }
static bool wait_out(void) { for (int i = 0; i < 100000; i++) if (inb(0x64) & 1) return true; return false; }

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
    return true;
}

void kbd_irq(void) {
    static const uint8_t ext_map[][2] = { { 0x48, 103 }, { 0x50, 108 }, { 0x4b, 105 }, { 0x4d, 106 }, { 0x47, 102 },
                                          { 0x4f, 107 }, { 0x53, 111 }, { 0x1c, 96 }, { 0x5b, 125 }, { 0x5c, 126 } };
    while (inb(0x64) & 1) {
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
