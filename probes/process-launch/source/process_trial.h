/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <stdio.h>
/* Returns 1 only after a distinct child PID was observed and the started
 * child reached Exited, 0 for blocked/failed, -1 for incomplete cleanup.
 * This is a kernel boundary test, not a game launch. */
int nx2_process_trial(FILE *log, FILE *crash);
