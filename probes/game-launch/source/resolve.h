/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include "module.h"
#include "patch_plan.h"
#define NX2_RESOLVED_MAX 128
int nx2_resolve_plan(Nx2Module *, unsigned role, Nx2PatchSpec *, unsigned *,
                     char error[160]);
