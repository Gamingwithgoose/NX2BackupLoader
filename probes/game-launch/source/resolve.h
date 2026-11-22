/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include "module.h"
#include "patch_plan.h"
/* Hook bookkeeping capacity follows real executable padding, not a fixed
 * global 128-entry array. The patcher enforces actual trampoline bytes. */
int nx2_resolve_plan(Nx2Module *, unsigned role, Nx2PatchSpec *,
                     size_t capacity, unsigned *, char error[160]);
