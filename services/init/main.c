/* init: the first user process. Runs the kernel protection self-tests, then starts the shell (or Nova Recovery)
 * and restarts it if it crashes. Holds all capabilities and hands each child only what it needs. */
#include "nova.h"

static int fails;
static void check(const char *name, bool ok) {
    puts_("NOVA_SELFTEST "); puts_(name); puts_(ok ? " PASS\n" : " FAIL\n");
    if (!ok) fails++;
}
static int64_t run_probe(int mode, uint32_t caps) {
    int64_t pid = sys_spawn("probe", caps, mode);
    return pid < 0 ? pid : sys_wait(pid);
}

int main(uint64_t boot_mode) {
    puts_("NOVA_USERSPACE_READY init\n");
    /* syscall validation */
    check("invalid_syscall_enosys", sc3(999, 0, 0, 0) == -E_NOSYS);
    check("write_kernel_pointer_efault", sys_write((const char *)0x100000, 16) == -E_FAULT);
    check("write_unmapped_pointer_efault", sys_write((const char *)0x50000000, 16) == -E_FAULT);
    check("write_oversized_einval", sys_write((const char *)0x7fffffff, 0xffffffffffffff00ull) == -E_INVAL);
    check("sysinfo_into_readonly_efault", sys_sysinfo((nova_sysinfo *)(uintptr_t)&check) == -E_FAULT);
    check("spawn_unknown_enoent", sys_spawn("nope", 0, 0) == -E_NOENT);
    /* memory protection and privilege separation (each probe runs in its own address space) */
    check("user_read_kernel_memory_killed", run_probe(1, 0) == (EXIT_FAULT | 14));
    check("user_write_own_code_killed", run_probe(2, 0) == (EXIT_FAULT | 14));
    check("user_privileged_instruction_killed", run_probe(3, 0) == (EXIT_FAULT | 13));
    check("user_port_io_killed", run_probe(4, 0) == (EXIT_FAULT | 13));
    check("capability_fb_denied", run_probe(5, 0) == 0);
    check("capability_escalation_denied", run_probe(6, CAP_SPAWN) == 0);
    check("user_execute_stack_killed", run_probe(7, 0) == (EXIT_FAULT | 14));
    check("kernel_survives_and_reaps", run_probe(0, 0) == 42);
    puts_(fails ? "NOVA_SELFTEST_DONE FAIL\n" : "NOVA_SELFTEST_DONE PASS\n");

    const char *ui = boot_mode == BOOT_RECOVERY ? "recovery" : "shell";
    for (int attempt = 0; ; attempt++) {
        int64_t pid = sys_spawn(ui, CAP_DISPLAY | CAP_INPUT | CAP_POWER | (ui[0] == 'r' ? CAP_SPAWN : 0), boot_mode);
        if (pid < 0) { puts_("init: cannot start the user interface\n"); break; }
        int64_t code = sys_wait(pid);
        puts_("init: user interface exited\n");
        if (code == 0x77) { ui = "shell"; continue; }       /* recovery: "boot normal system" */
        if (attempt >= 2 && ui[0] == 's') { puts_("init: shell keeps failing; starting Nova Recovery\n"); ui = "recovery"; attempt = 0; }
    }
    for (;;) sys_sleep(1000);
}
