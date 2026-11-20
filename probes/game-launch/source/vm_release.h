/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <stdio.h>
#include "vm_memory.h"
/* A launch-only transition. The caller ends the process after any failure or
 * after game execution; it must not return to hbloader with a shortened heap. */
int nx2_release_heap_for_vm(FILE *,const Nx2HeapPlan *,uint64_t,uint64_t,uint64_t,uint64_t *);
