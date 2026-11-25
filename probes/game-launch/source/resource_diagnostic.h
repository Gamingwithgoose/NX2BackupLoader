/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <stdint.h>
#include <stdio.h>
typedef struct {
 uint64_t sequence,last_elapsed_ms,min_system_free,max_system_used;
 uint32_t handle_close_failed;
 uint32_t declared_remote,declared_remote_known;
} Nx2ResourceDiagnostic;
/* Host-only observations. Does not allocate memory, alter quotas, or execute
 * privileged quota queries unless the loader advertises those SVCs. */
void nx2_resource_diagnostic(FILE *,Nx2ResourceDiagnostic *,const char *,
                              uint64_t elapsed_ms,uint64_t declared_bytes,int force);
