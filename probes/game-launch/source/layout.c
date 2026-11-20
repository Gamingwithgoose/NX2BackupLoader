/* SPDX-License-Identifier: GPL-2.0-only */
#include <stdint.h>
#include <stddef.h>
#include "launch.h"
/* Native worker test tools read this ABI table from their linked test ELF. */
const uint64_t __attribute__((used)) nx2_launch_layout[]={sizeof(Nx2Launch),sizeof(Nx2LaunchApi),sizeof(Nx2Request),sizeof(Nx2Object),offsetof(Nx2Launch,api),offsetof(Nx2Launch,requests),offsetof(Nx2Launch,objects),offsetof(Nx2Launch,heap_base),offsetof(Nx2Launch,arena_base),offsetof(Nx2Launch,worker_handle),offsetof(Nx2Launch,cache_size),offsetof(Nx2Launch,sample_results),offsetof(Nx2Launch,samples),offsetof(Nx2Launch,save_result),offsetof(Nx2Launch,regions),offsetof(Nx2Launch,region_count),offsetof(Nx2Launch,handle_tags),offsetof(Nx2Launch,storage),offsetof(Nx2Request,buffer),offsetof(Nx2Request,path)};
