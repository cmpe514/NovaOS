/* Nova Recovery: an independent program (own binary, no shell code) using the same theme. It needs no writable
 * user data. Started from the "Nova Recovery" boot entry, or by init when the shell keeps crashing.
 * If there is no usable display it falls back to a text summary on the serial console. */
#include "novaui.h"
#include "nova/shellstate.h"

typedef struct { const char *title, *detail; bool enabled; } item_t;
static const item_t items[] = {
    { "Boot normal system", "Leave recovery and start the NovaOS shell.", true },
    { "System information", "Kernel, memory, devices and drivers.", true },
    { "Restart", "Restart the computer.", true },
    { "Power off", "Turn the computer off.", true },
    { "Boot safe mode", "Not available yet: safe mode needs loadable drivers (milestone M4).", false },
    { "Previous system image", "Not available yet: needs A/B system slots on disk (milestone M4).", false },
    { "Install signed update", "Not available yet: needs storage and the update verifier (milestone M4).", false },
    { "Storage information", "Data disk, snapshot slots and their checksums. Logs go to the serial console only.", true },
    { "Back up or restore data", "Not available yet: needs a second disk or a USB storage driver.", false },
    { "Check and repair storage", "Verifies both snapshot slots and rewrites the newest valid snapshot so both slots are valid again.", true },
    { "Reset settings", "Restores default settings. Notes and tasks are kept.", true },
    { "Factory reset", "Erases all notes, tasks and settings on the data disk. Asks for confirmation.", true },
};
#define NITEMS (int)(sizeof items / sizeof *items)
static int sel, panel = -1;
static nova_time now;
static char msg[96], typed[8];
static nova_store_info store;
static int ptr_x = -1, ptr_y, ptr_btn;
enum { P_INFO = 1, P_STORAGE = 7, P_MSG = 100, P_ERASE = 101 };

static void set_msg(const char *a, const char *b) { msg[0] = 0; str_cat(msg, a); if (b) str_cat(msg, b); panel = P_MSG; }
static void act(int i) {
    shell_state s;
    sys_store_info(&store);
    if (i == 0) return;
    if (i == 1) { panel = P_INFO; return; }
    if (i == 2) sys_power(POWER_REBOOT);
    if (i == 3) sys_power(POWER_OFF);
    if (i == 7) { panel = P_STORAGE; return; }
    if (!store.present) { set_msg("No usable NovaOS data disk: ", store.status); return; }
    if (i == 9) {
        if (!store.has_data) { set_msg("Storage checked: no saved data, nothing to repair.", 0); return; }
        int64_t n = sys_store_read(&s, sizeof s);
        bool both = store.valid[0] && store.valid[1];
        if (n <= 0 || sys_store_write(&s, (uint64_t)n) < 0) { set_msg("Repair failed: the disk could not be written.", 0); return; }
        set_msg(both ? "Both slots were valid. The newest snapshot was rewritten." : "Repaired: one slot was invalid and has been rewritten from the valid snapshot.", 0);
        puts_("NOVA_RECOVERY_REPAIRED\n");
    } else if (i == 10) {
        if (sys_store_read(&s, sizeof s) != (int64_t)sizeof s || s.version != SHELL_STATE_VERSION) { set_msg("No saved settings: nothing to reset.", 0); return; }
        memset(&s.settings, 0, sizeof s.settings);
        set_msg(sys_store_write(&s, sizeof s) < 0 ? "Reset failed: the disk could not be written." : "Settings reset to defaults. Notes and tasks were kept.", 0);
        puts_("NOVA_RECOVERY_SETTINGS_RESET\n");
    } else if (i == 11) { panel = P_ERASE; typed[0] = 0; }
}

