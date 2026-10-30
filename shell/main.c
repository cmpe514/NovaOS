/* NovaOS shell: native home screen and apps, rendered with the theme extracted from ui/reference/ui.html.
 * Keyboard: arrows/Tab move focus, Enter opens, Esc or the Super key goes home. */
#include "novaui.h"
#include "nova/shellstate.h"

enum { V_HOME, V_APP };
static int view = V_HOME, app = -1, focus = 0;
static nova_time now;
static nova_sysinfo si;

/* ---------- app state (saved to the data store after every change) ---------- */
static char note[2048];
static struct { char text[64]; bool done; } tasks[SHELL_MAX_TASKS];
static int ntasks, task_sel = -1;
static char task_input[64];
static char calc_disp[32] = "0";
static int64_t calc_acc; static char calc_op; static bool calc_fresh = true, calc_err;
static int settings_sel;
static int drawer_focus;
static nova_store_info store;
static int ptr_x = -1, ptr_y, ptr_btn;
enum { ACT_HOME = 1, ACT_QUICK = 2, ACT_OPEN = 100, ACT_TASK_TOGGLE = 200, ACT_TASK_DEL = 300, ACT_TASK_INPUT = 399,
       ACT_CALC = 400, ACT_SET = 500 };

static shell_state st;
static void save(void) {
    memset(&st, 0, sizeof st);
    st.version = SHELL_STATE_VERSION;
    memcpy(st.note, note, sizeof st.note);
    st.ntasks = (uint32_t)ntasks;
    for (int i = 0; i < ntasks; i++) { memcpy(st.tasks[i].text, tasks[i].text, 64); st.tasks[i].done = tasks[i].done; }
    st.settings.clock24 = ui_clock24;
    sys_store_write(&st, sizeof st);
    sys_store_info(&store);
}
static void load(void) {
    sys_store_info(&store);
    if (sys_store_read(&st, sizeof st) != (int64_t)sizeof st || st.version != SHELL_STATE_VERSION) return;
    memcpy(note, st.note, sizeof note); note[sizeof note - 1] = 0;
    ntasks = st.ntasks <= SHELL_MAX_TASKS ? (int)st.ntasks : 0;
    for (int i = 0; i < ntasks; i++) { memcpy(tasks[i].text, st.tasks[i].text, 64); tasks[i].text[63] = 0; tasks[i].done = st.tasks[i].done; }
    ui_clock24 = st.settings.clock24 != 0;
}
static const char *save_note(void) { return store.present ? "Saved on the data disk" : "Not saved: no NovaOS data disk attached"; }

