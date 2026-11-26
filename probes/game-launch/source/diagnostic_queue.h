/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
/* Bounded freestanding label classification; no stdio or allocation. */
static inline int nx2_diag_failure(const char *s){
 const char *keys[]={"FAILED","REFUSED","ERROR"};
 if(!s)return 0;
 for(unsigned i=0;i<96&&s[i];i++)for(unsigned k=0;k<3;k++){
  unsigned j=0;while(keys[k][j]&&i+j<96&&s[i+j]==keys[k][j])j++;
  if(!keys[k][j])return 1;
 }
 return 0;
}
#include "launch.h"
/* Bounded, allocation-free multiple-producer / single-consumer queue.
 * Full queues drop diagnostics, never stall guest execution. */
int nx2_log_enqueue(Nx2Launch *,uint64_t,uint64_t,uint64_t,uint64_t,
                     const char *,const char *);
int nx2_log_dequeue(Nx2Launch *,Nx2Request *);
/* Only records the result of an already completed SDK registration.
 * Return value is queue acceptance, never a filesystem result. */
int nx2_save_mount_notice_enqueue(Nx2Launch *,uint32_t result,uint64_t caller,
                                    uint32_t profile,const char *name);
