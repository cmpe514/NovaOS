/* Keyboard event queue. Drivers report Linux input key codes; for the codes below 0x3a these equal PS/2
 * scancode set 1, so one table serves the PS/2 and virtio-input drivers. */
#include "kernel.h"

static nova_input_event q[64];
static volatile unsigned qh, qt;
static bool shift;

static const char map[0x3a] = { 0, 27, '1','2','3','4','5','6','7','8','9','0','-','=','\b','\t','q','w','e','r','t','y',
    'u','i','o','p','[',']','\n', 0, 'a','s','d','f','g','h','j','k','l',';','\'','`', 0,'\\','z','x','c','v','b','n','m',
    ',','.','/', 0, '*', 0, ' ' };
static const char smap[0x3a] = { 0, 27, '!','@','#','$','%','^','&','*','(',')','_','+','\b','\t','Q','W','E','R','T','Y',
    'U','I','O','P','{','}','\n', 0, 'A','S','D','F','G','H','J','K','L',':','"','~', 0,'|','Z','X','C','V','B','N','M',
    '<','>','?', 0, '*', 0, ' ' };

static void push(uint16_t key, bool pressed) {
    unsigned n = (qh + 1) % 64;
    if (n == qt) return;   /* full: drop */
    q[qh] = (nova_input_event){ key, pressed, shift, 0, 0 };
    qh = n;
}

void input_key(uint16_t c, bool pressed) {
    if (c == 42 || c == 54) { shift = pressed; return; }
    uint16_t k = c == 103 ? KEY_UP : c == 108 ? KEY_DOWN : c == 105 ? KEY_LEFT : c == 106 ? KEY_RIGHT :
                 c == 102 ? KEY_HOME : c == 107 ? KEY_END : c == 111 ? KEY_DELETE : c == 96 ? '\n' :
                 (c == 125 || c == 126) ? KEY_SUPER : c == 59 ? KEY_F1 : c == 60 ? KEY_F2 : 0;
    if (!k && c < sizeof map && map[c]) k = (uint8_t)(shift ? smap[c] : map[c]);
    if (k) push(k, pressed);
}

/* Pointer: one position for the whole system, in framebuffer pixels. Consecutive moves are merged. */
static int px = -1, py = -1, pbtn;
static void pointer_event(void) {
    unsigned last = (qh + 63) % 64;
    if (qh != qt && q[last].key == KEY_POINTER && q[last].pressed == pbtn) { q[last].x = (uint16_t)px; q[last].y = (uint16_t)py; return; }
    unsigned n = (qh + 1) % 64;
    if (n == qt) return;
    q[qh] = (nova_input_event){ KEY_POINTER, (uint8_t)pbtn, shift, (uint16_t)px, (uint16_t)py };
    qh = n;
}
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
void input_pointer_rel(int dx, int dy, int buttons) {
    if (px < 0) { px = (int)fb.width / 2; py = (int)fb.height / 2; }
    px = clampi(px + dx, 0, (int)fb.width - 1); py = clampi(py + dy, 0, (int)fb.height - 1);
    if (buttons >= 0) pbtn = buttons & 1;
    pointer_event();
}
void input_pointer_abs(int x, int y, int buttons) {
    px = clampi((int)((int64_t)x * fb.width / 32768), 0, (int)fb.width - 1);
    py = clampi((int)((int64_t)y * fb.height / 32768), 0, (int)fb.height - 1);
    if (buttons >= 0) pbtn = buttons & 1;
    pointer_event();
}

int input_read(nova_input_event *ev, int max) {
    int n = 0;
    irq_disable();
    while (n < max && qt != qh) { ev[n++] = q[qt]; qt = (qt + 1) % 64; }
    irq_enable();
    return n;
}
