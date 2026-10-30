/* novaui: shared screen setup and NovaOS chrome (status bar, headers, buttons) for the shell and Nova Recovery. */
#pragma once
#include "nova.h"
#include "gfx.h"

#define UI_MAX_W 1920
#define UI_MAX_H 1200
#define C_TEXT 0xffffff
#define C_SOFT 0xd8e4f0
#define C_MUTED 0xa9b8c9
#define C_GLASS 0x10213b
#define A_GLASS 0xd9
#define C_APPVIEW 0x0b1728
#define A_APPVIEW 0xed

typedef struct {
    surface back, wall, blur;      /* back buffer, wallpaper, blurred wallpaper (for glass) */
    nova_fb_info fb;
    int w, h;
} ui_t;
extern ui_t ui;

bool ui_init(void);                         /* false if there is no display (or no CAP_DISPLAY) */
void ui_present(void);
int ui_x(int v);                            /* reference layout (1440x900) -> screen */
int ui_y(int v);
void ui_status_bar(const nova_time *t, const char *right_note);
void ui_glass(rect r, int radius);
void ui_action(rect r, const char *label, int icon, bool focused);   /* .small-action button */
void ui_focus_ring(rect r, int radius);
void ui_appview(const char *title, bool back_focused);              /* app background + header */
void fmt_time12(char *out, const nova_time *t, bool seconds);
void fmt_date(char *out, const nova_time *t);                      /* "Thursday, October 29" */
extern const char *const weekday_names[7], *const month_names[12];
