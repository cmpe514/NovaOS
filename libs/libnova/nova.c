#include "nova.h"
void *memset(void *d, int c, size_t n) { uint8_t *p = d; while (n--) *p++ = (uint8_t)c; return d; }
void *memcpy(void *d, const void *s, size_t n) { uint8_t *a = d; const uint8_t *b = s; while (n--) *a++ = *b++; return d; }
size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
int strcmp(const char *a, const char *b) { while (*a && *a == *b) a++, b++; return (uint8_t)*a - (uint8_t)*b; }
void puts_(const char *s) { sys_write(s, strlen(s)); }
char *fmt_u(char *b, uint64_t v, int w) {
    char t[24]; int i = 0;
    do { t[i++] = '0' + v % 10; v /= 10; } while (v);
    while (i < w) t[i++] = '0';
    while (i) *b++ = t[--i];
    *b = 0;
    return b;
}
char *str_cat(char *d, const char *s) { while (*d) d++; while (*s) *d++ = *s++; *d = 0; return d; }
