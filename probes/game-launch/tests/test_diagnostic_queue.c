/* SPDX-License-Identifier: GPL-2.0-only */
#include "diagnostic_queue.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif
static Nx2Launch *l;
#ifdef _WIN32
static DWORD WINAPI produce(LPVOID context){
 uintptr_t id=(uintptr_t)context;
 char text[32];snprintf(text,sizeof(text),"producer-%llu",(unsigned long long)id);
 for(unsigned i=0;i<5000;i++)nx2_log_enqueue(l,id,i,id^i,0,text,"owned");
 return 0;
}
#endif
int main(void){
 l=calloc(1,sizeof(*l));assert(l);
 char text[]="before";
 for(unsigned i=0;i<NX2_LOG_SLOTS;i++)assert(nx2_log_enqueue(l,i,0,0,0,text,0));
 text[0]='X';
 assert(!nx2_log_enqueue(l,999,0,0,0,"overflow",0));
 assert(l->log_dropped==1);
 Nx2Request r={0};unsigned count=0;
 while(nx2_log_dequeue(l,&r)){assert(r.op==RPC_LOG && !strcmp(r.path,"before"));count++;}
 assert(count==NX2_LOG_SLOTS && !nx2_log_dequeue(l,&r));
 memset(l,0,sizeof(*l));
 char mount_name[]="save";
 assert(nx2_save_mount_notice_enqueue(l,0x202,0x12345678,1,mount_name));
 mount_name[0]='X';
 assert(nx2_log_dequeue(l,&r));
 assert(r.op==RPC_SAVE_MOUNT && r.a==0x202 && r.b==0x12345678 && r.c==1);
 assert(!strcmp(r.path,"save") && l->save_notice_enqueued==1 && !l->save_notice_served);
 for(unsigned i=0;i<NX2_LOG_SLOTS;i++)assert(nx2_log_enqueue(l,i,0,0,0,"full",0));
 assert(!nx2_save_mount_notice_enqueue(l,0,0,0,"save"));
 assert(l->save_notice_dropped==1 && l->save_notice_enqueued==1);
 while(nx2_log_dequeue(l,&r))assert(r.op==RPC_LOG);
 memset(l,0,sizeof(*l));
#ifdef _WIN32
 HANDLE threads[4];
 for(uintptr_t i=0;i<4;i++){threads[i]=CreateThread(0,0,produce,(LPVOID)i,0,0);assert(threads[i]);}
 unsigned consumed=0;
 for(;;){
  while(nx2_log_dequeue(l,&r)){
   char expected[32];snprintf(expected,sizeof(expected),"producer-%llu",(unsigned long long)r.a);
   assert(r.a<4 && r.b<5000 && r.c==(r.a^r.b));
   assert(!strcmp(r.path,expected) && !strcmp(r.path2,"owned"));consumed++;
  }
  if(WaitForMultipleObjects(4,threads,TRUE,0)==WAIT_OBJECT_0)break;
  SwitchToThread();
 }
 while(nx2_log_dequeue(l,&r))consumed++;
 assert(consumed==l->log_enqueued && consumed+l->log_dropped==20000);
 for(unsigned i=0;i<4;i++)CloseHandle(threads[i]);
#endif
 free(l);puts("Nonblocking diagnostic queue: PASS");return 0;
}
