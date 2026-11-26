#pragma once
#include <stdint.h>
#define NX2_MONITOR_MAGIC 0x4e58324du
typedef struct {
    uint32_t magic, phase;
    uint64_t pid, title, tick, frames;
    uint32_t stage, reserved;
    uint64_t end_tick;
} Nx2MonitorSample;