#define SCALE 1000000ll
static int64_t calc_parse(const char *s) {
    int64_t ip = 0, fp = 0, div = 1; bool neg = *s == '-', dot = false;
    for (s += neg; *s; s++) {
        if (*s == '.') { dot = true; continue; }
        if (!dot) ip = ip * 10 + (*s - '0'); else if (div < SCALE) { fp = fp * 10 + (*s - '0'); div *= 10; }
    }
    int64_t v = ip * SCALE + fp * (SCALE / div);
    return neg ? -v : v;
}
static void calc_format(int64_t v) {
    char *o = calc_disp;
    if (v < 0) { *o++ = '-'; v = -v; }
    o = fmt_u(o, (uint64_t)(v / SCALE), 1);
    int64_t f = v % SCALE;
    if (f) { *o++ = '.'; char t[8]; fmt_u(t, (uint64_t)f, 6); int n = 6; while (t[n - 1] == '0') n--; t[n] = 0; str_cat(o, t); }
}
static int64_t iabs(int64_t v) { return v < 0 ? -v : v; }
static void calc_apply(void) {
    int64_t b = calc_parse(calc_disp), a = calc_acc, r = b;
    const int64_t LIM = 999999999999ll * SCALE;          /* 12 integer digits */
    bool neg = (a < 0) != (b < 0);
    int64_t ma = iabs(a), mb = iabs(b);
    switch (calc_op) {
    case '+': r = a + b; break;
    case '-': r = a - b; break;
    case '*': {                                           /* fixed-point multiply without 128-bit division */
        int64_t ai = ma / SCALE, af = ma % SCALE;
        if (ai && mb > LIM / ai) { calc_err = true; break; }
        r = ai * mb + af * (mb / SCALE) + af * (mb % SCALE) / SCALE;
        if (neg) r = -r;
        break; }
    case '/': {
        if (!mb || mb > LIM) { calc_err = true; break; }
        int64_t q = ma / mb, rem = ma % mb;
        if (q > LIM / SCALE) { calc_err = true; break; }
        r = q * SCALE;
        for (int64_t place = SCALE / 10; place; place /= 10) { rem *= 10; r += rem / mb * place; rem %= mb; }
        if (neg) r = -r;
        break; }
    }
    if (r > LIM || r < -LIM) calc_err = true;
    if (calc_err) { memcpy(calc_disp, "Error", 6); return; }
    calc_acc = r; calc_format(r);
}
static void calc_key(int k) {
    if (calc_err && k != 'c' && k != 'C') return;
    if ((k >= '0' && k <= '9') || k == '.') {
        size_t n = strlen(calc_disp);
        if (calc_fresh) { calc_disp[0] = 0; n = 0; calc_fresh = false; }
        if (k == '.') for (size_t i = 0; i < n; i++) if (calc_disp[i] == '.') return;
        if (n >= 14) return;
        if (n == 1 && calc_disp[0] == '0' && k != '.') n = 0;
        if (n == 0 && k == '.') calc_disp[n++] = '0';
        calc_disp[n] = (char)k; calc_disp[n + 1] = 0;
    } else if (k == '+' || k == '-' || k == '*' || k == '/' || k == 'x') {
        if (!calc_fresh) { if (calc_op) calc_apply(); else calc_acc = calc_parse(calc_disp); }
        calc_op = k == 'x' ? '*' : (char)k; calc_fresh = true;
    } else if (k == '=' || k == KEY_ENTER) {
        if (calc_op) { calc_apply(); calc_op = 0; }
        calc_fresh = true;
    } else if (k == KEY_BACKSPACE) {
        size_t n = strlen(calc_disp);
        if (!calc_fresh && n > 1) calc_disp[n - 1] = 0; else { memcpy(calc_disp, "0", 2); calc_fresh = true; }
    } else if (k == 'c' || k == 'C' || k == KEY_DELETE) {
        memcpy(calc_disp, "0", 2); calc_acc = 0; calc_op = 0; calc_fresh = true; calc_err = false;
    }
}

/* ---------- drawing ---------- */
static rect tile_rect(int i, int x0, int y0, int dx, int dy) { return (rect){ x0 + (i % 4) * dx, y0 + (i / 4) * dy, 60, 60 }; }

static void draw_tile(int i, rect r, bool focused) {
    const nova_app *a = &nova_apps[i];
    for (int k = 3; k >= 1; k--) gfx_round(&ui.back, (rect){ r.x - k, r.y + 10 - k + 2, r.w + 2 * k, r.h + 2 * k - 4 }, 20 + k, 0, 0x14);
    gfx_round(&ui.back, r, 20, a->bg, 255);
    gfx_fill(&ui.back, (rect){ r.x + 14, r.y + 1, r.w - 28, 1 }, 0xffffff, 0x60);   /* inset highlight */
    gfx_icon(&ui.back, i, r.x + (60 - NOVA_ICON_PX) / 2, r.y + (60 - NOVA_ICON_PX) / 2, a->fg);
    int w = gfx_text_width(&font_label, a->label, 0);
    gfx_text(&ui.back, &font_label, r.x + 30 - w / 2, r.y + 78, a->label, C_TEXT, 0);
    if (focused) ui_focus_ring(r, 20);
    ui_hit((rect){ r.x - 20, r.y, 100, 86 }, ACT_OPEN + i);
}

