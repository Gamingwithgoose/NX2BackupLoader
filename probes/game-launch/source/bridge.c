/* SPDX-License-Identifier: GPL-2.0-only */
#include "launch.h"
#include "raw_svc.h"
#include "file_range_policy.h"
#include "cache_catalog.h"
#include <stdio.h>
Nx2Launch *nx2_launch;
static void copy(void *d,const void *s,uint64_t n){unsigned char *a=d;const unsigned char *b=s;for(uint64_t i=0;i<n;i++)a[i]=b[i];}
static void zero(void *d,uint64_t n){unsigned char *p=d;for(uint64_t i=0;i<n;i++)p[i]=0;}
static int text(char *out,const char *s){if(!s){out[0]=0;return 1;}for(unsigned i=0;i<NX2_PATH_MAX;i++){out[i]=s[i];if(!s[i])return 1;}out[0]=0;return 0;}
static uint32_t w(const void *v){const unsigned char *p=v;uint32_t r=0;for(unsigned i=0;i<4;i++)r|=(uint32_t)p[i]<<(i*8);return r;}
static void wake(void){__asm__ volatile("sev" ::: "memory");}
/* CPU event registers belong to cores, not scheduled threads. Poll the
 * acquire state after a bounded kernel sleep; no event delivery is required. */
static void wait(void){nx2_raw_sleep(100000);}
uint32_t nx2_rpc(uint32_t op,uint64_t a,uint64_t b,uint64_t c,uint64_t d,void *buffer,const char *path,const char *path2,uint64_t *answer){
 Nx2Launch *l=nx2_launch;Nx2Request *r=0;
 /* The host can persist the last guest callsites even if the guest disappears
  * before its log RPC finishes. Never log from the exception handler. */
 uint32_t ticket=__atomic_fetch_add(&l->rpc_trace_next,1,__ATOMIC_RELAXED)+1;
 Nx2RpcTrace *trace=&l->rpc_trace[(ticket-1)%NX2_RPC_TRACE_SLOTS];
 uint64_t thread=0;
 __asm__ volatile("mrs %0,tpidrro_el0":"=r"(thread));
 __atomic_store_n(&trace->sequence,0,__ATOMIC_RELAXED);
 __atomic_store_n(&trace->caller,(uint64_t)(uintptr_t)__builtin_return_address(0),__ATOMIC_RELAXED);
 __atomic_store_n(&trace->thread,thread,__ATOMIC_RELAXED);
 __atomic_store_n(&trace->op,op,__ATOMIC_RELAXED);
 __atomic_store_n(&trace->result,NX2_INVALID,__ATOMIC_RELAXED);
 __atomic_store_n(&trace->phase,1,__ATOMIC_RELAXED);
 __atomic_store_n(&trace->sequence,ticket,__ATOMIC_RELEASE);
 for(;;){for(unsigned i=0;i<NX2_RPC_SLOTS;i++){uint32_t expected=0;if(__atomic_compare_exchange_n(&l->requests[i].state,&expected,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)){r=&l->requests[i];break;}}if(r)break;wait();}
 r->op=op;r->a=a;r->b=b;r->c=c;r->d=d;r->buffer=buffer;r->answer=0;r->result=NX2_INVALID;
 if(!text(r->path,path)||!text(r->path2,path2)){__atomic_store_n(&trace->result,NX2_INVALID,__ATOMIC_RELAXED);__atomic_store_n(&trace->phase,2,__ATOMIC_RELEASE);__atomic_store_n(&r->state,0,__ATOMIC_RELEASE);wake();return NX2_INVALID;}
 __atomic_store_n(&r->state,2,__ATOMIC_RELEASE);wake();
 while(__atomic_load_n(&r->state,__ATOMIC_ACQUIRE)!=3)wait();
 uint32_t result=r->result;if(answer)*answer=r->answer;__atomic_store_n(&trace->result,result,__ATOMIC_RELAXED);__atomic_store_n(&trace->phase,2,__ATOMIC_RELEASE);__atomic_store_n(&r->state,0,__ATOMIC_RELEASE);wake();return result;
}
static uint32_t extent(void *ctx,uint32_t file,uint64_t off,void *buf,uint64_t size){(void)ctx;return nx2_rpc(RPC_ROM_READ,file,off,size,0,buf,0,0,0);}
static void noop(void *obj){(void)obj;}
static __attribute__((noinline)) uint32_t storage_read(void *self,int64_t offset,void *buffer,uint64_t size){
 (void)self;
 Nx2Launch *l=nx2_launch;
 /* Called through the guest's IStorage vtable: unlike nx2_rpc's return
  * address, this return address belongs to the original guest caller. */
 __atomic_fetch_add(&l->fs_guest_read_entries,1,__ATOMIC_RELAXED);
 __atomic_store_n(&l->fs_guest_read_caller,
                  (uint64_t)(uintptr_t)__builtin_return_address(0),__ATOMIC_RELEASE);
 __atomic_store_n(&l->fs_guest_read_offset,(uint64_t)offset,__ATOMIC_RELAXED);
 __atomic_store_n(&l->fs_guest_read_size,size,__ATOMIC_RELAXED);
 uint32_t rc=offset<0?NX2_INVALID:
   nx2_rom_read(&l->rom,(uint64_t)offset,buffer,size,extent,0);
 __atomic_store_n(&l->fs_guest_read_result,rc,__ATOMIC_RELAXED);
 __atomic_fetch_add(&l->fs_guest_read_returns,1,__ATOMIC_RELEASE);
 return rc;
}
static uint32_t storage_write(void *self,int64_t off,const void *p,uint64_t n){(void)self;(void)off;(void)p;(void)n;return NX2_FS_UNSUPPORTED;}
static uint32_t storage_flush(void *self){(void)self;return 0;}
static uint32_t storage_resize(void *self,int64_t n){(void)self;(void)n;return NX2_FS_UNSUPPORTED;}
static uint32_t storage_size(void *self,int64_t *n){(void)self;if(!n)return NX2_INVALID;*n=(int64_t)nx2_launch->rom.total;return 0;}
/* IStorage::OperateRange: validate requested virtual range before claiming
 * support. This is a read-only RomFS image, not a writable backing store.
 * QueryRange returns the standard 0x40-byte range-information structure;
 * InvalidateCache is a valid no-op because all reads are revalidated by the
 * backing reader. Do not silently succeed on unknown operation IDs. */
