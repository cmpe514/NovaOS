#include "novaui.h"

ui_t ui;
bool ui_clock24;
static struct { rect r; int action; } hits[96];
static int nhits;
void ui_hit_reset(void) { nhits = 0; }
void ui_hit(rect r, int action) { if (nhits < 96) { hits[nhits].r = r; hits[nhits].action = action; nhits++; } }
int ui_hit_at(int x, int y) {
    for (int i = nhits - 1; i >= 0; i--) {
        rect r = hits[i].r;
        if (x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h) return hits[i].action;
    }
    return 0;
}
void ui_cursor(int x, int y) {
    gfx_round(&ui.back, (rect){ x - 9, y - 9, 18, 18 }, 9, 0x0b1728, 0xc0);
    gfx_round(&ui.back, (rect){ x - 7, y - 7, 14, 14 }, 7, 0xffffff, 0xf0);
}
static uint32_t backbuf[UI_MAX_W * UI_MAX_H], wallbuf[UI_MAX_W * UI_MAX_H], blurbuf[UI_MAX_W * UI_MAX_H];
static uint32_t scratch[NOVA_WALL_W * NOVA_WALL_H * 2];
const char *const weekday_names[7] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
const char *const month_names[12] = { "January", "February", "March", "April", "May", "June", "July", "August",
                                      "September", "October", "November", "December" };

bool ui_init(void) {
    if (sys_fb_map(&ui.fb) < 0 || ui.fb.bpp != 32) return false;
    ui.w = ui.fb.width < UI_MAX_W ? (int)ui.fb.width : UI_MAX_W;
    ui.h = ui.fb.height < UI_MAX_H ? (int)ui.fb.height : UI_MAX_H;
    ui.back = (surface){ backbuf, ui.w, ui.h, ui.w };
    ui.wall = (surface){ wallbuf, ui.w, ui.h, ui.w };
    ui.blur = (surface){ blurbuf, ui.w, ui.h, ui.w };
    gfx_wallpaper(&ui.wall, 0, scratch);
    gfx_wallpaper(&ui.blur, 6, scratch);   /* blur(22px) at 1/4 resolution: three box passes of radius 6 */
    return true;
}

void ui_present(void) {
    for (int y = 0; y < ui.h; y++)
        memcpy((uint8_t *)ui.fb.addr + (uint64_t)y * ui.fb.pitch, &backbuf[y * ui.w], (size_t)ui.w * 4);
}

int ui_x(int v) { return v * ui.w / 1440; }
int ui_y(int v) { return v * ui.h / 900; }

void fmt_time12(char *o, const nova_time *t, bool sec) {
    int h = ui_clock24 ? t->hour : t->hour % 12 ? t->hour % 12 : 12;
    o[0] = 0;
    char *e = fmt_u(o, h, 2); *e++ = ':'; e = fmt_u(e, t->minute, 2);
    if (sec) { *e++ = ':'; e = fmt_u(e, t->second, 2); }
    *e = 0;
    if (!ui_clock24) str_cat(e, t->hour < 12 ? " AM" : " PM");
}
void fmt_date(char *o, const nova_time *t) {
    o[0] = 0;
    str_cat(o, weekday_names[t->weekday % 7]); str_cat(o, ", "); str_cat(o, month_names[(t->month + 11) % 12]); str_cat(o, " ");
    fmt_u(o + strlen(o), t->day, 1);
}

void ui_status_bar(const nova_time *t, const char *note) {
    char s[32];
    fmt_time12(s, t, false);
    int base = ui_y(32);
    gfx_text(&ui.back, &font_status, ui_x(56), base, s, C_TEXT, 0);
    if (note) {
        int w = gfx_text_width(&font_small, note, 0);
        gfx_text(&ui.back, &font_small, ui_x(1330) - w, base, note, C_MUTED, 0);
    }
    rect b = { ui_x(1343), ui_y(11), 40, 32 };
    gfx_round(&ui.back, b, 11, 0xffffff, 0x16);
    gfx_icon(&ui.back, ICON_SLIDERS, b.x + 7, b.y + 3, C_TEXT);
    ui_hit(b, 2);   /* action 2: quick settings */
}

void ui_glass(rect r, int radius) { gfx_glass(&ui.back, &ui.blur, r, radius, C_GLASS, A_GLASS); }

void ui_focus_ring(rect r, int radius) {
    gfx_round_border(&ui.back, (rect){ r.x - 4, r.y - 4, r.w + 8, r.h + 8 }, radius + 4, NOVA_ACCENT, 255);
    gfx_round_border(&ui.back, (rect){ r.x - 3, r.y - 3, r.w + 6, r.h + 6 }, radius + 3, NOVA_ACCENT, 255);
}

void ui_action(rect r, const char *label, int icon, bool focused) {
    gfx_round(&ui.back, r, 11, 0xffffff, focused ? 0x30 : 0x16);
    int x = r.x + 12;
    if (icon >= 0) { gfx_icon(&ui.back, icon, x - 4, r.y + (r.h - NOVA_ICON_PX) / 2, C_TEXT); x += 26; }
    if (icon < 0) x = r.x + (r.w - gfx_text_width(&font_small, label, 0)) / 2;
    gfx_text(&ui.back, &font_small, x, r.y + r.h / 2 + 5, label, C_TEXT, 0);
    if (focused) ui_focus_ring(r, 11);
}

void ui_appview(const char *title, bool back_focused) {
    gfx_fill(&ui.back, (rect){ 0, ui_y(54), ui.w, ui.h - ui_y(54) }, C_APPVIEW, A_APPVIEW);
    int x = ui.w > 1100 + 96 ? (ui.w - 1100) / 2 : 48;
    rect b = { x, ui_y(54) + 36, 44, 44 };
    gfx_round(&ui.back, b, 22, 0xffffff, back_focused ? 0x30 : 0x16);
    gfx_icon(&ui.back, ICON_BACK, b.x + 9, b.y + 9, C_TEXT);
    if (back_focused) ui_focus_ring(b, 22);
    ui_hit(b, 1);   /* action 1: back / home */
    gfx_text(&ui.back, &font_title, x + 64, b.y + 33, title, C_TEXT, 0);
}
