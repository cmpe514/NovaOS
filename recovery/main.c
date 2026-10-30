/* Nova Recovery: an independent program (own binary, no shell code) using the same theme. It needs no writable
 * user data. Started from the "Nova Recovery" boot entry, or by init when the shell keeps crashing.
 * If there is no usable display it falls back to a text summary on the serial console. */
#include "novaui.h"

typedef struct { const char *title, *detail; bool enabled; } item_t;
static const item_t items[] = {
    { "Boot normal system", "Leave recovery and start the NovaOS shell.", true },
    { "System information", "Kernel, memory, devices and drivers.", true },
    { "Restart", "Restart the computer.", true },
    { "Power off", "Turn the computer off.", true },
    { "Boot safe mode", "Not available yet: safe mode needs loadable drivers (milestone M4).", false },
    { "Previous system image", "Not available yet: needs A/B system slots on disk (milestone M4).", false },
    { "Install signed update", "Not available yet: needs storage and the update verifier (milestone M4).", false },
    { "Storage and logs", "Not available yet: needs the virtio-blk driver (milestone M2).", false },
    { "Back up or restore data", "Not available yet: needs storage (milestone M2).", false },
    { "Repair file system", "Not available yet: needs the NovaOS filesystem (milestone M2).", false },
    { "Reset settings", "Not available yet: settings are not stored on disk yet (milestone M2).", false },
    { "Factory reset", "Not available yet: there is no user data on disk to erase yet (milestone M2).", false },
};
#define NITEMS (int)(sizeof items / sizeof *items)
static int sel, panel = -1;
static nova_time now;

static void draw(void) {
    memcpy(ui.back.px, ui.wall.px, (size_t)ui.w * ui.h * 4);
    gfx_fill(&ui.back, (rect){ 0, 0, ui.w, ui.h }, C_APPVIEW, 0xb0);
    int x = ui.w > 1100 + 96 ? (ui.w - 1100) / 2 : 48, y = ui_y(54) + 40;
    gfx_text(&ui.back, &font_small, x, y, "NOVA RECOVERY", NOVA_ACCENT, 64);
    gfx_text(&ui.back, &font_title, x, y + 40, "How can we help?", C_TEXT, 0);
    gfx_text(&ui.back, &font_body, x, y + 72, "Recovery runs from the system image only and never needs your data.", C_SOFT, 0);
    int lw = 470, ly = y + 100, rowh = ui.h > 820 ? 46 : 40;
    rect list = { x, ly, lw, NITEMS * rowh + 20 };
    ui_glass(list, 26);
    for (int i = 0; i < NITEMS; i++) {
        rect r = { x + 10, ly + 10 + i * rowh, lw - 20, rowh - 4 };
        if (i == sel) gfx_round(&ui.back, r, 14, 0xffffff, 0x1e);
        gfx_text(&ui.back, &font_body, r.x + 16, r.y + rowh / 2 + 3, items[i].title, items[i].enabled ? C_TEXT : 0x7d8ca0, 0);
        if (!items[i].enabled) gfx_text(&ui.back, &font_small, r.x + r.w - 100, r.y + rowh / 2 + 3, "Unavailable", 0x7d8ca0, 0);
        if (i == sel) ui_focus_ring(r, 14);
    }
    rect d = { x + lw + 24, ly, ui.w - 2 * x - lw - 24, 260 };
    if (d.w > 300) {
        ui_glass(d, 26);
        int dx = d.x + 26, dy = d.y + 44;
        if (panel == 1) {
            nova_sysinfo si; sys_sysinfo(&si);
            char b[96];
            gfx_text(&ui.back, &font_h2, dx, dy, "System information", C_TEXT, 0); dy += 40;
            b[0] = 0; str_cat(b, "NovaOS kernel "); str_cat(b, si.version); gfx_text(&ui.back, &font_body, dx, dy, b, C_SOFT, 0); dy += 28;
            gfx_text(&ui.back, &font_body, dx, dy, si.cpu, C_SOFT, 0); dy += 28;
            b[0] = 0; fmt_u(b, si.mem_free >> 20, 1); str_cat(b, " MiB free of "); fmt_u(b + strlen(b), si.mem_total >> 20, 1); str_cat(b, " MiB");
            gfx_text(&ui.back, &font_body, dx, dy, b, C_SOFT, 0); dy += 28;
            b[0] = 0; fmt_u(b, si.pci_devices, 1); str_cat(b, " PCI devices, keyboard "); str_cat(b, si.kbd_ok ? "OK" : "missing");
            gfx_text(&ui.back, &font_body, dx, dy, b, C_SOFT, 0); dy += 28;
            gfx_text(&ui.back, &font_body, dx, dy, "Storage and network drivers: not implemented yet", 0xf6a9a4, 0);
        } else {
            gfx_text(&ui.back, &font_h2, dx, dy, items[sel].title, C_TEXT, 0);
            gfx_text_wrap(&ui.back, &font_body, dx, dy + 40, d.w - 52, 24, items[sel].detail, C_SOFT);
        }
    }
    gfx_text(&ui.back, &font_small, x, list.y + list.h + 30, "Up/Down: choose    Enter: select    Esc: close panel", C_MUTED, 0);
    ui_status_bar(&now, "Recovery");
    ui_present();
}

static void serial_menu(void) {
    puts_("Nova Recovery (text mode: no usable display)\n");
    for (int i = 0; i < NITEMS; i++) { puts_(items[i].enabled ? "  [ ] " : "  [-] "); puts_(items[i].title); puts_("\n"); }
}

int main(void) {
    sys_time(&now);
    if (!ui_init()) { serial_menu(); puts_("NOVA_RECOVERY_READY text\n"); for (;;) sys_sleep(1000); }
    draw();
    puts_("NOVA_RECOVERY_READY graphical\n");
    for (;;) {
        nova_input_event ev[16];
        int64_t n = sys_input_read(ev, 16);
        bool dirty = false;
        for (int i = 0; i < n; i++) {
            if (!ev[i].pressed) continue;
            int k = ev[i].key; dirty = true;
            if (k == KEY_UP) { sel = (sel + NITEMS - 1) % NITEMS; panel = -1; }
            else if (k == KEY_DOWN || k == KEY_TAB) { sel = (sel + 1) % NITEMS; panel = -1; }
            else if (k == KEY_ESC) panel = -1;
            else if (k == KEY_ENTER && items[sel].enabled) {
                if (sel == 0) { puts_("recovery: starting the normal system\n"); return 0x77; }
                if (sel == 1) panel = 1;
                if (sel == 2) sys_power(POWER_REBOOT);
                if (sel == 3) sys_power(POWER_OFF);
            }
        }
        if (dirty) draw(); else sys_sleep(30);
    }
}
