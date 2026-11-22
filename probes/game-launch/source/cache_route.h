/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <stdint.h>
#include <stddef.h>

/* Map only current-guest user cache attributes to the private SD namespace.
 * The attribute is the native FsSaveDataAttribute byte layout (0x40 bytes).
 * No host-title FS sessions or native save IDs are used here. */
int nx2_guest_cache_path(uint64_t guest_id,uint8_t space,
                         const unsigned char *attr,size_t attr_len,
                         char *out,size_t capacity);
