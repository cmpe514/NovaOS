/* System call dispatch. Every argument is validated: numbers against the table, pointers against the
 * caller's page tables (user_range_ok), privileged operations against the caller's capabilities. */
#include "kernel.h"

#define ERR(e) ((uint64_t)(int64_t)-(e))
#define CHECK(p, n, w) do { if (!user_range_ok(current->pml4, (uint64_t)(p), (n), (w))) return ERR(E_FAULT); } while (0)
#define NEED(c) do { if ((current->caps & (c)) != (c)) return ERR(E_PERM); } while (0)

static uint64_t sys_spawn(uint64_t uname, uint64_t caps, uint64_t arg) {
    NEED(CAP_SPAWN);
    char name[16];
    for (int i = 0; ; i++) {
        if (i == 16) return ERR(E_INVAL);
        CHECK(uname + i, 1, false);
        name[i] = ((const char *)uname)[i];
        if (!name[i]) break;
    }
    if (caps & ~current->caps) return ERR(E_PERM);   /* never grant more than the parent holds */
    int pid = task_spawn(name, (uint32_t)caps, arg, current->pid);
    return pid < 0 ? ERR(-pid) : (uint64_t)pid;
}

static uint64_t sys_fb_map(uint64_t uinfo) {
    NEED(CAP_DISPLAY);
    CHECK(uinfo, sizeof(nova_fb_info), true);
    if (!fb.ok) return ERR(E_NOENT);
    if (!current->fb_mapped) {
        uint64_t len = (uint64_t)fb.pitch * fb.height;
        if (len > USER_STACK_TOP - 64 * PAGE - USER_FB) return ERR(E_NOMEM);
        for (uint64_t off = 0; off < len; off += PAGE)
            if (!as_map(current->pml4, USER_FB + off, (fb.phys & ~(PAGE - 1)) + off, true, false, true)) return ERR(E_NOMEM);
        current->fb_mapped = true;
    }
    *(nova_fb_info *)uinfo = (nova_fb_info){ USER_FB + (fb.phys & (PAGE - 1)), fb.width, fb.height, fb.pitch, fb.bpp };
    return 0;
}

static uint64_t sys_sysinfo(uint64_t u) {
    CHECK(u, sizeof(nova_sysinfo), true);
    nova_sysinfo *s = (nova_sysinfo *)u;
    memset(s, 0, sizeof *s);
    s->mem_total = pmm_total() * PAGE; s->mem_free = pmm_free_count() * PAGE;
    s->ticks = ticks; s->hz = TICK_HZ; s->tasks = task_count(); s->pci_devices = device_count;
    s->boot_mode = boot_mode; s->kbd_ok = kbd_ok; s->fb_width = fb.width; s->fb_height = fb.height;
    memcpy(s->version, NOVA_VERSION, sizeof NOVA_VERSION);
    memcpy(s->arch, arch_name, strlen(arch_name) + 1);
    arch_cpu_name(s->cpu, sizeof s->cpu);
    return 0;
}

uint64_t syscall_dispatch(frame_t *f, bool *resched, bool *restart) {
    uint64_t a = SYSCALL_ARG0(f), b = SYSCALL_ARG1(f), c = SYSCALL_ARG2(f);
    switch (SYSCALL_NR(f)) {
    case SYS_WRITE:
        if (b > 4096) return ERR(E_INVAL);
        CHECK(a, b, false);
        for (uint64_t i = 0; i < b; i++) serial_putc(((const char *)a)[i]);
        return b;
    case SYS_YIELD: *resched = true; return 0;
    case SYS_SPAWN: return sys_spawn(a, b, c);
    case SYS_WAIT: {
        bool block = false;
        int64_t r = task_wait((int)a, &block);
        if (block) { *restart = *resched = true; return 0; }   /* re-execute the syscall when woken */
        return (uint64_t)r;
    }
    case SYS_FB_MAP: return sys_fb_map(a);
    case SYS_INPUT_READ:
        NEED(CAP_INPUT);
        if (b == 0 || b > 64) return ERR(E_INVAL);
        CHECK(a, b * sizeof(nova_input_event), true);
        return (uint64_t)input_read((nova_input_event *)a, (int)b);
    case SYS_TIME: CHECK(a, sizeof(nova_time), true); rtc_read((nova_time *)a); return 0;
    case SYS_TICKS: return ticks * 1000 / TICK_HZ;
    case SYS_SLEEP:
        if (a > 60000) return ERR(E_INVAL);
        current->wake = ticks + (a * TICK_HZ + 999) / 1000;
        current->state = T_SLEEP; *resched = true; return 0;
    case SYS_SYSINFO: return sys_sysinfo(a);
    case SYS_POWER:
        NEED(CAP_POWER);
        if (a == POWER_OFF) power_off();
        if (a == POWER_REBOOT) power_reboot();
        return ERR(E_INVAL);
    case SYS_STORE_INFO:
        NEED(CAP_STORAGE);
        CHECK(a, sizeof(nova_store_info), true);
        store_get_info((nova_store_info *)a);
        return 0;
    case SYS_STORE_READ:
        NEED(CAP_STORAGE);
        if (b > STORE_MAX) b = STORE_MAX;
        CHECK(a, b, true);
        return (uint64_t)store_read((void *)a, b);
    case SYS_STORE_WRITE:
        NEED(CAP_STORAGE);
        if (b > STORE_MAX) return ERR(E_INVAL);
        CHECK(a, b, false);
        return (uint64_t)store_write((const void *)a, b);
    case SYS_STORE_ERASE:
        NEED(CAP_STORAGE);
        if (a != 0x45524153) return ERR(E_INVAL);   /* "ERAS": the caller must state the intent explicitly */
        return (uint64_t)store_erase();
    default:
        return ERR(E_NOSYS);
    }
}