static uint32_t storage_range(void *self,void *out,uint64_t outsize,
                             uint32_t op,int64_t off,int64_t size,
                             const void *in,uint64_t insize){
 (void)self;
 if(off<0||size<0||(uint64_t)off>nx2_launch->rom.total||
    (uint64_t)size>nx2_launch->rom.total-(uint64_t)off)
   return NX2_INVALID;
 if(op==2){ /* InvalidateCache */
   if(outsize||insize)return NX2_INVALID;
   return 0;
 }
 if(op==3){ /* QueryRange: FsRangeInfo is exactly 64 bytes */
   if(!out||outsize!=0x40||in||insize)return NX2_INVALID;
   zero(out,0x40);
   return 0;
 }
 return NX2_FS_UNSUPPORTED;
}
static Nx2Object *object(uintptr_t vt,uint64_t handle,uint64_t mode){for(unsigned i=0;i<NX2_OBJECTS;i++){uint64_t expected=0;if(__atomic_compare_exchange_n(&nx2_launch->objects[i].active,&expected,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)){Nx2Object *o=&nx2_launch->objects[i];o->vtable=vt;o->handle=handle;o->mode=mode;o->profile_high=0;return o;}}return 0;}
static void release_fs(void *p){
 Nx2Object *o=p;
 if(o!=&nx2_launch->save_fs)__atomic_store_n(&o->active,0,__ATOMIC_RELEASE);
}
static void release_file(void *p){Nx2Object *o=p;if(o->active){nx2_rpc(RPC_SAVE_CLOSE,o->handle,0,0,0,0,0,0,0);__atomic_store_n(&o->active,0,__ATOMIC_RELEASE);}}
static void release_dir(void *p){Nx2Object *o=p;if(o->active){nx2_rpc(RPC_DIR_CLOSE,o->handle,0,0,0,0,0,0,0);__atomic_store_n(&o->active,0,__ATOMIC_RELEASE);}}
/* No guest-visible transfer ceiling: forward genuine IFile reads in fair
 * 1-MiB increments and preserve native EOF and actual-byte semantics. */
static uint32_t file_read(void *p,uint64_t *read,int64_t offset,void *buf,
                          uint64_t size,const void *option){
 (void)option;
 if(!p||!read||!nx2_save_read_request_valid((uint64_t)offset,size)||
    (size&&!buf))return NX2_INVALID;
 Nx2Object *o=p;
 uint64_t done=0;
 do{
  uint64_t amount=size-done;
  if(amount>0x100000U)amount=0x100000U;
  uint64_t got=0;
  uint32_t error=nx2_rpc(RPC_SAVE_READ,o->handle,
                 (uint64_t)offset+done,amount,0,
                 amount?(unsigned char*)buf+done:buf,0,0,&got);
  if(error)return error;
  if(got>amount)return NX2_INVALID;
  done+=got;
  if(got<amount)break;
 }while(done<size);
 *read=done;
 return 0;
}
/* Flush is applied only to the final native write chunk. Zero-sized writes
 * still reach the real filesystem, retaining its handle/mode validation. */
static uint32_t file_write(void *p,int64_t offset,const void *buf,
                           uint64_t size,const void *option){
 uint32_t flags=option?w(option):0;
 if(!p||!nx2_save_write_request_valid((uint64_t)offset,size,flags)||
    (size&&!buf))return NX2_INVALID;
 Nx2Object *o=p;
 uint64_t done=0;
 do{
  uint64_t amount=size-done;
  if(amount>0x100000U)amount=0x100000U;
  uint32_t last=(done+amount==size)?flags:0;
  uint32_t error=nx2_rpc(RPC_SAVE_WRITE,o->handle,
              (uint64_t)offset+done,amount,last,
              amount?(unsigned char*)buf+done:(void*)buf,0,0,0);
  if(error)return error;
  done+=amount;
 }while(done<size);
 return 0;
}
static uint32_t file_flush(void *p){return nx2_rpc(RPC_SAVE_FLUSH,((Nx2Object *)p)->handle,0,0,0,0,0,0,0);}
static uint32_t file_resize(void *p,int64_t n){if(n<0)return NX2_INVALID;return nx2_rpc(RPC_SAVE_RESIZE,((Nx2Object *)p)->handle,n,0,0,0,0,0,0);}
static uint32_t file_size(void *p,int64_t *n){uint64_t size=0;uint32_t rc=nx2_rpc(RPC_SAVE_SIZE,((Nx2Object *)p)->handle,0,0,0,0,0,0,&size);if(n)*n=(int64_t)size;return n?rc:NX2_INVALID;}
/* IFile::OperateRange must apply to the actual guest save/cache file,
 * not the unrelated, read-only ROMFS virtual IStorage. */
static uint32_t file_range(void *p,void *out,uint64_t outsize,
                           uint32_t op,int64_t off,int64_t n,
                           const void *in,uint64_t insize){
 Nx2Object *o=p;
 if(!o || !o->active ||
    o->vtable!=(uintptr_t)&nx2_launch->file_table[2] || !o->handle)
  return NX2_INVALID;
 if(!nx2_file_range_allowed(op,off,n,out,outsize,in,insize))
  return NX2_FS_UNSUPPORTED;
 /* Use the same real SD-backed IFile range RPC as native FSP command 5.
  * InvalidateCache does not expose the internal 64-byte ABI scratch area. */
 unsigned char range[0x40]={0};
 uint32_t rc=nx2_rpc(RPC_SAVE_OPERATE_RANGE,o->handle,(uint64_t)op,
                     (uint64_t)off,(uint64_t)n,range,0,0,0);
 if(!rc && op==3)copy(out,range,sizeof(range));
 return rc;
}
static const char *path(const void *p){return nx2_launch->api.path_string(p);}
/* Translate cache-mounted paths into an isolated, per-index SD subtree.
 * The host's save-path validator still checks all path components. */
