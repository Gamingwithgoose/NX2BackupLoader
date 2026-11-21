/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <stddef.h>
void *nx2_cache_load(const char *root, size_t *bytes);
void nx2_cache_save(const char *root, const void *data, size_t bytes);
