/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <stdint.h>
#include "ipc_policy.h"
/* Route a guest FSP domain request to the SD-backed private filesystem.
 * Returns 1 if the request was handled and its CMIF response was written
 * in-place; otherwise 0 so the ordinary policy can decide. */
int nx2_guest_fsp_route(void *buffer, uint64_t buffer_size, uint32_t session,
                        uint32_t session_kind, Nx2IpcDecision request);
/* Return a valid guest-visible CMIF unsupported error without touching the
 * host filesystem or pretending an unimplemented operation succeeded. */
int nx2_guest_fsp_unsupported(void *buffer,uint64_t size,Nx2IpcDecision request);