static const char *routed_path(void *self,const void *p,char out[NX2_PATH_MAX]){
 const char *raw=path(p);
 if(!raw)return 0;
 if(!self || self==&nx2_launch->save_fs)return raw;
 Nx2Object *fs=self;
 if(!fs->active || fs->vtable!=(uintptr_t)&nx2_launch->fs_table[2] ||
    !fs->mode)return 0;
 if(fs->mode & NX2_PROFILE_FS_MODE){
  if(fs->mode & NX2_PROFILE_PRIMARY_FLAG)return raw;
  Nx2UserId uid={fs->handle,fs->profile_high};char prefix[80];
  if(!nx2_profile_relative(prefix,sizeof(prefix),uid))return 0;
  while(*raw=='/')raw++;
  int n=snprintf(out,NX2_PATH_MAX,"%s%s%s",prefix,*raw?"/":"",raw);
  return n>=0&&n<NX2_PATH_MAX?out:0;
 }
 while(*raw=='/')raw++;
 int n=snprintf(out,NX2_PATH_MAX,"cache-storage/%llu%s%s",
                (unsigned long long)(fs->mode-1),*raw?"/":"",raw);
 return n>=0 && n<NX2_PATH_MAX?out:0;
}
static uint32_t create_file(void *s,const void *p,int64_t size,int option){
 if(size<0)return NX2_INVALID;
 char temp[NX2_PATH_MAX];const char *name=routed_path(s,p,temp);
 return name?nx2_rpc(RPC_SAVE_CREATE,size,(uint32_t)option,0,0,0,name,0,0):NX2_INVALID;
}
#define PATH_FN(fn,operation) static uint32_t fn(void *s,const void *p){char temp[NX2_PATH_MAX];const char *routed=routed_path(s,p,temp);return routed?nx2_rpc(operation,0,0,0,0,0,routed,0,0):NX2_INVALID;}
PATH_FN(delete_file,RPC_SAVE_DELETE) PATH_FN(mkdir_save,RPC_SAVE_MKDIR) PATH_FN(rmdir_save,RPC_SAVE_RMDIR) PATH_FN(rmtree_save,RPC_SAVE_RMTREE) PATH_FN(clean_save,RPC_SAVE_CLEAN)
static uint32_t rename_file(void *s,const void *a,const void *b){
 char aa[NX2_PATH_MAX],bb[NX2_PATH_MAX];
 const char *from=routed_path(s,a,aa),*to=routed_path(s,b,bb);
 return from&&to?nx2_rpc(RPC_SAVE_RENAME_FILE,0,0,0,0,0,from,to,0):NX2_INVALID;
}
static uint32_t rename_dir(void *s,const void *a,const void *b){
 char aa[NX2_PATH_MAX],bb[NX2_PATH_MAX];
 const char *from=routed_path(s,a,aa),*to=routed_path(s,b,bb);
 return from&&to?nx2_rpc(RPC_SAVE_RENAME_DIR,0,0,0,0,0,from,to,0):NX2_INVALID;
}
static uint32_t entry_type(void *s,uint32_t *out,const void *p){
 if(!out)return NX2_INVALID;
 char temp[NX2_PATH_MAX];const char *name=routed_path(s,p,temp);
 uint64_t v=0;uint32_t rc=name?nx2_rpc(RPC_SAVE_TYPE,0,0,0,0,0,name,0,&v):NX2_INVALID;
 if(!rc)*out=(uint32_t)v;
 return rc;
}
static uint32_t free_size(void *s,int64_t *out,const void *p){
 if(!out)return NX2_INVALID;
 char temp[NX2_PATH_MAX];const char *name=routed_path(s,p,temp);
 uint64_t v=0;uint32_t rc=name?nx2_rpc(RPC_SAVE_FREE,0,0,0,0,0,name,0,&v):NX2_INVALID;
 if(!rc)*out=(int64_t)v;
 return rc;
}
static uint32_t total_size(void *s,int64_t *out,const void *p){
 if(!out)return NX2_INVALID;
 char temp[NX2_PATH_MAX];const char *name=routed_path(s,p,temp);
 uint64_t v=0;uint32_t rc=name?nx2_rpc(RPC_SAVE_TOTAL,0,0,0,0,0,name,0,&v):NX2_INVALID;
 if(!rc)*out=(int64_t)v;
 return rc;
}
static uint32_t open_file(void *s,void **out,const void *p,uint32_t mode){
 if(!out)return NX2_INVALID;
 char temp[NX2_PATH_MAX];const char *name=routed_path(s,p,temp);
 if(!name)return NX2_INVALID;
 uint64_t handle=0;uint32_t rc=nx2_rpc(RPC_SAVE_OPEN,mode,0,0,0,0,name,0,&handle);
 if(rc)return rc;
 Nx2Object *o=object((uintptr_t)&nx2_launch->file_table[2],handle,mode);
 if(!o){nx2_rpc(RPC_SAVE_CLOSE,handle,0,0,0,0,0,0,0);return NX2_LIMIT;}
 *out=o;return 0;
}
static uint32_t open_dir(void *s,void **out,const void *p,uint32_t mode){
 if(!out)return NX2_INVALID;
 char temp[NX2_PATH_MAX];const char *name=routed_path(s,p,temp);
 if(!name)return NX2_INVALID;
 uint64_t handle=0;uint32_t rc=nx2_rpc(RPC_DIR_OPEN,mode,0,0,0,0,name,0,&handle);
 if(rc)return rc;
 Nx2Object *o=object((uintptr_t)&nx2_launch->dir_table[2],handle,mode);
 if(!o){nx2_rpc(RPC_DIR_CLOSE,handle,0,0,0,0,0,0,0);return NX2_LIMIT;}
 *out=o;return 0;
}
static uint32_t commit(void *s){(void)s;return nx2_rpc(RPC_COMMIT,0,0,0,0,0,0,0,0);}
static uint32_t commit_provisional(void *s,int64_t id){(void)id;return commit(s);}
static uint32_t unsupported(void *s){(void)s;return NX2_FS_UNSUPPORTED;}
/* The real kernel directory entry is 0x310 bytes; 1,024 entries is a
 * per-RPC fairness batch, NOT an externally visible directory limit. */
