#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "nova/abi.h"
#include "arch.h"      /* per-architecture: frame_t, register accessors, KERNEL_RAM_* */

#define PAGE 4096ull
#define NOVA_VERSION "0.4.0"

/* lib */
void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
bool str_contains(const char *hay, const char *needle);
void kprintf(const char *fmt, ...);
__attribute__((noreturn)) void panic(const char *fmt, ...);
#define KASSERT(c) do { if (!(c)) panic("assertion failed: %s (%s:%d)", #c, __FILE__, __LINE__); } while (0)

/* Interface every architecture/platform port provides (arch/<arch>/, drivers/<platform>/). */
void serial_putc(char c);
void rtc_read(nova_time *t);
__attribute__((noreturn)) void power_off(void);
__attribute__((noreturn)) void power_reboot(void);
void arch_user_frame(frame_t *f, uint64_t entry, uint64_t sp, uint64_t arg);
void arch_switch_to(uint64_t pml4, uint64_t kstack_top);
void arch_cpu_name(char *out, size_t n);
extern uint32_t elf_machine;
extern const char arch_name[];

/* Portable kernel entry points called by the architecture code. */
void kernel_start(void);                                    /* after arch + memory + drivers are up */
frame_t *kernel_tick(frame_t *f);                           /* timer interrupt */
frame_t *kernel_syscall(frame_t *f);
frame_t *kernel_user_fault(frame_t *f, unsigned kind, const char *what, uint64_t pc, uint64_t addr);
extern volatile uint64_t ticks;
#define TICK_HZ 250

/* Data store (kernel/store.c) on the virtio-blk disk (kernel/virtio_blk.c). */
void store_init(void);
void store_get_info(nova_store_info *out);
int64_t store_read(void *dst, uint64_t max);
int64_t store_write(const void *src, uint64_t len);
int64_t store_erase(void);

/* Pointer: drivers report relative motion or absolute position (0..32767) and the left button. */
void input_pointer_rel(int dx, int dy, int buttons);
void input_pointer_abs(int x, int y, int buttons);   /* buttons < 0: unchanged */

/* Input: drivers report Linux input key codes (PS/2 set 1 is translated to them). */
void input_key(uint16_t linux_code, bool pressed);
int input_read(nova_input_event *ev, int max);

/* framebuffer (from the bootloader) */
typedef struct { uint64_t phys; uint32_t width, height, pitch, bpp; bool ok; } fb_t;
extern fb_t fb;
void fb_panic_screen(const char *msg);

/* memory */
void pmm_add_region(uint64_t base, uint64_t len);    /* usable RAM; call before pmm_reserve */
void pmm_reserve(uint64_t base, uint64_t len);
uint64_t pmm_alloc(void);           /* zeroed 4 KiB page (physical == kernel virtual), 0 if out of memory */
void pmm_free(uint64_t pa);
uint64_t pmm_total(void), pmm_free_count(void);
extern uint64_t kernel_pml4;
uint64_t as_create(void);
void as_destroy(uint64_t pml4);
bool as_map(uint64_t pml4, uint64_t va, uint64_t pa, bool writable, bool exec, bool device);
bool user_range_ok(uint64_t pml4, uint64_t va, uint64_t len, bool write);

/* tasks */
typedef enum { T_FREE, T_RUN, T_SLEEP, T_WAIT, T_ZOMBIE } tstate_t;
typedef struct task {
    int pid, parent;
    tstate_t state;
    char name[16];
    uint32_t caps;
    uint64_t pml4, kstack, wake, exit_code;
    int wait_pid;
    frame_t *frame;
    bool fb_mapped;
} task_t;
#define MAX_TASKS 16
extern task_t *current;
void task_init(void);
int task_spawn(const char *name, uint32_t caps, uint64_t arg, int parent);
frame_t *schedule(frame_t *f);
frame_t *task_exit(frame_t *f, uint64_t code);
int64_t task_wait(int pid, bool *block);
int task_count(void);
uint64_t syscall_dispatch(frame_t *f, bool *resched, bool *restart);
uint64_t elf_load(uint64_t pml4, const uint8_t *img, uint64_t size);   /* entry or 0 */

/* boot */
extern char boot_cmdline[128];
extern uint32_t boot_mode;
extern int device_count;
extern bool kbd_ok;
