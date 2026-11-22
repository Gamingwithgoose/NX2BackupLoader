/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <stdint.h>
/* Select a supported guest worker core. An NPDM-declared default CPU
 * takes precedence, even when it is also hosting the launch watchdog:
 * these are schedulable threads, not exclusive core reservations.
 * Without valid metadata, retain the legacy non-host core fallback. */
int nx2_guest_select_core(uint64_t allowed_mask,int host_core,
                          int npdm_core);
/* Optional dedicated watchdog CPU, without overriding the guest NPDM
 * preference or sharing the dedicated serial SD worker's CPU. */
int nx2_watchdog_select_core(uint64_t allowed_mask,int host_core,
                            int guest_core,int storage_core);
