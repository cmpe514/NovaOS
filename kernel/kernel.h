#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "nova/abi.h"
#include "io.h"

#define KCODE_SEL 0x08
#define KDATA_SEL 0x10
#define UDATA_SEL 0x1b
#define UCODE_SEL 0x23
#define PAGE 4096ull
#define KERNEL_RAM_LIMIT 0x40000000ull   /* the kernel manages RAM below 1 GiB (identity mapped) */
#define NOVA_VERSION "0.2.0-m1"

typedef struct frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8, rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vec, err, rip, cs, rflags, rsp, ss;
} frame_t;

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

/* arch */
void cpu_init(void);
void cpu_set_kernel_stack(uint64_t top);

/* drivers */
void serial_init(void);
void serial_putc(char c);
void pic_init(void);
void pic_eoi(int irq);
void pic_unmask(int irq);
void pit_init(uint32_t hz);
extern volatile uint64_t ticks;
#define TICK_HZ 250
bool kbd_init(void);
void kbd_irq(void);
int kbd_read(nova_input_event *ev, int max);
void rtc_read(nova_time *t);
int pci_scan(void);
void power_off(void);
void power_reboot(void);

/* framebuffer (from the bootloader) */
typedef struct { uint64_t phys; uint32_t width, height, pitch, bpp; bool ok; } fb_t;
extern fb_t fb;
void fb_panic_screen(const char *msg);

/* memory */
void pmm_init(uint64_t mbi);
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
uint64_t syscall_dispatch(frame_t *f, bool *resched);
uint64_t elf_load(uint64_t pml4, const uint8_t *img, uint64_t size);   /* entry or 0 */

/* boot */
extern char boot_cmdline[128];
extern uint32_t boot_mode;
extern int pci_count;
extern bool kbd_ok;
