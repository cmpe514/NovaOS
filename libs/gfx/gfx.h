/* NovaOS software renderer: draws the theme (theme/tokens.json) into 32-bit XRGB surfaces.
 * Integer-only so it can run in the kernel and in userspace without FPU state. */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "assets.h"

typedef struct { uint32_t *px; int w, h, stride; } surface;
typedef struct { int x, y, w, h; } rect;

extern const nova_font font_status, font_label, font_small, font_body, font_date, font_h2, font_title, font_big, font_hero;

uint32_t gfx_mix(uint32_t dst, uint32_t src, int a);   /* a: 0..255 */
void gfx_fill(surface *s, rect r, uint32_t c, int a);
void gfx_round(surface *s, rect r, int radius, uint32_t c, int a);
void gfx_round_border(surface *s, rect r, int radius, uint32_t c, int a);
/* Glass panel: the blurred wallpaper (bg_blur, same size as s) tinted with the glass fill and a 1px border. */
void gfx_glass(surface *s, const surface *bg_blur, rect r, int radius, uint32_t tint, int tint_a);
int gfx_text(surface *s, const nova_font *f, int x, int baseline, const char *str, uint32_t c, int tracking16);
int gfx_text_width(const nova_font *f, const char *str, int tracking16);
int gfx_text_wrap(surface *s, const nova_font *f, int x, int baseline, int max_w, int line_h, const char *str, uint32_t c);
void gfx_icon(surface *s, int icon, int x, int y, uint32_t c);
/* Scale the generated wallpaper to the surface (bilinear). If blur != 0 the source is box-blurred first. */
void gfx_wallpaper(surface *dst, int blur_radius, uint32_t *scratch /* NOVA_WALL_W*NOVA_WALL_H*2 words */);
void gfx_copy(surface *dst, const surface *src, rect r);
