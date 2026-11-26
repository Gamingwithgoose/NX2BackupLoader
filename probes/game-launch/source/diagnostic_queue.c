/* SPDX-License-Identifier: GPL-2.0-only */
#include "diagnostic_queue.h"
static void copy_text(char *out,const char *in){
 unsigned i=0;
 if(in)for(;i<NX2_PATH_MAX-1 && in[i];i++)out[i]=in[i];
 out[i]=0;
}
static int enqueue(Nx2Launch *l,uint32_t op,uint64_t a,uint64_t b,uint64_t c,
                     uint64_t d,const char *path,const char *detail){
 uint32_t first=__atomic_fetch_add(&l->log_next,1,__ATOMIC_RELAXED);
 for(unsigned j=0;j<NX2_LOG_SLOTS;j++){
  Nx2LogRecord *r=&l->logs[(first+j)%NX2_LOG_SLOTS];
  uint32_t expected=0;
  if(!__atomic_compare_exchange_n(&r->state,&expected,1,0,
                                  __ATOMIC_ACQUIRE,__ATOMIC_RELAXED))continue;
  r->op=op;r->a=a;r->b=b;r->c=c;r->d=d;
  copy_text(r->path,path);copy_text(r->path2,detail);
  __atomic_fetch_add(&l->log_enqueued,1,__ATOMIC_RELAXED);
  if(op==RPC_SAVE_MOUNT)__atomic_fetch_add(&l->save_notice_enqueued,1,__ATOMIC_RELAXED);
  __atomic_store_n(&r->state,2,__ATOMIC_RELEASE);
  return 1;
 }
 __atomic_fetch_add(&l->log_dropped,1,__ATOMIC_RELAXED);
 if(op==RPC_SAVE_MOUNT)__atomic_fetch_add(&l->save_notice_dropped,1,__ATOMIC_RELAXED);
 return 0;
}
int nx2_log_enqueue(Nx2Launch *l,uint64_t a,uint64_t b,uint64_t c,
                     uint64_t d,const char *path,const char *detail){
 return enqueue(l,RPC_LOG,a,b,c,d,path,detail);
}
int nx2_save_mount_notice_enqueue(Nx2Launch *l,uint32_t result,uint64_t caller,
                                    uint32_t profile,const char *name){
 return enqueue(l,RPC_SAVE_MOUNT,result,caller,profile,0,name,0);
}
int nx2_log_dequeue(Nx2Launch *l,Nx2Request *out){
 /* One host consumer owns ready records until it releases state=0. */
 for(unsigned i=0;i<NX2_LOG_SLOTS;i++){
  Nx2LogRecord *r=&l->logs[i];
  if(__atomic_load_n(&r->state,__ATOMIC_ACQUIRE)!=2)continue;
  out->op=r->op;out->a=r->a;out->b=r->b;out->c=r->c;out->d=r->d;
  copy_text(out->path,r->path);copy_text(out->path2,r->path2);
  __atomic_store_n(&r->state,0,__ATOMIC_RELEASE);
  __atomic_fetch_add(&l->log_drained,1,__ATOMIC_RELAXED);
  return 1;
 }
 return 0;
}