static uint32_t dir_read(void *s,uint64_t *out,void *entries,int64_t max){
 if(!s||!out||max<0||(max&&!entries)||
    (uint64_t)max>UINTPTR_MAX/0x310U ||
    ((uint64_t)max && (uintptr_t)entries>
      UINTPTR_MAX-(uint64_t)max*0x310U))return NX2_INVALID;
 uint64_t done=0,capacity=(uint64_t)max;
 do{
  uint64_t batch=capacity-done;
  if(batch>1024)batch=1024;
  uint64_t actual=0;
  uint32_t error=nx2_rpc(RPC_DIR_READ,((Nx2Object*)s)->handle,
                batch,0,0,batch?(unsigned char*)entries+done*0x310U:entries,
                0,0,&actual);
  if(error)return error;
  if(actual>batch)return NX2_INVALID;
  done+=actual;
  if(actual<batch)break;
 }while(done<capacity);
 *out=done;
 return 0;
}
static uint32_t dir_count(void *s,int64_t *out){uint64_t n=0;uint32_t rc=nx2_rpc(RPC_DIR_COUNT,((Nx2Object *)s)->handle,0,0,0,0,0,0,&n);if(out)*out=(int64_t)n;return out?rc:NX2_INVALID;}
static uint32_t timestamp(void *self,void *out,const void *p){
 if(!out)return NX2_INVALID;
 char temp[NX2_PATH_MAX];const char *name=routed_path(self,p,temp);
 return name?nx2_rpc(RPC_SAVE_TIMESTAMP,0,0,0,0,out,name,0,0):NX2_INVALID;
}
/* Report real backing filesystem capabilities instead of fabricated limits. */
static uint32_t fs_attribute(void *self,void *out){
 (void)self;
 if(!out)return NX2_INVALID;
 return nx2_rpc(RPC_SAVE_ATTRIBUTE,0,0,0,0,out,0,0,0);
}
void nx2_bridge_init(Nx2Launch *l){
 nx2_launch=l;
 uintptr_t storage[]={0,l->api.storage_rtti,(uintptr_t)noop,(uintptr_t)noop,(uintptr_t)storage_read,(uintptr_t)storage_write,(uintptr_t)storage_flush,(uintptr_t)storage_resize,(uintptr_t)storage_size,(uintptr_t)storage_range};copy(l->storage_table,storage,sizeof(storage));l->storage.vtable=(uintptr_t)&l->storage_table[2];
 uintptr_t fs[]={0,l->api.fs_rtti,(uintptr_t)release_fs,(uintptr_t)release_fs,(uintptr_t)create_file,(uintptr_t)delete_file,(uintptr_t)mkdir_save,(uintptr_t)rmdir_save,(uintptr_t)rmtree_save,(uintptr_t)clean_save,(uintptr_t)rename_file,(uintptr_t)rename_dir,(uintptr_t)entry_type,(uintptr_t)free_size,(uintptr_t)total_size,(uintptr_t)open_file,(uintptr_t)open_dir,(uintptr_t)commit,(uintptr_t)commit_provisional,(uintptr_t)unsupported,(uintptr_t)unsupported,(uintptr_t)timestamp,(uintptr_t)unsupported,(uintptr_t)fs_attribute};copy(l->fs_table,fs,sizeof(fs));l->save_fs.vtable=(uintptr_t)&l->fs_table[2];
 uintptr_t file[]={0,l->api.file_rtti,(uintptr_t)release_file,(uintptr_t)release_file,(uintptr_t)file_read,(uintptr_t)file_write,(uintptr_t)file_flush,(uintptr_t)file_resize,(uintptr_t)file_size,(uintptr_t)file_range};copy(l->file_table,file,sizeof(file));
 uintptr_t dir[]={0,l->api.dir_rtti,(uintptr_t)release_dir,(uintptr_t)release_dir,(uintptr_t)dir_read,(uintptr_t)dir_count};copy(l->dir_table,dir,sizeof(dir));
}
static void *arena_alloc(uint64_t size){Nx2Launch *l=nx2_launch;if(size>UINT64_MAX-15)return 0;size=(size+15)&~UINT64_C(15);if(!size)size=16;uint64_t off=__atomic_load_n(&l->arena_used,__ATOMIC_RELAXED);for(;;){if(off>l->arena_capacity||size>l->arena_capacity-off)return 0;uint64_t next=off+size;if(__atomic_compare_exchange_n(&l->arena_used,&off,next,0,__ATOMIC_RELAXED,__ATOMIC_RELAXED))return (void *)(uintptr_t)(l->arena_base+off);}}
static void arena_free(void *p,uint64_t n){(void)p;(void)n;}
uint32_t nx2_query_rom(uint64_t *size){return nx2_launch->api.query_cache(size,&nx2_launch->storage);}
uint32_t nx2_query_rom_app(uint64_t *size,uint64_t app){(void)app;return nx2_query_rom(size);}
uint32_t nx2_query_rom_index(uint64_t *size,int index){(void)index;return nx2_query_rom(size);}
uint32_t nx2_mount_rom(const char *name,void *cache,uint64_t bytes){void *owned=&nx2_launch->storage;uint32_t rc=nx2_launch->api.mount_rom(name,&owned,cache,bytes);nx2_rpc(RPC_ROM_MOUNT,rc,bytes,0,0,0,name,0,0);return rc;}
uint32_t nx2_mount_rom_default(const char *name){uint64_t size=0;uint32_t rc=nx2_query_rom(&size);if(rc)return rc;void *cache=arena_alloc(size);if(!cache)return NX2_LIMIT;return nx2_mount_rom(name,cache,size);}
uint32_t nx2_mount_rom_app(const char *name,uint64_t app,void *p,uint64_t n){(void)app;return nx2_mount_rom(name,p,n);}
uint32_t nx2_mount_rom_index(const char *name,int index,void *p,uint64_t n){(void)index;return nx2_mount_rom(name,p,n);}
/* Each game-selected user has an immutable, distinct mounted filesystem.
 * Preserve the original shared data for one persistently bound UID. */
