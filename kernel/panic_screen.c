/* Last-resort error screen drawn by the kernel itself (no userspace needed), in the NovaOS theme. */
#include "kernel.h"
#include "gfx.h"

void fb_panic_screen(const char *msg) {
    if (!fb.ok) return;
    surface s = { (uint32_t *)fb.phys, (int)fb.width, (int)fb.height, (int)(fb.pitch / 4) };
    gfx_fill(&s, (rect){ 0, 0, s.w, s.h }, 0x0b1728, 255);
    rect p = { s.w / 2 - 300, s.h / 2 - 110, 600, 220 };
    gfx_round(&s, p, 26, 0x10213b, 0xd9);
    gfx_round_border(&s, p, 26, 0xffffff, 0x26);
    gfx_text(&s, &font_small, p.x + 32, p.y + 46, "NOVAOS", NOVA_ACCENT, 48);
    gfx_text(&s, &font_h2, p.x + 32, p.y + 86, "NovaOS stopped", 0xffffff, 0);
    gfx_text_wrap(&s, &font_body, p.x + 32, p.y + 124, p.w - 64, 22, msg, 0xd8e4f0);
    gfx_text(&s, &font_small, p.x + 32, p.y + 190, "Restart the computer. Choose Nova Recovery in the boot menu if this repeats.", 0xbdcce0, 0);
}
