/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <stdint.h>
/* Read-only NUP v1 discovery. Does not launch or extract games. */
int nx2_nup_probe(const char *path, uint64_t *title_id, uint64_t *files);
