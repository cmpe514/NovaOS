/* Freestanding helpers, kernel log (serial) and panic. */
#include <stdarg.h>
#include "kernel.h"

void *memset(void *d, int c, size_t n) { uint8_t *p = d; while (n--) *p++ = (uint8_t)c; return d; }
void *memcpy(void *d, const void *s, size_t n) { uint8_t *a = d; const uint8_t *b = s; while (n--) *a++ = *b++; return d; }
int memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *x = a, *y = b;
    for (; n; n--, x++, y++) if (*x != *y) return *x - *y;
    return 0;
}
size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
int strcmp(const char *a, const char *b) { while (*a && *a == *b) a++, b++; return (uint8_t)*a - (uint8_t)*b; }
bool str_contains(const char *h, const char *n) {
    size_t ln = strlen(n);
    for (; *h; h++) if (!memcmp(h, n, ln)) return true;
    return false;
}

static void put_u(uint64_t v, int base, int width) {
    char b[24]; int i = 0;
    do { b[i++] = "0123456789abcdef"[v % base]; v /= base; } while (v);
    while (i < width) b[i++] = '0';
    while (i) serial_putc(b[--i]);
}

static void vkprintf(const char *f, va_list ap) {
    for (; *f; f++) {
        if (*f != '%') { serial_putc(*f); continue; }
        f++;
        int w = 0;
        while (*f >= '0' && *f <= '9') w = w * 10 + (*f++ - '0');
        bool l = false;
        if (*f == 'l') { l = true; f++; }
        switch (*f) {
        case 's': { const char *s = va_arg(ap, const char *); while (s && *s) serial_putc(*s++); break; }
        case 'c': serial_putc((char)va_arg(ap, int)); break;
        case 'd': { int64_t v = l ? va_arg(ap, int64_t) : va_arg(ap, int); if (v < 0) { serial_putc('-'); v = -v; } put_u((uint64_t)v, 10, w); break; }
        case 'u': put_u(l ? va_arg(ap, uint64_t) : va_arg(ap, unsigned), 10, w); break;
        case 'x': put_u(l ? va_arg(ap, uint64_t) : va_arg(ap, unsigned), 16, w); break;
        case 'p': serial_putc('0'); serial_putc('x'); put_u(va_arg(ap, uint64_t), 16, 16); break;
        default: serial_putc(*f);
        }
    }
}

void kprintf(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vkprintf(fmt, ap); va_end(ap); }

void panic(const char *fmt, ...) {
    irq_disable();
    kprintf("\nNOVA_PANIC: ");
    va_list ap; va_start(ap, fmt); vkprintf(fmt, ap); va_end(ap);
    kprintf("\n");
    fb_panic_screen("The kernel stopped because of an internal error. Details were written to the serial log.");
    for (;;) cpu_wait();
}
