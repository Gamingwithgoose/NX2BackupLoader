/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include "launch.h"
typedef struct {uint64_t module,offset,svc_offset,svc,mode,original_field;const char *symbol,*label;uintptr_t gate;} Nx2PatchSpec;
extern const Nx2PatchSpec nx2_patch_specs[];
extern const uint64_t nx2_patch_spec_count;

uint32_t nx2_connect_hipc_gate(uint32_t *,const char *);