static void draw_home(void) {
    char s[48];
    int x = ui_x(72);
    gfx_text(&ui.back, &font_small, x, ui_y(237), "A NEW PERSPECTIVE", NOVA_ACCENT, 64);
    fmt_date(s, &now);
    gfx_text(&ui.back, &font_date, x, ui_y(279), s, C_TEXT, 8);
    fmt_time12(s, &now, false);
    gfx_text(&ui.back, &font_hero, x - 4, ui_y(422), s, C_TEXT, -190);   /* letter-spacing: -.09em */
    gfx_text(&ui.back, &font_title, x, ui_y(505), "NovaOS", C_TEXT, 0);
    gfx_text(&ui.back, &font_body, x, ui_y(543), "Make room for what matters.", C_TEXT, 0);
    rect card = { x, ui_y(586), ui_x(447), ui_y(113) };
    ui_glass(card, 26);
    gfx_text(&ui.back, &font_small, card.x + 21, card.y + ui_y(32), "YOUR SPACE", NOVA_ACCENT, 40);
    gfx_text(&ui.back, &font_date, card.x + 21, card.y + ui_y(61), "A little more clarity.", C_TEXT, 0);
    gfx_text(&ui.back, &font_body, card.x + 21, card.y + ui_y(87), "Your favorite tools, always close at hand.", C_SOFT, 0);

    gfx_text(&ui.back, &font_h2, ui_x(928), ui_y(305), "Your apps", C_TEXT, 0);
    rect qs = { ui_x(1240), ui_y(280), ui_x(127), ui_y(33) };
    ui_action(qs, "Quick settings", ICON_SLIDERS, focus == 12);
    ui_hit(qs, ACT_QUICK);
    for (int i = 0; i < NOVA_APP_COUNT; i++)
        draw_tile(i, tile_rect(i, ui_x(949), ui_y(339), ui_x(113), ui_y(110)), focus == i);

    rect dock = { ui.w / 2 - 92, ui.h - ui_y(57), 185, 41 };
    ui_glass(dock, 20);
    gfx_icon(&ui.back, ICON_DRAWER, dock.x + 16, dock.y + 7, C_TEXT);
    gfx_round(&ui.back, (rect){ dock.x + 64, dock.y + 18, 58, 5 }, 2, 0xe8faff, 255);
    gfx_icon(&ui.back, ICON_SLIDERS, dock.x + 143, dock.y + 7, C_TEXT);
    ui_hit((rect){ dock.x, dock.y, 60, dock.h }, ACT_OPEN + 11);
    ui_hit((rect){ dock.x + 125, dock.y, 60, dock.h }, ACT_QUICK);
}

static int content_x(void) { return ui.w > 1100 + 96 ? (ui.w - 1100) / 2 : 48; }
static int content_w(void) { return ui.w - 2 * content_x(); }
static int content_y(void) { return ui_y(54) + 110; }

static void chip(int x, int y, const char *text) {
    int w = gfx_text_width(&font_small, text, 0) + 24;
    gfx_round(&ui.back, (rect){ x, y, w, 26 }, 13, store.present ? NOVA_ACCENT : 0xf6a9a4, 0x30);
    gfx_text(&ui.back, &font_small, x + 12, y + 18, text, store.present ? 0xd8fbf6 : 0xffd9d4, 0);
}

static void draw_unavailable(const char *what, const char *why) {
    rect r = { content_x(), content_y(), content_w() < 760 ? content_w() : 760, 170 };
    ui_glass(r, 26);
    gfx_text(&ui.back, &font_small, r.x + 28, r.y + 40, "NOT AVAILABLE YET", NOVA_ACCENT, 40);
    gfx_text(&ui.back, &font_h2, r.x + 28, r.y + 76, what, C_TEXT, 0);
    gfx_text_wrap(&ui.back, &font_body, r.x + 28, r.y + 110, r.w - 56, 22, why, C_SOFT);
}

static void draw_notes(void) {
    chip(content_x(), content_y() - 8, save_note());
    rect r = { content_x(), content_y() + 30, content_w() < 760 ? content_w() : 760, ui.h - content_y() - 90 };
    ui_glass(r, 26);
    char buf[2050]; size_t n = strlen(note);
    memcpy(buf, note, n); buf[n] = '|'; buf[n + 1] = 0;
    if (!n) gfx_text(&ui.back, &font_body, r.x + 28, r.y + 44, "Start typing a note...", C_MUTED, 0);
    gfx_text_wrap(&ui.back, &font_body, r.x + 28, r.y + 44, r.w - 56, 24, n ? buf : "|", C_TEXT);
}