static uint32_t mount_save_uid(const char *name,Nx2UserId uid){
 if(!nx2_profile_has_uid(uid))return nx2_mount_save(name);
 uint64_t primary=0;
 uint32_t rc=nx2_rpc(RPC_SAVE_PROFILE_BIND,uid.lo,uid.hi,0,0,0,0,0,&primary);
 if(rc)return rc;
 Nx2Object *fs=object((uintptr_t)&nx2_launch->fs_table[2],uid.lo,
               NX2_PROFILE_FS_MODE|(primary?NX2_PROFILE_PRIMARY_FLAG:0));
 if(!fs)return NX2_LIMIT;
 fs->profile_high=uid.hi;
 void *owned=fs;rc=nx2_launch->api.register_fs(name,&owned);
 if(rc)__atomic_store_n(&fs->active,0,__ATOMIC_RELEASE);
 /* Preserve the original save mount's late-startup trace and crash marker
  * even when this mount is scoped to an individual guest profile. */
 nx2_rpc(RPC_LOG,rc,primary,0,0,0,"SAVE_PROFILE_MOUNT",name,0);
 nx2_rpc(RPC_SAVE_MOUNT,rc,0,0,0,0,name,0,0);
 return rc;
}
uint32_t nx2_mount_save_user_ref(const char *s,const Nx2UserId *u){
 return mount_save_uid(s,u?*u:(Nx2UserId){0,0});
}
uint32_t nx2_mount_save_user_value(const char *s,Nx2UserId u){
 return mount_save_uid(s,u);
}
uint32_t nx2_mount_save_app_user_ref(const char*s,uint64_t app,const Nx2UserId*u){
 (void)app;return nx2_mount_save_user_ref(s,u);
}
uint32_t nx2_mount_save_app_user_value(const char*s,uint64_t app,Nx2UserId u){
 (void)app;return mount_save_uid(s,u);
}
uint32_t nx2_mount_save_detail_user(const char*s,uint32_t space,uint64_t app,
                          Nx2UserId u,uint32_t type,uint32_t a,uint32_t b){
 (void)space;(void)app;(void)type;(void)a;(void)b;
 return mount_save_uid(s,u);
}
uint32_t __attribute__((noinline)) nx2_mount_save(const char *name){
 Nx2Launch *l=nx2_launch;
 __atomic_store_n(&l->save_gate_caller,
                  (uint64_t)(uintptr_t)__builtin_return_address(0),__ATOMIC_RELEASE);
 __atomic_store_n(&l->save_gate_phase,1,__ATOMIC_RELEASE);
 void *owned=&nx2_launch->save_fs;
 nx2_rpc(RPC_LOG,0,0,0,0,0,"SAVE_MOUNT_REGISTER_BEGIN",name,0);
 uint32_t rc=nx2_launch->api.register_fs(name,&owned);
 __atomic_store_n(&l->save_gate_result,rc,__ATOMIC_RELEASE);
 __atomic_store_n(&l->save_gate_phase,2,__ATOMIC_RELEASE);
 nx2_rpc(RPC_LOG,rc,0,0,0,0,"SAVE_MOUNT_REGISTER_RETURN",name,0);
 uint32_t reported=nx2_rpc(RPC_SAVE_MOUNT,rc,0,0,0,0,name,0,0);
 __atomic_store_n(&l->save_gate_phase,3,__ATOMIC_RELEASE);
 nx2_rpc(RPC_LOG,rc,reported,0,0,0,"SAVE_MOUNT_GATE_RETURN",name,0);
 __atomic_store_n(&l->save_gate_phase,4,__ATOMIC_RELEASE);
 return rc;
}
/* Back cache storage with a real, private SD subtree. Never claim
 * creation if the host rejected the requested capacity or directory. */
uint32_t nx2_create_cache_storage(int index,int64_t size,int64_t journal){
 if(index<0||index>65535||size<0||journal<0||
    (uint64_t)size>INT64_MAX-(uint64_t)journal)return NX2_INVALID;
 uint32_t rc=nx2_rpc(RPC_CACHE_CREATE,(uint32_t)index,
                     (uint64_t)size,(uint64_t)journal,0,0,0,0,0);
 nx2_rpc(RPC_LOG,(uint32_t)index,(uint64_t)size,
         (uint64_t)journal,rc,0,"VIRTUAL_CACHE_STORAGE_CREATE",0,0);
 return rc;
}
/* Shared by SDK MountCacheStorage and direct FSP cache-open commands.
 * Horizon normally provisions a title's cache during installation;
 * since the guest has no install phase, provision on explicit first mount.
 * RPC_CACHE_PROVISION runs atomically on the serialized host storage worker
 * and preserves all existing cache contents and quota markers. */
uint32_t nx2_ensure_guest_cache_storage(int index){
 if(index<0||index>65535)return NX2_INVALID;
 return nx2_rpc(RPC_CACHE_PROVISION,(uint32_t)index,
                NX2_CACHE_INSTALL_DATA_BYTES,
                NX2_CACHE_INSTALL_JOURNAL_BYTES,0,0,0,0,0);
}
uint32_t nx2_mount_cache_storage_index(const char *name,int index){
 if(!name||index<0||index>65535)return NX2_INVALID;
 uint32_t rc=nx2_ensure_guest_cache_storage(index);
 if(rc)return rc;
 Nx2Object *fs=object((uintptr_t)&nx2_launch->fs_table[2],0,(uint64_t)index+1);
 if(!fs)return NX2_LIMIT;
 void *owned=fs;
 rc=nx2_launch->api.register_fs(name,&owned);
 if(rc)__atomic_store_n(&fs->active,0,__ATOMIC_RELEASE);
 nx2_rpc(RPC_LOG,(uint32_t)index,rc,0,0,0,
         "VIRTUAL_CACHE_STORAGE_MOUNT",name,0);
 return rc;
}
uint32_t nx2_mount_cache_storage(const char *name){
 return nx2_mount_cache_storage_index(name,0);
}
uint32_t nx2_mount_cache_storage_app(const char *name,uint64_t app){
 (void)app;return nx2_mount_cache_storage_index(name,0);
}
uint32_t nx2_mount_cache_storage_app_index(const char *name,uint64_t app,int index){
 (void)app;return nx2_mount_cache_storage_index(name,index);
}
/* Save archive existence is distinct from the host-created directory.
 * The existing private SD backend stores one metadata marker per selected
 * title. The marker is outside the guest-visible save subtree. */
