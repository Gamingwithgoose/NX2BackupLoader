/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <stdint.h>

/* Bounded speculative ROMFS streaming policy, shared by extracted folders
 * and uncompressed NUP extents. Returns zero for a demand-only request. */
uint64_t nx2_rom_prefetch_bytes(uint64_t requested,uint64_t remaining,
                                unsigned sequential_streak);
