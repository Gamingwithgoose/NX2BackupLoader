/* SPDX-License-Identifier: GPL-2.0-only */
#include "launch.h"
#include "ipc_policy.h"
static uint32_t raw_info(uint64_t *out,uint32_t type,uint32_t handle,uint64_t sub){register uint64_t x0 __asm__("x0")=(uintptr_t)out,x1 __asm__("x1")=type,x2 __asm__("x2")=handle,x3 __asm__("x3")=sub;__asm__ volatile("svc #0x29":"+r"(x0),"+r"(x1):"r"(x2),"r"(x3):"memory","cc");if(out)*out=x1;return (uint32_t)x0;}
uint32_t nx2_virtual_info(uint64_t *out,uint32_t type,uint32_t handle,uint64_t sub){Nx2Launch *l=nx2_launch;if(handle==0xffff8001U&&!sub){switch(type){case 4:*out=l->heap_base;return 0;case 5:case 6:case 21:*out=l->heap_capacity;return 0;case 7:case 22:*out=__atomic_load_n(&l->heap_size,__ATOMIC_ACQUIRE);return 0;}}return raw_info(out,type,handle,sub);}
uint32_t nx2_virtual_heap(void **out,uint64_t size){Nx2Launch *l=nx2_launch;if(!out||(size&0x1fffff)||size>l->heap_capacity)return 0xd001;__atomic_store_n(&l->heap_size,size,__ATOMIC_RELEASE);*out=(void *)(uintptr_t)l->heap_base;nx2_rpc(RPC_LOG,size,0,0,0,0,"SetHeapSize handled in private arena",0,0);return 0;}
uint32_t nx2_rtld_query(void *memory,uint32_t *page,uint64_t address){Nx2Launch *l=nx2_launch;uint64_t begin=0,end=UINT64_C(0x8000000000);uint32_t type=0,perm=0;for(uint32_t i=0;i<l->region_count;i++){Nx2Region *r=&l->regions[i];if(address>=r->begin&&address<r->end){begin=r->begin;end=r->end;type=r->type;perm=r->permission;break;}if(r->end<=address&&r->end>begin)begin=r->end;if(r->begin>address&&r->begin<end)end=r->begin;}if(!memory)return 0xdc01;if(address>=UINT64_C(0x8000000000)){begin=UINT64_C(0x8000000000);end=0;type=0x10;perm=0;}uint64_t *q=memory;uint32_t *w=memory;q[0]=begin;q[1]=end-begin;w[4]=type;w[5]=0;w[6]=perm;w[7]=w[8]=w[9]=0;if(page)*page=0;return 0;}
static uint32_t raw_connect(uint32_t *out,const char *name){register uint64_t x0 __asm__("x0")=(uintptr_t)out,x1 __asm__("x1")=(uintptr_t)name;__asm__ volatile("svc #0x1f":"+r"(x0),"+r"(x1)::"memory","cc");if(out)*out=(uint32_t)x1;return (uint32_t)x0;}
static uint32_t raw_sync(uint32_t h){register uint64_t x0 __asm__("x0")=h;__asm__ volatile("svc #0x21":"+r"(x0)::"memory","cc");return (uint32_t)x0;}
static uint32_t raw_buffer(void *b,uint64_t n,uint32_t h){register uint64_t x0 __asm__("x0")=(uintptr_t)b,x1 __asm__("x1")=n,x2 __asm__("x2")=h;__asm__ volatile("svc #0x22":"+r"(x0):"r"(x1),"r"(x2):"memory","cc");return (uint32_t)x0;}
static uint32_t tag(uint32_t h){for(unsigned i=0;i<128;i++){uint64_t v=__atomic_load_n(&nx2_launch->handle_tags[i],__ATOMIC_ACQUIRE);if((uint32_t)v==h&&h)return v>>32;}return 0;}
static void set_tag(uint32_t h,uint32_t kind){if(!h)return;for(unsigned i=0;i<128;i++){uint64_t v=__atomic_load_n(&nx2_launch->handle_tags[i],__ATOMIC_RELAXED);if((uint32_t)v==h){__atomic_store_n(&nx2_launch->handle_tags[i],kind?((uint64_t)kind<<32)|h:0,__ATOMIC_RELEASE);return;}}if(!kind)return;for(unsigned i=0;i<128;i++){uint64_t expected=0;if(__atomic_compare_exchange_n(&nx2_launch->handle_tags[i],&expected,((uint64_t)kind<<32)|h,0,__ATOMIC_RELEASE,__ATOMIC_RELAXED))return;}nx2_rpc(RPC_ABORT,h,0,0,0,0,"Service handle tag table full",0,0);for(;;)__asm__ volatile("wfe");}
uint32_t nx2_connect(uint32_t *out,const char *name){nx2_rpc(RPC_LOG,0x1f,0,0,0,0,"CONNECT_TO_NAMED_PORT_BEGIN",name,0);uint32_t rc=raw_connect(out,name);if(!rc&&name&&name[0]=='s'&&name[1]=='m'&&name[2]==':'&&!name[3])set_tag(*out,1);nx2_rpc(RPC_IPC,0x1f,rc,rc?0:*out,0,0,name,0,0);return rc;}
static uint32_t word(const unsigned char *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static void response_tags(const unsigned char *p,uint64_t n,uint32_t kind){if(n<12||!(word(p+4)&0x80000000))return;uint32_t s=word(p+8),copy=(s>>1)&15,move=(s>>5)&15;uint64_t off=12+((s&1)?8:0);if(off+4*(copy+move)>n)return;for(uint32_t i=0;i<copy+move;i++)set_tag(word(p+off+4*i),kind);}
static uint32_t filter_sync(void *buffer,uint64_t n,uint32_t h,int external){
 unsigned char *p=buffer;uint32_t kind=tag(h);Nx2IpcDecision d=nx2_ipc_decide(p,n,kind);uint32_t cmd=d.command,has=d.valid,control=d.control,want_fsp=d.want_fsp;uint64_t scan=n<256?n:256;
 if(kind==2&&!nx2_ipc_fsp_allowed(d)){__atomic_store_n(&nx2_launch->fsp_denied,1,__ATOMIC_RELEASE);nx2_rpc(RPC_ABORT,h,cmd,has,control,0,"Blocked unapproved native FSP request",0,0);for(;;)__asm__ volatile("wfe");}
 if(kind)nx2_rpc(RPC_IPC,h,cmd,UINT64_MAX,kind,0,"IPC_BEGIN",0,0);
 uint32_t rc=external?raw_buffer(buffer,n,h):raw_sync(h);
 if(!rc){if(want_fsp)response_tags(p,scan,2);else if(kind&&control&&(cmd==1||cmd==2||cmd==4))response_tags(p,scan,kind);}
 if(kind||rc)nx2_rpc(RPC_IPC,h,cmd,rc,kind,0,want_fsp?"fsp-srv service acquired":(kind==2?"FSP request":"IPC"),0,0);
 return rc;
}
uint32_t nx2_sync(uint32_t h){void *tlr;__asm__ volatile("mrs %0,tpidrro_el0":"=r"(tlr));return filter_sync(tlr,256,h,0);}
uint32_t nx2_sync_buffer(void *p,uint64_t n,uint32_t h){return filter_sync(p,n,h,1);}
uint32_t nx2_close_handle(uint32_t h){register uint64_t x0 __asm__("x0")=h;__asm__ volatile("svc #0x16":"+r"(x0)::"memory","cc");if(!(uint32_t)x0)set_tag(h,0);return (uint32_t)x0;}
void nx2_break(uint32_t reason,uint64_t address,uint64_t size){nx2_rpc(RPC_ABORT,reason,address,size,0,0,"Native SDK Break",0,0);for(;;)__asm__ volatile("wfe");}
void nx2_debug(const char *text,uint64_t n){char buf[NX2_PATH_MAX];if(n>=sizeof(buf))n=sizeof(buf)-1;for(uint64_t i=0;i<n;i++)buf[i]=text[i];buf[n]=0;nx2_rpc(RPC_LOG,0,0,0,0,0,buf,0,0);}

uint32_t nx2_async(uint32_t *out,void *p,uint64_t n,uint32_t h){if(tag(h)){nx2_rpc(RPC_ABORT,h,0,0,0,0,"Refused asynchronous SM/FSP operation",0,0);for(;;)__asm__ volatile("wfe");}register uint64_t x0 __asm__("x0")=(uintptr_t)out,x1 __asm__("x1")=(uintptr_t)p,x2 __asm__("x2")=n,x3 __asm__("x3")=h;__asm__ volatile("svc #0x23":"+r"(x0),"+r"(x1):"r"(x2),"r"(x3):"memory","cc");if(out)*out=(uint32_t)x1;return (uint32_t)x0;}
void nx2_exit_gate(void){nx2_rpc(RPC_ABORT,0,0,0,0,0,"Game requested process exit",0,0);for(;;)__asm__ volatile("wfe");}
