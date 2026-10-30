#include "gfx.h"

const nova_font font_status = NOVA_FONT_STATUS, font_label = NOVA_FONT_LABEL, font_small = NOVA_FONT_SMALL,
    font_body = NOVA_FONT_BODY, font_date = NOVA_FONT_DATE, font_h2 = NOVA_FONT_H2, font_title = NOVA_FONT_TITLE,
    font_big = NOVA_FONT_BIG, font_hero = NOVA_FONT_HERO;

uint32_t gfx_mix(uint32_t d, uint32_t s, int a) {
    if (a >= 255) return s;
    if (a <= 0) return d;
    uint32_t r = ((d >> 16 & 255) * (255 - a) + (s >> 16 & 255) * a + 127) / 255;
    uint32_t g = ((d >> 8 & 255) * (255 - a) + (s >> 8 & 255) * a + 127) / 255;
    uint32_t b = ((d & 255) * (255 - a) + (s & 255) * a + 127) / 255;
    return r << 16 | g << 8 | b;
}
static inline uint32_t *at(surface *s, int x, int y) { return &s->px[y * s->stride + x]; }
static inline void plot(surface *s, int x, int y, uint32_t c, int a) {
    if ((unsigned)x < (unsigned)s->w && (unsigned)y < (unsigned)s->h) *at(s, x, y) = gfx_mix(*at(s, x, y), c, a);
}

void gfx_fill(surface *s, rect r, uint32_t c, int a) {
    for (int y = r.y < 0 ? 0 : r.y; y < r.y + r.h && y < s->h; y++)
        for (int x = r.x < 0 ? 0 : r.x; x < r.x + r.w && x < s->w; x++) *at(s, x, y) = gfx_mix(*at(s, x, y), c, a);
}

static int isqrt(uint32_t v) { uint32_t r = 0, b = 1u << 30; while (b > v) b >>= 2;
    while (b) { if (v >= r + b) { v -= r + b; r = (r >> 1) + b; } else r >>= 1; b >>= 2; } return (int)r; }

/* Coverage (0..256) of pixel (x,y) inside a rounded rectangle, computed on the pixel centre with a 1px ramp. */
static int coverage(rect r, int rad, int x, int y) {
    if (x < r.x || y < r.y || x >= r.x + r.w || y >= r.y + r.h) return 0;
    int cx = x < r.x + rad ? r.x + rad : x >= r.x + r.w - rad ? r.x + r.w - rad : -1;
    int cy = y < r.y + rad ? r.y + rad : y >= r.y + r.h - rad ? r.y + r.h - rad : -1;
    if (cx < 0 || cy < 0) return 256;
    int dx = (x * 16 + 8) - cx * 16, dy = (y * 16 + 8) - cy * 16;
    int d = isqrt((uint32_t)(dx * dx + dy * dy));          /* 1/16 px */
    int c = (rad * 16 + 8 - d) * 16;
    return c < 0 ? 0 : c > 256 ? 256 : c;
}

void gfx_round(surface *s, rect r, int rad, uint32_t c, int a) {
    for (int y = r.y; y < r.y + r.h; y++)
        for (int x = r.x; x < r.x + r.w; x++) {
            int cov = coverage(r, rad, x, y);
            if (cov) plot(s, x, y, c, a * cov >> 8);
        }
}

void gfx_round_border(surface *s, rect r, int rad, uint32_t c, int a) {
    rect in = { r.x + 1, r.y + 1, r.w - 2, r.h - 2 };
    for (int y = r.y; y < r.y + r.h; y++)
        for (int x = r.x; x < r.x + r.w; x++) {
            if (y > r.y + rad && y < r.y + r.h - rad - 1 && x > r.x && x < r.x + r.w - 1) { x = r.x + r.w - 2; continue; }
            int cov = coverage(r, rad, x, y) - coverage(in, rad - 1, x, y);
            if (cov > 0) plot(s, x, y, c, a * cov >> 8);
        }
}

void gfx_glass(surface *s, const surface *bg, rect r, int rad, uint32_t tint, int ta) {
    for (int y = r.y; y < r.y + r.h; y++)
        for (int x = r.x; x < r.x + r.w; x++) {
            if ((unsigned)x >= (unsigned)s->w || (unsigned)y >= (unsigned)s->h) continue;
            int cov = coverage(r, rad, x, y);
            if (!cov) continue;
            uint32_t g = gfx_mix(bg->px[y * bg->stride + x], tint, ta);
            *at(s, x, y) = gfx_mix(*at(s, x, y), g, cov >= 256 ? 255 : cov);
        }
    gfx_round_border(s, r, rad, 0xffffff, 0x26);
}

static int glyph_index(const nova_font *f, char ch) {
    for (int i = 0; f->chars[i]; i++) if (f->chars[i] == ch) return i;
    return -1;
}

int gfx_text_width(const nova_font *f, const char *str, int tr) {
    int pen = 0;
    for (; *str; str++) { int i = glyph_index(f, *str); if (i >= 0) pen += f->g[i].adv16 + tr; }
    return (pen - tr) / 16;
}

