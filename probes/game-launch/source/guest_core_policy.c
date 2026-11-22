/* SPDX-License-Identifier: GPL-2.0-only */
#include "guest_core_policy.h"
int nx2_guest_select_core(uint64_t allowed_mask,int host_core,
                          int npdm_core){
 if(npdm_core>=0 && npdm_core<6 &&
    (allowed_mask & (UINT64_C(1)<<npdm_core)))
  return npdm_core;
 for(int core=0;core<6;core++)
  if(core!=host_core && (allowed_mask & (UINT64_C(1)<<core)))
   return core;
 /* The watchdog can share its own core if it is the only permitted one. */
 if(host_core>=0 && host_core<6 &&
    (allowed_mask & (UINT64_C(1)<<host_core)))
  return host_core;
 return -1;
}

int nx2_watchdog_select_core(uint64_t allowed_mask,int host_core,
                            int guest_core,int storage_core){
 for(int core=5;core>=0;core--){
  if(core==host_core||core==guest_core||core==storage_core)continue;
  if(allowed_mask&(UINT64_C(1)<<core))return core;
 }
 return -1;
}

/* Never request a priority outside the process's Horizon PriorityMask. */
int nx2_guest_select_priority(uint64_t mask,int npdm_priority,int legacy_priority){
 if(npdm_priority>=0&&npdm_priority<64&&
    (mask&(UINT64_C(1)<<npdm_priority)))return npdm_priority;
 if(legacy_priority>=0&&legacy_priority<64&&
    (mask&(UINT64_C(1)<<legacy_priority)))return legacy_priority;
 int target=npdm_priority>=0&&npdm_priority<64?npdm_priority:
            legacy_priority>=0&&legacy_priority<64?legacy_priority:44;
 int chosen=-1,best_delta=65;
 for(int p=0;p<64;p++){
  if(!(mask&(UINT64_C(1)<<p)))continue;
  int delta=p>target?p-target:target-p;
  if(delta<best_delta){best_delta=delta;chosen=p;}
 }
 return chosen;
}
/* Keep the serial filesystem worker responsive to guest IPC without
 * acquiring a scheduling priority the host is not permitted to use. */
int nx2_storage_select_priority(uint64_t mask,int guest_priority){
 if(guest_priority<0||guest_priority>=64||
    !(mask&(UINT64_C(1)<<guest_priority)))return -1;
 for(int p=guest_priority-1;p>=0;p--)
  if(mask&(UINT64_C(1)<<p))return p;
 return guest_priority;
}