static void draw_tasks(void) {
    chip(content_x(), content_y() - 8, save_note());
    int w = content_w() < 760 ? content_w() : 760, x = content_x(), y = content_y() + 30;
    rect in = { x, y, w, 52 };
    ui_glass(in, 18);
    char buf[70]; memcpy(buf, task_input, sizeof task_input); str_cat(buf, task_sel < 0 ? "|" : "");
    gfx_text(&ui.back, &font_body, x + 22, y + 32, task_input[0] || task_sel < 0 ? buf : "", C_TEXT, 0);
    if (!task_input[0]) gfx_text(&ui.back, &font_body, x + 30, y + 32, "Add a task and press Enter", C_MUTED, 0);
    if (task_sel < 0) ui_focus_ring(in, 18);
    ui_hit(in, ACT_TASK_INPUT);
    y += 72;
    if (!ntasks) gfx_text(&ui.back, &font_body, x + 4, y + 20, "No tasks yet.", C_MUTED, 0);
    for (int i = 0; i < ntasks; i++, y += 56) {
        rect r = { x, y, w, 48 };
        ui_glass(r, 16);
        rect box = { x + 16, y + 13, 22, 22 };
        if (tasks[i].done) gfx_round(&ui.back, box, 7, NOVA_ACCENT, 255); else gfx_round_border(&ui.back, box, 7, 0xffffff, 0x90);
        if (tasks[i].done) gfx_text(&ui.back, &font_label, box.x + 6, box.y + 16, "v", 0x10222c, 0);
        gfx_text(&ui.back, &font_body, x + 52, y + 30, tasks[i].text, tasks[i].done ? C_MUTED : C_TEXT, 0);
        if (tasks[i].done) gfx_fill(&ui.back, (rect){ x + 52, y + 25, gfx_text_width(&font_body, tasks[i].text, 0), 1 }, C_MUTED, 255);
        gfx_icon(&ui.back, ICON_DELETE, x + w - 44, y + 10, i == task_sel ? 0xf6a9a4 : C_MUTED);
        if (i == task_sel) ui_focus_ring(r, 16);
        ui_hit(r, ACT_TASK_TOGGLE + i);
        ui_hit((rect){ x + w - 52, y, 52, 48 }, ACT_TASK_DEL + i);
    }
    gfx_text(&ui.back, &font_small, x, ui.h - 40, "Up/Down: select   Space or click: complete   Delete or click the icon: remove   Esc: home", C_MUTED, 0);
}

static const char *const calc_keys[20] = { "C", "<", "%", "/", "7", "8", "9", "x", "4", "5", "6", "-", "1", "2", "3", "+", "0", "00", ".", "=" };
static void draw_calculator(void) {
    int w = 420, x = content_x(), y = content_y();
    rect d = { x, y, w, 110 };
    ui_glass(d, 26);
    int tw = gfx_text_width(&font_big, calc_disp, 0);
    gfx_text(&ui.back, &font_big, x + w - 28 - tw, y + 80, calc_disp, C_TEXT, 0);
    if (calc_op) { char o[2] = { calc_op == '*' ? 'x' : calc_op, 0 }; gfx_text(&ui.back, &font_h2, x + 24, y + 40, o, NOVA_ACCENT, 0); }
    for (int i = 0; i < 20; i++) {
        rect k = { x + (i % 4) * 106, y + 130 + (i / 4) * 66, 96, 56 };
        bool op = i % 4 == 3 || i < 3;
        gfx_round(&ui.back, k, 18, i == 19 ? NOVA_ACCENT : 0xffffff, i == 19 ? 255 : op ? 0x24 : 0x14);
        int lw = gfx_text_width(&font_h2, calc_keys[i], 0);
        gfx_text(&ui.back, &font_h2, k.x + (96 - lw) / 2, k.y + 36, calc_keys[i], i == 19 ? 0x10222c : C_TEXT, 0);
        ui_hit(k, ACT_CALC + i);
    }
    gfx_text(&ui.back, &font_small, x, y + 480, "Type digits and + - * / . Enter = result, Backspace, C = clear.", C_MUTED, 0);
}

