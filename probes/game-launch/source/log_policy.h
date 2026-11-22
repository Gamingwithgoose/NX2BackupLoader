/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <string.h>
extern int nx2_log_verbose;
/* Error/warning classification uses the stable event ID, not path or title.
 * Raw IPC errors are independently selected by real result codes. */
static inline int nx2_log_is_issue(const char *s){
 static const char *const tokens[]={
  "WARN","ERROR","FAIL","REFUS","BLOCK","DENIED","INVALID",
  "UNSUPPORTED","ABORT","SDK_BREAK","NATIVE_STOP","STOP_PROCESS",
  "NATIVE_CPU_FAULT","CRASH","FAULT","TIMEOUT","HUNG","STALL",
  "IPC_EVENTS_DROPPED","PRELAUNCH_STOP","LOG_CONTEXT",
  "IPC_EVENT_TAIL","RPC_TRAIL","GUEST_CALLSITE","GUEST_FS_READ",
  "GUEST_SAVE_GATE","HOST_SNAPSHOT","STORAGE_SERVICE_SNAPSHOT",
  "GUEST_FSP_ROUTE_SNAPSHOT","NUP_STORAGE_PROGRESS"
 };
 if(!s)return 0;
 for(unsigned i=0;i<sizeof(tokens)/sizeof(tokens[0]);i++)
  if(strstr(s,tokens[i]))return 1;
 return 0;
}
static inline int nx2_log_keep(const char *s){
 return nx2_log_verbose || nx2_log_is_issue(s);
}
