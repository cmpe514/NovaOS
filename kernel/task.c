/* Processes and the round-robin preemptive scheduler. Task 0 is the kernel idle loop (ring 0). */
#include "kernel.h"

static task_t tasks[MAX_TASKS];
task_t *current;
static int next_pid = 1;

/* Program images linked into the kernel (see kernel/programs.S). */
extern const uint8_t prog_init[], prog_init_end[], prog_shell[], prog_shell_end[],
                     prog_recovery[], prog_recovery_end[], prog_probe[], prog_probe_end[];
static const struct { const char *name; const uint8_t *start, *end; } programs[] = {
    { "init", prog_init, prog_init_end }, { "shell", prog_shell, prog_shell_end },
    { "recovery", prog_recovery, prog_recovery_end }, { "probe", prog_probe, prog_probe_end },
};

void task_init(void) {
    tasks[0] = (task_t){ .pid = 0, .state = T_RUN, .name = "idle", .pml4 = kernel_pml4 };
    current = &tasks[0];
}

static uint8_t kstacks[MAX_TASKS][4 * PAGE] __attribute__((aligned(16)));

int task_spawn(const char *name, uint32_t caps, uint64_t arg, int parent) {
    int prog = -1;
    for (unsigned i = 0; i < sizeof programs / sizeof *programs; i++) if (!strcmp(programs[i].name, name)) prog = i;
    if (prog < 0) return -E_NOENT;
    task_t *t = 0;
    for (int i = 1; i < MAX_TASKS; i++) if (tasks[i].state == T_FREE) { t = &tasks[i]; break; }
    if (!t) return -E_BUSY;
    uint64_t pml4 = as_create();
    if (!pml4) return -E_NOMEM;
    uint64_t entry = elf_load(pml4, programs[prog].start, programs[prog].end - programs[prog].start);
    bool ok = entry != 0;
    for (uint64_t va = USER_STACK_TOP - 32 * PAGE; ok && va < USER_STACK_TOP; va += PAGE) {
        uint64_t pa = pmm_alloc();
        ok = pa && as_map(pml4, va, pa, true, false, false);
        if (!ok && pa) pmm_free(pa);
    }
    if (!ok) { as_destroy(pml4); kprintf("spawn %s: load failed\n", name); return entry ? -E_NOMEM : -E_INVAL; }
    uint64_t kstack = (uint64_t)kstacks[t - tasks] + sizeof kstacks[0];
    *t = (task_t){ .pid = next_pid++, .parent = parent, .state = T_RUN, .caps = caps, .pml4 = pml4, .kstack = kstack };
    for (int i = 0; i < 15 && name[i]; i++) t->name[i] = name[i];
    frame_t *f = (frame_t *)(kstack - sizeof(frame_t));
    memset(f, 0, sizeof *f);
    f->rip = entry; f->cs = UCODE_SEL; f->rflags = 0x202; f->rsp = USER_STACK_TOP - 8; f->ss = UDATA_SEL;
    f->rdi = arg;
    t->frame = f;
    kprintf("spawn: pid %d '%s' caps=%x\n", t->pid, t->name, caps);
    return t->pid;
}

static void switch_to(task_t *t) {
    current = t;
    if (t->pid) cpu_set_kernel_stack(t->kstack);
    __asm__ volatile("mov %0, %%cr3" :: "r"(t->pml4) : "memory");
}

frame_t *schedule(frame_t *f) {
    current->frame = f;
    for (int i = 1; i < MAX_TASKS; i++)
        if (tasks[i].state == T_SLEEP && ticks >= tasks[i].wake) tasks[i].state = T_RUN;
    int start = current - tasks;
    for (int n = 1; n <= MAX_TASKS; n++) {
        task_t *t = &tasks[(start + n) % MAX_TASKS];
        if (t->pid && t->state == T_RUN) { switch_to(t); return t->frame; }
    }
    switch_to(&tasks[0]);
    return tasks[0].frame;
}

int task_count(void) { int n = 0; for (int i = 1; i < MAX_TASKS; i++) n += tasks[i].state != T_FREE; return n; }

/* Called from syscall or fault context of `current`. The address space and kernel stack are freed by the
 * parent's wait() (we are still running on this stack). */
frame_t *task_exit(frame_t *f, uint64_t code) {
    current->state = T_ZOMBIE;
    current->exit_code = code;
    kprintf("exit: pid %d '%s' code=%lx\n", current->pid, current->name, code);
    for (int i = 1; i < MAX_TASKS; i++) {
        task_t *p = &tasks[i];
        if (p->pid == current->parent && p->state == T_WAIT && (p->wait_pid == current->pid)) p->state = T_RUN;
        if (p->state != T_FREE && p->parent == current->pid) p->parent = 0;   /* orphans are reaped by nobody */
    }
    return schedule(f);
}

/* wait(pid): returns the exit code or blocks. *again is set when the caller must retry after waking up. */
int64_t task_wait(int pid, bool *block) {
    for (int i = 1; i < MAX_TASKS; i++) {
        task_t *c = &tasks[i];
        if (c->pid != pid || c->state == T_FREE || c->parent != current->pid) continue;
        if (c->state != T_ZOMBIE) { current->state = T_WAIT; current->wait_pid = pid; *block = true; return 0; }
        int64_t code = (int64_t)c->exit_code;
        as_destroy(c->pml4);
        c->state = T_FREE; c->pid = 0;
        return code;
    }
    return -E_NOENT;
}