uint32_t nx2_provision_save(void){
 return nx2_rpc(RPC_SAVE_ARCHIVE_CREATE,0,0,0,0,0,0,0,0);
}
uint32_t nx2_save_exists(void){
 uint64_t exists=0;
 uint32_t rc=nx2_rpc(RPC_SAVE_ARCHIVE_EXISTS,0,0,0,0,0,0,0,&exists);
 return !rc && exists?1U:0U;
}
uint32_t nx2_save_exists_user_ref(const Nx2UserId *u){
 if(!u||!nx2_profile_has_uid(*u))return nx2_save_exists();
 uint64_t exists=0;
 uint32_t rc=nx2_rpc(RPC_SAVE_PROFILE_EXISTS,u->lo,u->hi,0,0,0,0,0,&exists);
 return !rc&&exists?1U:0U;
}
uint32_t nx2_provision_save_user_ref(const Nx2UserId *u){
 if(!u||!nx2_profile_has_uid(*u))return nx2_provision_save();
 return nx2_rpc(RPC_SAVE_PROFILE_CREATE,u->lo,u->hi,0,0,0,0,0,0);
}
uint32_t nx2_provision_save_user_value(uint64_t app,Nx2UserId u){
 (void)app;return nx2_provision_save_user_ref(&u);
}
uint32_t nx2_ensure_save_user(uint64_t *required,uint64_t app,
                             const void *control,const Nx2UserId *u){
 (void)app;(void)control;if(required)*required=0;
 return nx2_provision_save_user_ref(u);
}
uint32_t nx2_ensure_save(uint64_t *required){
 if(required)*required=0;
 return nx2_provision_save();
}
uint32_t nx2_save_ok(void){return 0;}
void nx2_main_gate(void){__atomic_store_n(&nx2_launch->main_entered,1,__ATOMIC_RELEASE);nx2_rpc(RPC_MAIN_ENTERED,0,0,0,0,0,0,0,0);nx2_launch->api.original_main();__atomic_store_n(&nx2_launch->guest_main_returned,1,__ATOMIC_RELEASE);nx2_rpc(RPC_ABORT,0,0,0,0,0,"nnMain returned",0,0);for(;;)wait();}
static uint32_t preflight_fs_policy(uint32_t result){(void)result;return 2;}
void nx2_preflight_worker(Nx2Launch *l){
 volatile unsigned char *tlr;uint64_t tp;unsigned char saved[128];__asm__ volatile("mrs %0,tpidrro_el0":"=r"(tlr));__asm__ volatile("mrs %0,tpidr_el0":"=r"(tp));
 if(*(volatile uint16_t *)(tlr+0x100)){__atomic_store_n(&l->abort_requested,1,__ATOMIC_RELEASE);return;}
 for(unsigned i=0;i<128;i++){saved[i]=tlr[0x180+i];tlr[0x180+i]=0;}
 __asm__ volatile("msr tpidr_el0,xzr\n\tisb" ::: "memory");
 __atomic_store_n(&l->stage,1,__ATOMIC_RELEASE);l->api.os_init(l->worker_handle,0);
 l->api.set_fs_result_handler(preflight_fs_policy);l->api.set_fs_allocator(arena_alloc,arena_free);l->api.fs_init();
 __atomic_store_n(&l->stage,2,__ATOMIC_RELEASE);uint32_t rc=nx2_query_rom(&l->cache_size);
 void *cache=rc?0:arena_alloc(l->cache_size);if(!rc&&!cache)rc=NX2_LIMIT;if(!rc)rc=nx2_mount_rom("rom",cache,l->cache_size);l->preflight_result=rc;
 if(!rc){
  static const char *paths[]={"rom:/UECommandLine.txt","rom:/PJ045/Content/Paks/PJ045-Sage.utoc","rom:/PJ045/Content/Paks/PJ045-Sage.ucas"};
  static const uint64_t offsets[]={0,0,UINT64_C(0x80010000)};static const uint32_t lengths[]={40,4096,4096};uint64_t option=0;
  for(unsigned i=0;i<3;i++){uint64_t file=0;rc=l->api.open_file(&file,paths[i],1);if(!rc){rc=l->api.read_file(&l->sample_reads[i],file,offsets[i],l->samples[i],lengths[i],&option);l->api.close_file(file);}l->sample_results[i]=rc;}
  l->api.unmount("rom");
 }
 __atomic_store_n(&l->stage,3,__ATOMIC_RELEASE);rc=nx2_mount_save("save");
 if(!rc){
  const char *p="save:/.__nx2_probe.bin";uint64_t file=0,value=UINT64_C(0x1122334455667788),option=1,read=0;
  l->api.delete_file(p);rc=l->api.create_file(p,8);
  if(!rc)rc=l->api.open_file(&file,p,3);
  if(!rc){rc=l->api.write_file(file,0,&value,8,&option);l->api.close_file(file);file=0;}
  if(!rc)rc=l->api.open_file(&file,p,1);
  if(!rc){option=0;rc=l->api.read_file(&read,file,0,&l->save_cookie,8,&option);l->api.close_file(file);if(!rc&&read!=8)rc=NX2_INVALID;}
  l->api.delete_file(p);l->api.unmount("save");
 }l->save_result=rc;
 if(*(volatile uint16_t *)(tlr+0x100))__atomic_store_n(&l->abort_requested,1,__ATOMIC_RELEASE);
 for(unsigned i=0;i<128;i++){tlr[0x180+i]=saved[i];}
 __asm__ volatile("msr tpidr_el0,%0\n\tisb" ::"r"(tp):"memory");
 uint64_t restored_tp;__asm__ volatile("mrs %0,tpidr_el0":"=r"(restored_tp));if(restored_tp!=tp)__atomic_store_n(&l->abort_requested,1,__ATOMIC_RELEASE);for(unsigned i=0;i<128;i++)if(tlr[0x180+i]!=saved[i])__atomic_store_n(&l->abort_requested,1,__ATOMIC_RELEASE);
 __atomic_store_n(&l->stage,4,__ATOMIC_RELEASE);__atomic_store_n(&l->finished,1,__ATOMIC_RELEASE);
}
void nx2_game_worker(Nx2Launch *l){volatile unsigned char *tlr;__asm__ volatile("mrs %0,tpidrro_el0":"=r"(tlr));if(*(volatile uint16_t *)(tlr+0x100)){__atomic_store_n(&l->abort_requested,1,__ATOMIC_RELEASE);for(;;)wait();}for(unsigned i=0;i<128;i++)tlr[0x180+i]=0;__asm__ volatile("msr tpidr_el0,xzr\n\tisb" ::: "memory");__atomic_store_n(&l->stage,10,__ATOMIC_RELEASE);l->api.rtld(0,l->worker_handle);nx2_rpc(RPC_ABORT,0,0,0,0,0,"Original rtld returned unexpectedly",0,0);for(;;)wait();}

