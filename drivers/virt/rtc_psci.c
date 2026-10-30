/* PL031 real-time clock (0x09010000) and PSCI power control (QEMU virt implements PSCI via HVC at EL1). */
#include "kernel.h"

void rtc_read(nova_time *t) {
    uint64_t s = mmio_r32(0x09010000ull), days = s / 86400, rem = s % 86400;
    t->hour = rem / 3600; t->minute = rem / 60 % 60; t->second = rem % 60;
    t->weekday = (days + 4) % 7;                         /* 1970-01-01 was a Thursday */
    /* civil_from_days (H. Hinnant) */
    int64_t z = (int64_t)days + 719468, era = z / 146097, doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365, doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153, d = doy - (153 * mp + 2) / 5 + 1, m = mp < 10 ? mp + 3 : mp - 9;
    t->year = (uint16_t)(yoe + era * 400 + (m <= 2)); t->month = (uint8_t)m; t->day = (uint8_t)d;
}

static void psci(uint64_t fn) {
    register uint64_t x0 __asm__("x0") = fn;
    __asm__ volatile("hvc #0" : "+r"(x0) :: "memory");
}
__attribute__((noreturn)) void power_off(void) { kprintf("NOVA_POWER_OFF\n"); psci(0x84000008); for (;;) cpu_wait(); }
__attribute__((noreturn)) void power_reboot(void) { kprintf("NOVA_REBOOT\n"); psci(0x84000009); for (;;) cpu_wait(); }