int gfx_text(surface *s, const nova_font *f, int x, int base, const char *str, uint32_t c, int tr) {
    int pen = x * 16;
    for (; *str; str++) {
        int i = glyph_index(f, *str);
        if (i < 0) continue;
        const nova_glyph *g = &f->g[i];
        int gx = pen / 16 - f->pad, gy = base - f->asc;
        for (int yy = 0; yy < f->h; yy++)
            for (int xx = 0; xx < g->w; xx++) {
                int a = f->alpha[yy * f->stride + g->x + xx];
                if (a) plot(s, gx + xx, gy + yy, c, a);
            }
        pen += g->adv16 + tr;
    }
    return (pen - x * 16) / 16;
}

int gfx_text_wrap(surface *s, const nova_font *f, int x, int base, int max_w, int lh, const char *str, uint32_t c) {
    char line[256]; int n = 0, last_space = -1, lines = 0;
    for (const char *p = str; ; p++) {
        bool end = !*p || *p == '\n';
        if (!end) { if (n < 255) line[n++] = *p; if (*p == ' ') last_space = n - 1; line[n] = 0; }
        if (!end && gfx_text_width(f, line, 0) <= max_w) continue;
        int cut = end ? n : last_space > 0 ? last_space : n - 1;
        char keep = line[cut]; line[cut] = 0;
        gfx_text(s, f, x, base + lines * lh, line, c, 0);
        lines++;
        if (end && !*p) break;
        if (end) { n = 0; last_space = -1; continue; }
        line[cut] = keep;
        int rest = n - cut - (keep == ' ' ? 1 : 0), from = cut + (keep == ' ' ? 1 : 0);
        for (int i = 0; i < rest; i++) line[i] = line[from + i];
        n = rest; line[n] = 0; last_space = -1;
    }
    return lines;
}

void gfx_icon(surface *s, int icon, int x, int y, uint32_t c) {
    const uint8_t *m = nova_blob_icons + icon * NOVA_ICON_PX * NOVA_ICON_PX;
    for (int yy = 0; yy < NOVA_ICON_PX; yy++)
        for (int xx = 0; xx < NOVA_ICON_PX; xx++) if (m[yy * NOVA_ICON_PX + xx]) plot(s, x + xx, y + yy, c, m[yy * NOVA_ICON_PX + xx]);
}

static void box_blur(uint32_t *a, uint32_t *tmp, int w, int h, int r) {
    for (int pass = 0; pass < 3; pass++) {
        for (int dir = 0; dir < 2; dir++) {
            int len = dir ? h : w, lines = dir ? w : h;
            for (int l = 0; l < lines; l++) {
                uint32_t sr = 0, sg = 0, sb = 0;
                #define IDX(i) (dir ? ((i) < 0 ? 0 : (i) >= len ? len - 1 : (i)) * w + l : l * w + ((i) < 0 ? 0 : (i) >= len ? len - 1 : (i)))
                for (int i = -r; i <= r; i++) { uint32_t p = a[IDX(i)]; sr += p >> 16 & 255; sg += p >> 8 & 255; sb += p & 255; }
                for (int i = 0; i < len; i++) {
                    int n = 2 * r + 1;
                    tmp[IDX(i)] = (sr / n) << 16 | (sg / n) << 8 | sb / n;
                    uint32_t o = a[IDX(i - r)], q = a[IDX(i + r + 1)];
                    sr += (q >> 16 & 255) - (o >> 16 & 255); sg += (q >> 8 & 255) - (o >> 8 & 255); sb += (q & 255) - (o & 255);
                }
                #undef IDX
            }
            for (int i = 0; i < w * h; i++) a[i] = tmp[i];
        }
    }
}

void gfx_wallpaper(surface *d, int blur, uint32_t *scratch) {
    const int W = NOVA_WALL_W, H = NOVA_WALL_H;
    uint32_t *src = scratch, *tmp = scratch + W * H;
    for (int i = 0; i < W * H; i++)
        src[i] = (uint32_t)nova_blob_wallpaper[i * 3] << 16 | nova_blob_wallpaper[i * 3 + 1] << 8 | nova_blob_wallpaper[i * 3 + 2];
    if (blur) box_blur(src, tmp, W, H, blur);
    /* cover-fit, then bilinear sample in 16.16 fixed point */
    int64_t sx = ((int64_t)W << 16) / d->w, sy = ((int64_t)H << 16) / d->h, sc = sx < sy ? sx : sy;
    int64_t ox = (((int64_t)W << 16) - sc * d->w) / 2, oy = (((int64_t)H << 16) - sc * d->h) / 2;
    for (int y = 0; y < d->h; y++) {
        int64_t fy = oy + sc * y; int y0 = (int)(fy >> 16), wy = (int)(fy >> 8 & 255); int y1 = y0 + 1 < H ? y0 + 1 : y0;
        for (int x = 0; x < d->w; x++) {
            int64_t fx = ox + sc * x; int x0 = (int)(fx >> 16), wx = (int)(fx >> 8 & 255); int x1 = x0 + 1 < W ? x0 + 1 : x0;
            uint32_t top = gfx_mix(src[y0 * W + x0], src[y0 * W + x1], wx), bot = gfx_mix(src[y1 * W + x0], src[y1 * W + x1], wx);
            d->px[y * d->stride + x] = gfx_mix(top, bot, wy);
        }
    }
}

void gfx_copy(surface *d, const surface *s, rect r) {
    for (int y = r.y; y < r.y + r.h && y < d->h; y++)
        for (int x = r.x; x < r.x + r.w && x < d->w; x++) d->px[y * d->stride + x] = s->px[y * s->stride + x];
}