static void draw(void) {
    ui_hit_reset();
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
        ui_hit(r, 100 + i);
    }
    rect d = { x + lw + 24, ly, ui.w - 2 * x - lw - 24, 260 };
    if (d.w > 300) {
        ui_glass(d, 26);
        int dx = d.x + 26, dy = d.y + 44;
        if (panel == P_STORAGE) {
            char b[96];
            gfx_text(&ui.back, &font_h2, dx, dy, "Storage information", C_TEXT, 0); dy += 40;
            if (!store.present) gfx_text_wrap(&ui.back, &font_body, dx, dy, d.w - 52, 24, store.status, 0xf6a9a4);
            else {
                b[0] = 0; str_cat(b, "Data disk: "); fmt_u(b + strlen(b), store.disk_bytes >> 10, 1); str_cat(b, " KiB, virtio-blk");
                gfx_text(&ui.back, &font_body, dx, dy, b, C_SOFT, 0); dy += 28;
                for (int s = 0; s < 2; s++) {
                    b[0] = 0; str_cat(b, s ? "Slot B: " : "Slot A: "); str_cat(b, store.valid[s] ? "valid" : "empty or invalid");
                    if (store.has_data && store.slot == (uint32_t)s) str_cat(b, " (current)");
                    gfx_text(&ui.back, &font_body, dx, dy, b, store.valid[s] ? C_SOFT : 0xf6a9a4, 0); dy += 28;
                }
                b[0] = 0; str_cat(b, "Generation "); fmt_u(b + strlen(b), store.generation, 1); str_cat(b, ", "); fmt_u(b + strlen(b), store.length, 1); str_cat(b, " bytes");
                gfx_text(&ui.back, &font_body, dx, dy, b, C_SOFT, 0); dy += 28;
                gfx_text(&ui.back, &font_body, dx, dy, "Logs: kernel log on the serial console only", C_MUTED, 0);
            }
        } else if (panel == P_MSG) {
            gfx_text(&ui.back, &font_h2, dx, dy, items[sel].title, C_TEXT, 0);
            gfx_text_wrap(&ui.back, &font_body, dx, dy + 40, d.w - 52, 24, msg, C_SOFT);
        } else if (panel == P_ERASE) {
            char b[96];
            gfx_text(&ui.back, &font_h2, dx, dy, "Factory reset", 0xf6a9a4, 0);
            b[0] = 0; str_cat(b, "This erases ALL notes, tasks and settings on the data disk ("); fmt_u(b + strlen(b), store.disk_bytes >> 10, 1); str_cat(b, " KiB virtio-blk).");
            gfx_text_wrap(&ui.back, &font_body, dx, dy + 40, d.w - 52, 24, b, C_SOFT);
            gfx_text(&ui.back, &font_body, dx, dy + 100, "Type ERASE and press Enter. Esc cancels.", C_TEXT, 0);
            rect in = { dx, dy + 120, 200, 44 };
            gfx_round(&ui.back, in, 14, 0xffffff, 0x16);
            gfx_text(&ui.back, &font_h2, dx + 14, dy + 151, typed, C_TEXT, 0);
            ui_focus_ring(in, 14);
        } else if (panel == 1) {
            nova_sysinfo si; sys_sysinfo(&si);
            char b[96];
            gfx_text(&ui.back, &font_h2, dx, dy, "System information", C_TEXT, 0); dy += 40;
            b[0] = 0; str_cat(b, "NovaOS kernel "); str_cat(b, si.version); str_cat(b, " "); str_cat(b, si.arch); gfx_text(&ui.back, &font_body, dx, dy, b, C_SOFT, 0); dy += 28;
            gfx_text(&ui.back, &font_body, dx, dy, si.cpu, C_SOFT, 0); dy += 28;
            b[0] = 0; fmt_u(b, si.mem_free >> 20, 1); str_cat(b, " MiB free of "); fmt_u(b + strlen(b), si.mem_total >> 20, 1); str_cat(b, " MiB");
            gfx_text(&ui.back, &font_body, dx, dy, b, C_SOFT, 0); dy += 28;
            b[0] = 0; fmt_u(b, si.pci_devices, 1); str_cat(b, " devices, keyboard "); str_cat(b, si.kbd_ok ? "OK" : "missing");
            gfx_text(&ui.back, &font_body, dx, dy, b, C_SOFT, 0); dy += 28;
            gfx_text(&ui.back, &font_body, dx, dy, "Storage and network drivers: not implemented yet", 0xf6a9a4, 0);
        } else {
            gfx_text(&ui.back, &font_h2, dx, dy, items[sel].title, C_TEXT, 0);
            gfx_text_wrap(&ui.back, &font_body, dx, dy + 40, d.w - 52, 24, items[sel].detail, C_SOFT);
        }
    }
    gfx_text(&ui.back, &font_small, x, list.y + list.h + 30, "Up/Down: choose    Enter: select    Esc: close panel", C_MUTED, 0);
    ui_status_bar(&now, "Recovery");
    if (ptr_x >= 0) ui_cursor(ptr_x, ptr_y);
    ui_present();
}

static int key(int k) {   /* returns 0x77 to leave recovery */
    if (panel == P_ERASE) {
        size_t n = strlen(typed);
        if (k == KEY_ESC) panel = -1;
        else if (k == KEY_BACKSPACE && n) typed[n - 1] = 0;
        else if (k == KEY_ENTER) {
            if (strcmp(typed, "ERASE")) { set_msg("Factory reset cancelled: the confirmation did not match.", 0); return 0; }
            set_msg(sys_store_erase() < 0 ? "Factory reset failed: the disk could not be written." : "All data on the data disk was erased.", 0);
        } else if (k >= 'a' && k <= 'z' && n < 6) { typed[n] = (char)(k - 32); typed[n + 1] = 0; }
        else if (k >= 'A' && k <= 'Z' && n < 6) { typed[n] = (char)k; typed[n + 1] = 0; }
        return 0;
    }
    if (k == KEY_UP) { sel = (sel + NITEMS - 1) % NITEMS; panel = -1; }
    else if (k == KEY_DOWN || k == KEY_TAB) { sel = (sel + 1) % NITEMS; panel = -1; }
    else if (k == KEY_ESC) panel = -1;
    else if (k == KEY_ENTER && items[sel].enabled) {
        if (sel == 0) { puts_("recovery: starting the normal system\n"); return 0x77; }
        act(sel);
    }
    return 0;
}

static void serial_menu(void) {
    puts_("Nova Recovery (text mode: no usable display)\n");
    for (int i = 0; i < NITEMS; i++) { puts_(items[i].enabled ? "  [ ] " : "  [-] "); puts_(items[i].title); puts_("\n"); }
}

int main(void) {
    sys_time(&now);
    if (!ui_init()) { serial_menu(); puts_("NOVA_RECOVERY_READY text\n"); for (;;) sys_sleep(1000); }
    sys_store_info(&store);
    draw();
    puts_("NOVA_RECOVERY_READY graphical\n");
    for (;;) {
        nova_input_event ev[16];
        int64_t n = sys_input_read(ev, 16);
        bool dirty = false;
        for (int i = 0; i < n; i++) {
            if (ev[i].key == KEY_POINTER) {
                ptr_x = ev[i].x; ptr_y = ev[i].y; dirty = true;
                int h = ev[i].pressed && !ptr_btn ? ui_hit_at(ptr_x, ptr_y) : 0;
                ptr_btn = ev[i].pressed;
                if (h >= 100 && h < 100 + NITEMS) { sel = h - 100; panel = -1; if (key(KEY_ENTER) == 0x77) return 0x77; }
                continue;
            }
            if (!ev[i].pressed) continue;
            dirty = true;
            if (key(ev[i].key) == 0x77) return 0x77;
        }
        if (dirty) draw(); else sys_sleep(30);
    }
}