void nx2_nninit_gate(void){nx2_rpc(RPC_LOG,0,0,0,0,0,"NNINIT_STARTUP_ENTERED",0,0);nx2_launch->api.original_nninit();nx2_rpc(RPC_LOG,0,0,0,0,0,"NNINIT_STARTUP_RETURNED",0,0);}
void nx2_sdk_init_gate(void){nx2_rpc(RPC_LOG,0,0,0,0,0,"SDK_MODULE_INIT_ENTERED",0,0);nx2_launch->api.original_sdk_init();nx2_rpc(RPC_LOG,0,0,0,0,0,"SDK_MODULE_INIT_RETURNED",0,0);}
uint32_t nx2_false(void){return 0;}
static int equal(const char *a,const char *b){if(!a||!b)return 0;while(*a&&*a==*b){a++;b++;}return *a==*b;}
/* Exact NVNqueuePresentTexture ABI. The old generic 8x u64
 * forwarding function made a void three-argument GPU call through an
 * incompatible function pointer type. Forward only real arguments and do
 * not invent a return value; still preserve frame counters. */
static void present_gate(void *queue,void *window,int texture_index){
 __atomic_fetch_add(&nx2_launch->nvn_present_entered,1,__ATOMIC_RELAXED);
 __atomic_load_n(&nx2_launch->api.original_present,__ATOMIC_ACQUIRE)
     (queue,window,texture_index);
 __atomic_fetch_add(&nx2_launch->nvn_present_returned,1,__ATOMIC_RELAXED);
 __atomic_fetch_add(&nx2_launch->frames,1,__ATOMIC_RELAXED);
}
/* Observe native initialization without altering the real NVN implementation,
 * arguments, or returned success/failure result. No RPC or synchronous I/O. */
static uint32_t device_init_gate(void *obj,const void *builder){
 __atomic_fetch_add(&nx2_launch->nvn_device_entered,1,__ATOMIC_RELAXED);
 uint32_t rc=nx2_launch->api.original_device_initialize(obj,builder);
 __atomic_store_n(&nx2_launch->nvn_device_result,rc,__ATOMIC_RELEASE);
 __atomic_fetch_add(&nx2_launch->nvn_device_returned,1,__ATOMIC_RELEASE);
 return rc;
}
static uint32_t queue_init_gate(void *obj,const void *builder){
 __atomic_fetch_add(&nx2_launch->nvn_queue_entered,1,__ATOMIC_RELAXED);
 uint32_t rc=nx2_launch->api.original_queue_initialize(obj,builder);
 __atomic_store_n(&nx2_launch->nvn_queue_result,rc,__ATOMIC_RELEASE);
 __atomic_fetch_add(&nx2_launch->nvn_queue_returned,1,__ATOMIC_RELEASE);
 return rc;
}
static uint32_t window_init_gate(void *obj,const void *builder){
 __atomic_fetch_add(&nx2_launch->nvn_window_entered,1,__ATOMIC_RELAXED);
 uint32_t rc=nx2_launch->api.original_window_initialize(obj,builder);
 __atomic_store_n(&nx2_launch->nvn_window_result,rc,__ATOMIC_RELEASE);
 __atomic_fetch_add(&nx2_launch->nvn_window_returned,1,__ATOMIC_RELEASE);
 return rc;
}
/* Post-initialization NVN instrumentation. These wrappers always forward to
 * the genuine driver; no synchronous logging, allocation, or RPC is performed.
 * The exact integer/pointer signatures preserve the guest ABI. */
