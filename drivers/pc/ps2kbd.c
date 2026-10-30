/* PS/2 keyboard through the i8042 controller: scancode set 1 (controller translation on), IRQ 1. */
#include "kernel.h"
bool kbd_ok;
static nova_input_event q[64];
static volatile unsigned qh, qt;
static bool shift, ext;

static const char map[0x3a] = { 0, 27, '1','2','3','4','5','6','7','8','9','0','-','=','\b','\t','q','w','e','r','t','y',
    'u','i','o','p','[',']','\n', 0, 'a','s','d','f','g','h','j','k','l',';','\'','`', 0,'\\','z','x','c','v','b','n','m',
    ',','.','/', 0, '*', 0, ' ' };
static const char smap[0x3a] = { 0, 27, '!','@','#','$','%','^','&','*','(',')','_','+','\b','\t','Q','W','E','R','T','Y',
    'U','I','O','P','{','}','\n', 0, 'A','S','D','F','G','H','J','K','L',':','"','~', 0,'|','Z','X','C','V','B','N','M',
    '<','>','?', 0, '*', 0, ' ' };

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

static void push(uint16_t key, bool pressed) {
    unsigned n = (qh + 1) % 64;
    if (n == qt) return;                                      /* queue full: drop */
    q[qh] = (nova_input_event){ key, pressed, shift };
    qh = n;
}

void kbd_irq(void) {
    while (inb(0x64) & 1) {
        uint8_t sc = inb(0x60);
        if (sc == 0xe0) { ext = true; continue; }
        bool pressed = !(sc & 0x80);
        uint8_t code = sc & 0x7f;
        if (ext) {
            ext = false;
            uint16_t k = code == 0x48 ? KEY_UP : code == 0x50 ? KEY_DOWN : code == 0x4b ? KEY_LEFT :
                         code == 0x4d ? KEY_RIGHT : code == 0x47 ? KEY_HOME : code == 0x4f ? KEY_END :
                         code == 0x53 ? KEY_DELETE : code == 0x1c ? '\n' : (code == 0x5b || code == 0x5c) ? KEY_SUPER : 0;
            if (k) push(k, pressed);
            continue;
        }
        if (code == 0x2a || code == 0x36) { shift = pressed; continue; }
        if (code == 0x3b) { push(KEY_F1, pressed); continue; }
        if (code == 0x3c) { push(KEY_F2, pressed); continue; }
        if (code < sizeof map && map[code]) push((uint8_t)(shift ? smap[code] : map[code]), pressed);
    }
}

int kbd_read(nova_input_event *ev, int max) {
    int n = 0;
    __asm__ volatile("cli");
    while (n < max && qt != qh) { ev[n++] = q[qt]; qt = (qt + 1) % 64; }
    __asm__ volatile("sti");
    return n;
}
