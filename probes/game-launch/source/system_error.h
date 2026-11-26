/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <stddef.h>
#include <stdint.h>
/* Menu-only readback. Never called by guest, GPU, or storage workers. */
int nx2_system_error_mark(uint64_t host, uint64_t guest);
void nx2_system_error_read(char *outcome, size_t capacity);
