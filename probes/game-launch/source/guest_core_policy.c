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