static void queue_submit_gate(void *queue,int count,const uint64_t *handles){
 __atomic_fetch_add(&nx2_launch->nvn_stage_entered[0],1,__ATOMIC_RELAXED);
 nx2_launch->api.original_queue_submit(queue,count,handles);
 __atomic_fetch_add(&nx2_launch->nvn_stage_returned[0],1,__ATOMIC_RELEASE);
}
static void queue_flush_gate(void *queue){
 __atomic_fetch_add(&nx2_launch->nvn_stage_entered[1],1,__ATOMIC_RELAXED);
 nx2_launch->api.original_queue_flush(queue);
 __atomic_fetch_add(&nx2_launch->nvn_stage_returned[1],1,__ATOMIC_RELEASE);
}
static void queue_finish_gate(void *queue){
 __atomic_fetch_add(&nx2_launch->nvn_stage_entered[2],1,__ATOMIC_RELAXED);
 nx2_launch->api.original_queue_finish(queue);
 __atomic_fetch_add(&nx2_launch->nvn_stage_returned[2],1,__ATOMIC_RELEASE);
}
static uint32_t window_acquire_gate(void *window,void *sync,int *index){
 __atomic_fetch_add(&nx2_launch->nvn_stage_entered[3],1,__ATOMIC_RELAXED);
 uint32_t rc=nx2_launch->api.original_window_acquire(window,sync,index);
 __atomic_store_n(&nx2_launch->nvn_stage_result[3],rc,__ATOMIC_RELEASE);
 __atomic_fetch_add(&nx2_launch->nvn_stage_returned[3],1,__ATOMIC_RELEASE);
 return rc;
}
static uint32_t queue_acquire_gate(void *queue,void *window,int *index){
 __atomic_fetch_add(&nx2_launch->nvn_stage_entered[4],1,__ATOMIC_RELAXED);
 uint32_t rc=nx2_launch->api.original_queue_acquire(queue,window,index);
 __atomic_store_n(&nx2_launch->nvn_stage_result[4],rc,__ATOMIC_RELEASE);
 __atomic_fetch_add(&nx2_launch->nvn_stage_returned[4],1,__ATOMIC_RELEASE);
 return rc;
}
static uintptr_t getproc_gate(void *device,const char *name){
 __atomic_fetch_add(&nx2_launch->nvn_getproc_calls,1,__ATOMIC_RELAXED);
 uintptr_t result=nx2_launch->api.original_getproc(device,name);
 /* Mode 1 (A) passes native NVN pointers except observed PresentTexture.
  * Mode 2 (L+A) passes all native pointers; ZR+A instruments all stages.
  * Keep this check safe even for a retained GetProcAddress wrapper. */
 if(nx2_launch->nvn_driver_passthrough==2 ||
    (nx2_launch->nvn_driver_passthrough==1 &&
     !equal(name,"nvnQueuePresentTexture"))) return result;
 if(result&&equal(name,"nvnQueueSubmitCommands")){
   __atomic_store_n(&nx2_launch->api.original_queue_submit,(void *)result,__ATOMIC_RELEASE);
   return (uintptr_t)queue_submit_gate;
 }
 if(result&&equal(name,"nvnQueueFlush")){
   __atomic_store_n(&nx2_launch->api.original_queue_flush,(void *)result,__ATOMIC_RELEASE);
   return (uintptr_t)queue_flush_gate;
 }
 if(result&&equal(name,"nvnQueueFinish")){
   __atomic_store_n(&nx2_launch->api.original_queue_finish,(void *)result,__ATOMIC_RELEASE);
   return (uintptr_t)queue_finish_gate;
 }
 if(result&&equal(name,"nvnWindowAcquireTexture")){
   __atomic_store_n(&nx2_launch->api.original_window_acquire,(void *)result,__ATOMIC_RELEASE);
   return (uintptr_t)window_acquire_gate;
 }
 if(result&&equal(name,"nvnQueueAcquireTexture")){
   __atomic_store_n(&nx2_launch->api.original_queue_acquire,(void *)result,__ATOMIC_RELEASE);
   return (uintptr_t)queue_acquire_gate;
 }
 if(result&&equal(name,"nvnDeviceInitialize")){
   __atomic_store_n(&nx2_launch->api.original_device_initialize,(void *)result,__ATOMIC_RELEASE);
   return (uintptr_t)device_init_gate;
 }
 if(result&&equal(name,"nvnQueueInitialize")){
   __atomic_store_n(&nx2_launch->api.original_queue_initialize,(void *)result,__ATOMIC_RELEASE);
   return (uintptr_t)queue_init_gate;
 }
 if(result&&equal(name,"nvnWindowInitialize")){
   __atomic_store_n(&nx2_launch->api.original_window_initialize,(void *)result,__ATOMIC_RELEASE);
   return (uintptr_t)window_init_gate;
 }
 if(result&&equal(name,"nvnQueuePresentTexture")){
   __atomic_fetch_add(&nx2_launch->nvn_present_lookups,1,__ATOMIC_RELAXED);
   __atomic_store_n(&nx2_launch->api.original_present,(void *)result,__ATOMIC_RELEASE);
   return (uintptr_t)present_gate;
 }
 return result;
}
uintptr_t nx2_bootstrap_gate(const char *name){
 __atomic_fetch_add(&nx2_launch->nvn_bootstrap_calls,1,__ATOMIC_RELAXED);
 uintptr_t result=nx2_launch->api.original_bootstrap(name);
 /* L+A passes through the native GetProcAddress and all subsequent NVN
  * driver pointers. A retains lightweight PresentTexture observation;
  * ZR+A retains the fully instrumented diagnostic path below. */
 if(nx2_launch->nvn_driver_passthrough==2) return result;
 if(result&&equal(name,"nvnDeviceGetProcAddress")){
   __atomic_store_n(&nx2_launch->api.original_getproc,(void *)result,__ATOMIC_RELEASE);
   return (uintptr_t)getproc_gate;
 }
 return result;
}

/* All guest titles execute in the host process. Initializing the host
 * ldr:ro session with guest assumptions can abort before native graphics
 * starts. Apply this process-context adapter uniformly where the guest
 * SDK exports nn::ro::Initialize; actual dynamic NRO loads still need a
 * correctly isolated, process-aware loader implementation. */
uint32_t nx2_ro_initialize_compat(void){
 nx2_rpc(RPC_LOG,0,0,0,0,0,"RO_INIT_COMPAT","host-owned ldr:ro initialization skipped; dynamic modules not yet virtualized",0);
 return 0;
}
uint32_t nx2_save_sizes(int64_t *size,int64_t *journal){if(size)*size=0x20000000;if(journal)*journal=0;return 0;}
/* Query real free bytes on this guest's isolated SD-backed archive.
 * The previous 512 MiB success was fictitious regardless of SD capacity. */
uint32_t nx2_save_available(int64_t *size){
 if(!size)return NX2_INVALID;
 uint64_t actual=0;
 uint32_t rc=nx2_rpc(RPC_SAVE_FREE,0,0,0,0,0,"",0,&actual);
 if(rc)return rc;
 if(actual>INT64_MAX)return NX2_INVALID;
 *size=(int64_t)actual;
 return 0;
}
uint32_t nx2_save_journal(int64_t *size){if(size)*size=0;return 0;}
uint32_t nx2_save_flags(uint32_t *flags){if(flags)*flags=0;return 0;}

uint32_t nx2_true(void){return 1;}

void nx2_os_init_gate(uint32_t handle,const void *args){nx2_rpc(RPC_LOG,0,0,0,0,0,"SDK_OS_INIT_ENTERED",0,0);nx2_launch->api.original_os_init(handle,args);nx2_rpc(RPC_LOG,0,0,0,0,0,"SDK_OS_INIT_RETURNED",0,0);}
