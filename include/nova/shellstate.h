/* The shell's saved state (the payload of the data store). Shared with Nova Recovery for "Reset settings". */
#pragma once
#include <stdint.h>
#define SHELL_STATE_VERSION 1
#define SHELL_MAX_TASKS 24
typedef struct {
    uint32_t version;
    char note[2048];
    uint32_t ntasks;
    struct { char text[64]; uint8_t done; } tasks[SHELL_MAX_TASKS];
    struct { uint8_t clock24; uint8_t reserved[63]; } settings;
} shell_state;
