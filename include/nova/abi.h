/* NovaOS kernel <-> userspace ABI (syscall numbers, structures, error codes). Shared by both sides. */
#pragma once
#include <stdint.h>

/* Calling convention: x86-64 `int $0x80`, number in rax, arguments in rdi, rsi, rdx, result in rax.
 * AArch64 `svc #0`, number in x8, arguments in x0, x1, x2, result in x0.
 * Negative results are errors. Every pointer argument is checked against the caller's page tables. */
enum {
    SYS_EXIT = 0, SYS_WRITE, SYS_YIELD, SYS_SPAWN, SYS_WAIT, SYS_FB_MAP, SYS_INPUT_READ,
    SYS_TIME, SYS_TICKS, SYS_SLEEP, SYS_SYSINFO, SYS_POWER,
    SYS_STORE_INFO, SYS_STORE_READ, SYS_STORE_WRITE, SYS_STORE_ERASE, SYS_COUNT
};

#define E_PERM    1    /* missing capability */
#define E_NOENT   2    /* no such program / process */
#define E_FAULT  14    /* bad user pointer */
#define E_BUSY   16
#define E_INVAL  22
#define E_NOSYS  38    /* unknown syscall number */
#define E_NOMEM  12

/* Capabilities: granted at spawn time, only a subset of the parent's. */
#define CAP_DISPLAY  (1u << 0)   /* map the framebuffer */
#define CAP_INPUT    (1u << 1)   /* read keyboard events */
#define CAP_SPAWN    (1u << 2)   /* start programs */
#define CAP_POWER    (1u << 3)   /* reboot / power off */
#define CAP_STORAGE  (1u << 4)   /* read/write the NovaOS data store */
#define CAP_ALL      0x1fu

/* A process killed by a CPU exception exits with EXIT_FAULT | kind (x86 vector numbers; AArch64 maps to them). */
#define EXIT_FAULT 0x100
#define FAULT_PAGE 14    /* access to unmapped or forbidden memory, NX / permission violation */
#define FAULT_PRIV 13    /* privileged or forbidden instruction */

/* The 1 GiB private user region. Program images are linked at USER_BASE. */
#if defined(__aarch64__)
#define USER_BASE      0x8000000000ull   /* 512 GiB (TTBR0, L0 entry 1); RAM sits at 1-2 GiB on QEMU virt */
#define PROBE_KERNEL_ADDR 0x40200000ull  /* kernel image, EL1-only */
#else
#define USER_BASE      0x40000000ull     /* 1 GiB */
#define PROBE_KERNEL_ADDR 0x100000ull    /* kernel image, supervisor-only */
#endif
#define USER_FB        (USER_BASE + 0x20000000ull)   /* framebuffer mapping (CAP_DISPLAY) */
#define USER_STACK_TOP (USER_BASE + 0x3fff0000ull)
#define USER_TOP       (USER_BASE + 0x40000000ull)

typedef struct { uint64_t addr; uint32_t width, height, pitch, bpp; } nova_fb_info;

enum { KEY_NONE = 0, KEY_UP = 0x100, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_HOME, KEY_END, KEY_DELETE,
       KEY_F1, KEY_F2, KEY_SUPER,
       KEY_POINTER = 0x200,      /* pointer moved or button changed: x, y = position in pixels, pressed = left button */
     };
#define KEY_ENTER '\n'
#define KEY_ESC 27
#define KEY_BACKSPACE '\b'
#define KEY_TAB '\t'
typedef struct { uint16_t key; uint8_t pressed, shift; uint16_t x, y; } nova_input_event;

typedef struct { uint16_t year; uint8_t month, day, hour, minute, second, weekday; } nova_time;

#define POWER_OFF 1
#define POWER_REBOOT 2

#define BOOT_NORMAL 0
#define BOOT_RECOVERY 1
#define BOOT_SAFE 2

/* Data store: a crash-safe A/B snapshot on the NovaOS data disk (see docs/STORAGE.md). */
#define STORE_MAX 65024
typedef struct {
    uint32_t present;          /* a usable NovaOS data disk was found */
    uint32_t has_data;         /* at least one valid snapshot */
    uint64_t disk_bytes, generation;
    uint32_t slot;             /* 0 = A, 1 = B: slot holding the current snapshot */
    uint32_t valid[2];         /* per-slot validity (checksums correct) */
    uint32_t length;
    char status[48];           /* human-readable state or the reason the disk is not used */
} nova_store_info;

typedef struct {
    uint64_t mem_total, mem_free;     /* bytes */
    uint64_t ticks, hz;
    uint32_t tasks, pci_devices, boot_mode, kbd_ok;
    uint32_t fb_width, fb_height;
    char version[16];
    char arch[16];
    char cpu[48];
} nova_sysinfo;