static void draw_clock(void) {
    char s[32];
    fmt_time12(s, &now, true);
    gfx_text(&ui.back, &font_hero, content_x(), content_y() + 150, s, C_TEXT, -190);
    fmt_date(s, &now);
    char d[48]; d[0] = 0; str_cat(d, s); str_cat(d, ", "); fmt_u(d + strlen(d), now.year, 4);
    gfx_text(&ui.back, &font_date, content_x() + 4, content_y() + 196, d, C_SOFT, 0);
    gfx_text(&ui.back, &font_small, content_x() + 4, content_y() + 232, "Time from the hardware clock (CMOS RTC). Time zones and alarms are not implemented yet.", C_MUTED, 0);
}

static void draw_calendar(void) {
    static const int mdays[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    int y0 = content_y(), x0 = content_x(), cw = 64;
    char t[32]; t[0] = 0; str_cat(t, month_names[(now.month + 11) % 12]); str_cat(t, " "); fmt_u(t + strlen(t), now.year, 4);
    rect r = { x0, y0, cw * 7 + 48, 400 };
    ui_glass(r, 26);
    gfx_text(&ui.back, &font_h2, x0 + 24, y0 + 46, t, C_TEXT, 0);
    static const char *const wd[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    for (int i = 0; i < 7; i++) gfx_text(&ui.back, &font_small, x0 + 24 + i * cw + 18, y0 + 86, wd[i], C_MUTED, 0);
    int first = (now.weekday + 35 - (now.day - 1)) % 7;
    int days = mdays[(now.month + 11) % 12] + (now.month == 2 && now.year % 4 == 0 && (now.year % 100 || now.year % 400 == 0));
    for (int d = 1; d <= days; d++) {
        int c = (first + d - 1) % 7, row = (first + d - 1) / 7;
        rect cell = { x0 + 24 + c * cw + 8, y0 + 104 + row * 46, 40, 36 };
        if (d == now.day) gfx_round(&ui.back, cell, 12, NOVA_ACCENT, 255);
        char n[4]; fmt_u(n, d, 1);
        int w = gfx_text_width(&font_body, n, 0);
        gfx_text(&ui.back, &font_body, cell.x + (40 - w) / 2, cell.y + 24, n, d == now.day ? 0x10222c : C_TEXT, 0);
    }
    gfx_text(&ui.back, &font_small, x0, y0 + 430, "Events need persistent storage (milestone M2).", C_MUTED, 0);
}

static void row(int x, int *y, int w, const char *k, const char *v, bool bad) {
    gfx_text(&ui.back, &font_body, x, *y, k, C_SOFT, 0);
    gfx_text(&ui.back, &font_body, x + w / 3, *y, v, bad ? 0xf6a9a4 : C_TEXT, 0);
    *y += 30;
}
static void draw_settings(void) {
    sys_sysinfo(&si);
    int x = content_x(), y = content_y(), w = content_w() < 760 ? content_w() : 760;
    rect r = { x, y, w, 420 };
    ui_glass(r, 26);
    char b[96]; int yy = y + 44, ix = x + 28;
    gfx_text(&ui.back, &font_small, ix, yy - 6, "ABOUT THIS DEVICE", NOVA_ACCENT, 40); yy += 26;
    b[0] = 0; str_cat(b, "NovaOS "); str_cat(b, si.version); str_cat(b, " (own kernel, "); str_cat(b, si.arch); str_cat(b, ")"); row(ix, &yy, w, "System", b, false);
    row(ix, &yy, w, "Processor", si.cpu[0] ? si.cpu : "unknown", false);
    b[0] = 0; fmt_u(b, si.mem_free >> 20, 1); str_cat(b, " MiB free of "); fmt_u(b + strlen(b), si.mem_total >> 20, 1); str_cat(b, " MiB");
    row(ix, &yy, w, "Memory", b, false);
    uint64_t up = si.ticks / si.hz; b[0] = 0; fmt_u(b, up / 60, 1); str_cat(b, " min "); fmt_u(b + strlen(b), up % 60, 1); str_cat(b, " s");
    row(ix, &yy, w, "Uptime", b, false);
    b[0] = 0; fmt_u(b, si.fb_width, 1); str_cat(b, " x "); fmt_u(b + strlen(b), si.fb_height, 1); str_cat(b, " (linear framebuffer)");
    row(ix, &yy, w, "Display", b, false);
    row(ix, &yy, w, "Keyboard", si.kbd_ok ? "Keyboard working" : "No keyboard found", !si.kbd_ok);
    b[0] = 0; fmt_u(b, si.pci_devices, 1); str_cat(b, " devices found"); row(ix, &yy, w, "Devices", b, false);
    b[0] = 0;
    if (store.present) { fmt_u(b, store.disk_bytes >> 10, 1); str_cat(b, " KiB data disk, "); str_cat(b, store.has_data ? "saved, generation " : "nothing saved yet"); if (store.has_data) fmt_u(b + strlen(b), store.generation, 1); }
    else str_cat(b, store.status[0] ? store.status : "No data disk");
    row(ix, &yy, w, "Storage", b, !store.present);
    row(ix, &yy, w, "Network", "No driver yet (virtio-net + TCP/IP: milestone M4)", true);
    row(ix, &yy, w, "Sound, battery", "Not supported yet", true);
    const char *acts[3] = { "Restart", "Shut down", ui_clock24 ? "24-hour clock: On" : "24-hour clock: Off" };
    for (int i = 0; i < 3; i++) {
        rect r = { x + i * 130, y + 440, i == 2 ? 170 : 118, 40 };
        ui_action(r, acts[i], -1, settings_sel == i);
        ui_hit(r, ACT_SET + i);
    }
    gfx_text(&ui.back, &font_small, x, y + 506, "Left/Right: choose   Enter or click: confirm", C_MUTED, 0);
}

static void draw_drawer(void) {
    int n = NOVA_APP_COUNT - 1;
    for (int i = 0; i < n; i++)
        draw_tile(i, (rect){ content_x() + (i % 6) * 120 + 20, content_y() + (i / 6) * 120 + 10, 60, 60 }, drawer_focus == i);
}

static void draw(void) {
    ui_hit_reset();
    memcpy(ui.back.px, ui.wall.px, (size_t)ui.w * ui.h * 4);
    if (view == V_HOME) draw_home();
    else {
        const char *id = nova_apps[app].id;
        ui_appview(nova_apps[app].label, false);
        if (!strcmp(id, "notes")) draw_notes();
        else if (!strcmp(id, "tasks")) draw_tasks();
        else if (!strcmp(id, "calculator")) draw_calculator();
        else if (!strcmp(id, "clock")) draw_clock();
        else if (!strcmp(id, "calendar")) draw_calendar();
        else if (!strcmp(id, "settings")) draw_settings();
        else if (!strcmp(id, "drawer")) draw_drawer();
        else if (!strcmp(id, "weather")) draw_unavailable("Weather needs a network", "Forecasts need the virtio-net driver and the TCP/IP, DHCP and DNS stack (milestone M4). NovaOS does not show sample forecasts as if they were real.");
        else if (!strcmp(id, "music")) draw_unavailable("Music needs sound and storage", "Playback needs an audio driver and persistent storage. Neither exists in the NovaOS kernel yet.");
        else if (!strcmp(id, "photos")) draw_unavailable("Photos needs storage", "Your photos will live on persistent storage (virtio-blk driver and filesystem, milestone M2).");
        else if (!strcmp(id, "files")) draw_unavailable("Files needs storage", "The file manager needs the virtio-blk driver and the NovaOS filesystem (milestone M2).");
        else if (!strcmp(id, "browser")) draw_unavailable("No web browser yet", "A real browser needs networking (M4), TLS and a ported web engine. This is a planning item; see docs/ROADMAP.md.");
    }
    ui_status_bar(&now, "No network");
    if (ptr_x >= 0) ui_cursor(ptr_x, ptr_y);
    ui_present();
}

/* ---------- input ---------- */
static void open_app(int i) { view = V_APP; app = i; task_sel = -1; settings_sel = 0; drawer_focus = 0; }

static void key(int k) {
    if (k == KEY_SUPER || k == KEY_F1 || (k == KEY_ESC)) { view = V_HOME; return; }
    if (view == V_HOME) {
        if (k == KEY_RIGHT || k == KEY_TAB) focus = (focus + 1) % 13;
        else if (k == KEY_LEFT) focus = (focus + 12) % 13;
        else if (k == KEY_DOWN) focus = focus == 12 ? 0 : focus + 4 < 12 ? focus + 4 : focus;
        else if (k == KEY_UP) focus = focus == 12 ? 12 : focus >= 4 ? focus - 4 : 12;
        else if (k == KEY_ENTER || k == ' ') open_app(focus == 12 ? 10 : focus);
        return;
    }
    const char *id = nova_apps[app].id;
    if (!strcmp(id, "notes")) {
        size_t n = strlen(note);
        if (k == KEY_BACKSPACE) { if (n) note[n - 1] = 0; }
        else if ((k == KEY_ENTER || (k >= 32 && k < 127)) && n < sizeof note - 2) { note[n] = (char)k; note[n + 1] = 0; }
        save();
    } else if (!strcmp(id, "tasks")) {
        size_t n = strlen(task_input);
        if (k == KEY_UP) task_sel = task_sel < 0 ? ntasks - 1 : task_sel - 1;
        else if (k == KEY_DOWN) task_sel = task_sel + 1 >= ntasks ? -1 : task_sel + 1;
        else if (task_sel >= 0 && (k == ' ' || k == KEY_ENTER)) tasks[task_sel].done = !tasks[task_sel].done;
        else if (task_sel >= 0 && (k == KEY_DELETE || k == KEY_BACKSPACE)) {
            for (int i = task_sel; i < ntasks - 1; i++) tasks[i] = tasks[i + 1];
            ntasks--; if (task_sel >= ntasks) task_sel = ntasks - 1;
        } else if (task_sel < 0 && k == KEY_ENTER && n && ntasks < SHELL_MAX_TASKS) {
            memcpy(tasks[ntasks].text, task_input, sizeof task_input); tasks[ntasks++].done = false; task_input[0] = 0;
        } else if (task_sel < 0 && k == KEY_BACKSPACE && n) task_input[n - 1] = 0;
        else if (task_sel < 0 && k >= 32 && k < 127 && n < sizeof task_input - 1) { task_input[n] = (char)k; task_input[n + 1] = 0; }
        if (k != KEY_UP && k != KEY_DOWN && (task_sel >= 0 || k == KEY_ENTER)) save();
        int done = 0; for (int i = 0; i < ntasks; i++) done += tasks[i].done;
        char m[48] = "NOVA_TASKS total="; fmt_u(m + strlen(m), ntasks, 1); str_cat(m, " done="); fmt_u(m + strlen(m), done, 1);
        str_cat(m, "\n"); puts_(m);
    } else if (!strcmp(id, "calculator")) {
        if (k == '%') { calc_format(calc_parse(calc_disp) / 100); }
        else calc_key(k);
        char m[48] = "NOVA_CALC "; str_cat(m, calc_disp); str_cat(m, "\n"); puts_(m);   /* test evidence */
    } else if (!strcmp(id, "settings")) {
        if (k == KEY_RIGHT || k == KEY_TAB) settings_sel = (settings_sel + 1) % 3;
        else if (k == KEY_LEFT) settings_sel = (settings_sel + 2) % 3;
        else if (k == KEY_ENTER && settings_sel == 2) { ui_clock24 = !ui_clock24; save(); puts_(ui_clock24 ? "NOVA_SETTING clock24=1\n" : "NOVA_SETTING clock24=0\n"); }
        else if (k == KEY_ENTER) sys_power(settings_sel ? POWER_OFF : POWER_REBOOT);
    } else if (!strcmp(id, "drawer")) {
        int n = NOVA_APP_COUNT - 1;
        if (k == KEY_RIGHT || k == KEY_TAB) drawer_focus = (drawer_focus + 1) % n;
        else if (k == KEY_LEFT) drawer_focus = (drawer_focus + n - 1) % n;
        else if (k == KEY_DOWN && drawer_focus + 6 < n) drawer_focus += 6;
        else if (k == KEY_UP && drawer_focus >= 6) drawer_focus -= 6;
        else if (k == KEY_ENTER) open_app(drawer_focus);
    }
}

static void click(int x, int y) {
    int a = ui_hit_at(x, y);
    char m[64] = "NOVA_CLICK action="; fmt_u(m + strlen(m), (uint64_t)a, 1); str_cat(m, " x="); fmt_u(m + strlen(m), (uint64_t)x, 1);
    str_cat(m, " y="); fmt_u(m + strlen(m), (uint64_t)y, 1); str_cat(m, "\n"); puts_(m);
    if (!a) return;
    if (a == ACT_HOME) { view = V_HOME; return; }
    if (a == ACT_QUICK) { open_app(10); return; }
    if (a >= ACT_OPEN && a < ACT_OPEN + NOVA_APP_COUNT) { open_app(a - ACT_OPEN); return; }
    if (a == ACT_TASK_INPUT) { task_sel = -1; return; }
    if (a >= ACT_TASK_TOGGLE && a < ACT_TASK_TOGGLE + SHELL_MAX_TASKS) { task_sel = a - ACT_TASK_TOGGLE; key(' '); return; }
    if (a >= ACT_TASK_DEL && a < ACT_TASK_DEL + SHELL_MAX_TASKS) { task_sel = a - ACT_TASK_DEL; key(KEY_DELETE); return; }
    if (a >= ACT_CALC && a < ACT_CALC + 20) {
        static const int keys[20] = { 'c', KEY_BACKSPACE, '%', '/', '7', '8', '9', '*', '4', '5', '6', '-', '1', '2', '3', '+', '0', 0, '.', '=' };
        int i = a - ACT_CALC;
        if (i == 17) { key('0'); key('0'); } else key(keys[i]);
        return;
    }
    if (a >= ACT_SET && a < ACT_SET + 3) { settings_sel = a - ACT_SET; key(KEY_ENTER); }
}

int main(void) {
    if (!ui_init()) { puts_("shell: no display available\n"); return 3; }
    load();
    {
        char m[96] = "NOVA_STATE_LOADED store="; str_cat(m, store.present ? "yes" : "no"); str_cat(m, " tasks=");
        fmt_u(m + strlen(m), (uint64_t)ntasks, 1); str_cat(m, " note_len="); fmt_u(m + strlen(m), strlen(note), 1);
        str_cat(m, ui_clock24 ? " clock24=1\n" : " clock24=0\n"); puts_(m);
    }
    sys_time(&now);
    draw();
    puts_("NOVA_SHELL_READY\n");
    uint8_t last_sec = now.second, last_min = now.minute;
    for (;;) {
        nova_input_event ev[16];
        int64_t n = sys_input_read(ev, 16);
        bool dirty = false;
        for (int i = 0; i < n; i++) {
            if (ev[i].key == KEY_POINTER) {
                ptr_x = ev[i].x; ptr_y = ev[i].y; dirty = true;
                if (ev[i].pressed && !ptr_btn) click(ptr_x, ptr_y);
                ptr_btn = ev[i].pressed;
            } else if (ev[i].pressed) { key(ev[i].key); dirty = true; }
        }
        sys_time(&now);
        bool in_clock = view == V_APP && !strcmp(nova_apps[app].id, "clock");
        if (now.minute != last_min || (in_clock && now.second != last_sec)) dirty = true;
        last_sec = now.second; last_min = now.minute;
        if (dirty) {
            draw();
            char m[40] = "NOVA_SHELL_VIEW "; str_cat(m, view == V_HOME ? "home" : nova_apps[app].id); str_cat(m, "\n");
            puts_(m);
        } else sys_sleep(30);
    }
}
