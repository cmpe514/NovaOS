/* libnova: the NovaOS userspace runtime (syscall wrappers and small helpers). */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "nova/abi.h"

static inline int64_t sc3(uint64_t n, uint64_t a, uint64_t b, uint64_t c) {
#if defined(__aarch64__)
    register uint64_t x8 __asm__("x8") = n, x0 __asm__("x0") = a, x1 __asm__("x1") = b, x2 __asm__("x2") = c;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2) : "memory");
    return (int64_t)x0;
#else
    int64_t r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c) : "memory");
    return r;
#endif
}
#define sys_exit(c)            sc3(SYS_EXIT, (c), 0, 0)
#define sys_write(p, n)        sc3(SYS_WRITE, (uint64_t)(p), (n), 0)
#define sys_yield()            sc3(SYS_YIELD, 0, 0, 0)
#define sys_spawn(nm, caps, a) sc3(SYS_SPAWN, (uint64_t)(nm), (caps), (a))
#define sys_wait(pid)          sc3(SYS_WAIT, (pid), 0, 0)
#define sys_fb_map(info)       sc3(SYS_FB_MAP, (uint64_t)(info), 0, 0)
#define sys_input_read(ev, n)  sc3(SYS_INPUT_READ, (uint64_t)(ev), (n), 0)
#define sys_time(t)            sc3(SYS_TIME, (uint64_t)(t), 0, 0)
#define sys_ticks()            sc3(SYS_TICKS, 0, 0, 0)
#define sys_sleep(ms)          sc3(SYS_SLEEP, (ms), 0, 0)
#define sys_sysinfo(s)         sc3(SYS_SYSINFO, (uint64_t)(s), 0, 0)
#define sys_power(cmd)         sc3(SYS_POWER, (cmd), 0, 0)
#define sys_store_info(i)      sc3(SYS_STORE_INFO, (uint64_t)(i), 0, 0)
#define sys_store_read(b, n)   sc3(SYS_STORE_READ, (uint64_t)(b), (n), 0)
#define sys_store_write(b, n)  sc3(SYS_STORE_WRITE, (uint64_t)(b), (n), 0)
#define sys_store_erase()      sc3(SYS_STORE_ERASE, 0x45524153, 0, 0)

void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
void puts_(const char *s);
char *fmt_u(char *buf, uint64_t v, int width);   /* writes decimal, returns end */
char *str_cat(char *d, const char *s);           /* appends, returns end */
