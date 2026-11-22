/* SPDX-License-Identifier: GPL-2.0-only */
/* Interface-based guest FSP domain bridge.
 * All files remain under the currently selected title's private SD tree;
 * never forward guest save opens to the host title's fsp-srv identity.
 * Native IFileSystem/IFile/IDirectory commands share the existing RPC backend.
 */
#include "guest_fsp_vfs.h"
#include "launch.h"
#include "cache_catalog.h"
#include "cache_route.h"
#include <switch.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#define VFS_MAX_OBJECTS 512
#define VFS_FS 1
#define VFS_FILE 2
#define VFS_DIR 3
#define VFS_CACHE_READER 4
typedef struct {
 uint32_t session,object_id,kind,read_only;
 uint64_t token;
 uint32_t busy;
 char root[128];
} VfsObject;
static VfsObject vfs_objects[VFS_MAX_OBJECTS];
static volatile uint32_t vfs_lock;
static uint32_t vfs_next_id=0xe0000000U;
static void lock_vfs(void){
 while(__atomic_exchange_n(&vfs_lock,1,__ATOMIC_ACQUIRE))
  __asm__ volatile("yield");
}
static void unlock_vfs(void){__atomic_store_n(&vfs_lock,0,__ATOMIC_RELEASE);}
static VfsObject find_object(uint32_t session,uint32_t id){
 (void)session;
 VfsObject v={0};
 lock_vfs();
 for(unsigned i=0;i<VFS_MAX_OBJECTS;i++)
  if(vfs_objects[i].kind && vfs_objects[i].object_id==id){
   /* Cloned fsp-srv domain sessions reuse the same object IDs. Our
    * generated IDs are process-global and never overlap kernel IDs. */
   v=vfs_objects[i];break;
  }
 unlock_vfs();
 return v;
}
static uint32_t allocate_object(uint32_t session,uint32_t kind,
                                uint32_t read_only,uint64_t token,
                                const char *root){
 uint32_t id=0;
 lock_vfs();
 for(unsigned i=0;i<VFS_MAX_OBJECTS;i++)if(!vfs_objects[i].kind){
  id=++vfs_next_id;
  if(!id||id<0xe0000000U)id=vfs_next_id=0xe0000001U;
  vfs_objects[i].session=session;
  vfs_objects[i].object_id=id;
  vfs_objects[i].kind=kind;
  vfs_objects[i].read_only=read_only;
  vfs_objects[i].token=token;
  vfs_objects[i].busy=0;
  if(root){
   unsigned j=0;
   for(;j<sizeof(vfs_objects[i].root)-1&&root[j];j++)
    vfs_objects[i].root[j]=root[j];
   vfs_objects[i].root[j]=0;
  }else vfs_objects[i].root[0]=0;
  break;
 }
 unlock_vfs();
 return id;
}
static int forget_object(uint32_t session,uint32_t id,VfsObject *old){
 (void)session;
 int found=0;
 lock_vfs();
 for(unsigned i=0;i<VFS_MAX_OBJECTS;i++)
  if(vfs_objects[i].kind && vfs_objects[i].object_id==id){
   if(old)*old=vfs_objects[i];
   vfs_objects[i]=(VfsObject){0};
   found=1;break;
  }
 unlock_vfs();
 return found;
}
/* Serialize each virtual cache reader cursor across guest threads. */
static int cache_reader_begin(uint32_t id,uint64_t *cursor){
 if(!id||!cursor)return 0;
 for(;;){
  int found=0;
  lock_vfs();
  for(unsigned i=0;i<VFS_MAX_OBJECTS;i++)
   if(vfs_objects[i].kind==VFS_CACHE_READER &&
      vfs_objects[i].object_id==id){
    found=1;
    if(!vfs_objects[i].busy){
     vfs_objects[i].busy=1;
     *cursor=vfs_objects[i].token;
     unlock_vfs();
     return 1;
    }
    break;
   }
  unlock_vfs();
  if(!found)return 0;
  __asm__ volatile("yield");
 }
}
static void cache_reader_end(uint32_t id,uint64_t count,int valid){
 lock_vfs();
 for(unsigned i=0;i<VFS_MAX_OBJECTS;i++)
  if(vfs_objects[i].kind==VFS_CACHE_READER &&
     vfs_objects[i].object_id==id && vfs_objects[i].busy){
   if(valid && count<=UINT64_MAX-vfs_objects[i].token)
    vfs_objects[i].token+=count;
   vfs_objects[i].busy=0;
   break;
  }
 unlock_vfs();
}
static uint32_t read32(const void *p){
 const unsigned char *b=p;
 return (uint32_t)b[0]|((uint32_t)b[1]<<8)|((uint32_t)b[2]<<16)|
        ((uint32_t)b[3]<<24);
}
static uint64_t read64(const void *p){
 return (uint64_t)read32(p)|((uint64_t)read32((const char*)p+4)<<32);
}
static void put32(void *p,uint32_t v){
 unsigned char *b=p;for(unsigned i=0;i<4;i++)b[i]=(unsigned char)(v>>(8*i));
}
/* Guest buffers may legitimately span adjacent virtual-memory regions.
 * Validate every covered region with the real kernel instead of insisting
 * one svcQueryMemory descriptor cover the entire transfer. */
static int valid_memory(const void *ptr,uint64_t bytes,int writable){
 uintptr_t at=(uintptr_t)ptr;
 if(!ptr||!bytes||bytes>0x4000000U||at>UINTPTR_MAX-bytes)return 0;
 const uintptr_t end=at+(uintptr_t)bytes;
 const uint32_t permission=writable?2U:1U;
 while(at<end){
  MemoryInfo mi;u32 page=0;
  Result rc=svcQueryMemory(&mi,&page,at);
  if(R_FAILED(rc)||mi.addr>at||mi.size>UINTPTR_MAX-mi.addr||
     mi.addr+mi.size<=at||(mi.perm&permission)!=permission)
   return 0;
  uintptr_t next=mi.addr+mi.size;
  at=next<end?next:end;
 }
 return 1;
}
/* Raw reply never passes a fabricated handle to the kernel. The virtual
 * domain object ID exists only in this guest-side dispatcher. */
static int write_reply(void *buffer,uint64_t size,int domain,
                       uint32_t result,uint32_t object_id,
                       const void *out,uint32_t out_size,uint32_t token){
 if(out_size>64)return 0;
 uint32_t count=domain&&object_id?1U:0U;
 uint32_t words=(16+(domain?16:0)+16+out_size+count*4+3)/4;
 uint32_t bytes=8+words*4;
 if(bytes>size)return 0;
 memset(buffer,0,bytes);
 put32(buffer,0);put32((char*)buffer+4,words);
 unsigned char *at=(unsigned char*)buffer+16;
 if(domain){put32(at,count);at+=16;}
 put32(at,0x4f434653U);put32(at+4,0);
 put32(at+8,result);put32(at+12,token);
 if(out_size&&out)memcpy(at+16,out,out_size);
 if(count)put32(at+16+out_size,object_id);
 return 1;
}
/* An nn::sf auto-buffer is encoded as a PAIR of descriptors: one X-static
 * and one A-buffer, with the unused side explicitly set to NULL/zero.
 * They are the same logical argument index, not two consecutive paths.
 * This also handles pure static and pure transfer-buffer calls. */
/* The IPC policy has already validated the SFCI/domain header. For the
 * first save operation, avoid reinterpreting a full libnx HipcRequest with
 * pointers derived from untrusted descriptor counts. Work from the bounded
 * HIPC data section instead. Child file/directory methods still parse their
 * descriptors separately, after these bounds have been verified. */
static int native_domain_payload(const void *buffer,uint64_t size,
                                 Nx2IpcDecision d,
                                 const unsigned char **out_head,
                                 const unsigned char **out_in,
                                 uint64_t *out_bytes,
                                 uint32_t *out_token){
 const unsigned char *p=buffer;
 if(!p||size<48||!d.valid||!d.domain||d.control||d.close_kind)
  return 0;
 uint32_t a=read32(p),b=read32(p+4);
 uint64_t off=8;
 if(b&0x80000000U){
  if(size<12)return 0;
  uint32_t extra=read32(p+8);
  off=12+((extra&1U)?8U:0U)+
      4U*(((extra>>1)&15U)+((extra>>5)&15U));
 }
 off+=8U*((a>>16)&15U)+
      12U*(((a>>20)&15U)+((a>>24)&15U)+((a>>28)&15U));
 uint64_t n=4U*(b&1023U);
 if(off>size||n>size-off)return 0;
 uint64_t end=off+n;
 uint64_t start=(off+15U)&~UINT64_C(15);
 if(start>end||end-start<32U)return 0;
 if(p[start]!=1||read32(p+start+4)!=d.object_id||
    read32(p+start+16)!=0x49434653U||
    read32(p+start+24)!=d.command)return 0;
 *out_head=p+start+16;
 *out_in=p+start+32;
 *out_bytes=end-(start+32);
 *out_token=read32(p+start+12);
 return 1;
}
/* The validated decoder above is also used for unsupported methods.
 * Never call hipcParseRequest a second time against a malformed TLS image.
 * Unsupported operations receive an actual guest error, not fake success. */
int nx2_guest_fsp_unsupported(void *buffer,uint64_t size,Nx2IpcDecision d){
 if(!buffer||size<48||!d.valid||d.control||d.close_kind)return 0;
 uint32_t token=0;
 if(d.domain){
  const unsigned char *header=0,*payload=0;
  uint64_t avail=0;
  if(!native_domain_payload(buffer,size,d,&header,&payload,&avail,&token))
   return write_reply(buffer,size,1,NX2_INVALID,0,0,0,0);
 }else{
  /* A non-domain error needs only a bounded CMIF request header.
   * Its HIPC control and descriptor placement was already checked by
   * nx2_ipc_decide. Avoid a raw libnx pointer-based reparse. */
  const unsigned char *p=buffer;
  uint32_t a=read32(p),h=read32(p+4);
  uint64_t off=8;
  if(h&0x80000000U){
   uint32_t extra=read32(p+8);
   off=12+((extra&1U)?8U:0U)+
       4U*(((extra>>1)&15U)+((extra>>5)&15U));
  }
  off+=8U*((a>>16)&15U)+12U*(((a>>20)&15U)+
        ((a>>24)&15U)+((a>>28)&15U));
  uint64_t count=4U*(h&1023U);
  uint64_t aligned=(off+15U)&~UINT64_C(15);
  if(off>size||count>size-off||aligned>off+count||
     off+count-aligned<16||read32(p+aligned)!=0x49434653U)
    return write_reply(buffer,size,0,NX2_INVALID,0,0,0,0);
  token=read32(p+aligned+12);
 }
 return write_reply(buffer,size,d.domain,NX2_FS_UNSUPPORTED,0,0,0,token);
}
static int path_argument(const HipcParsedRequest *req,unsigned index,
                         char out[NX2_PATH_MAX]){
 const void *ptr=0;uint64_t len=0;
 if(index<req->meta.num_send_statics){
  const HipcStaticDescriptor *v=&req->data.send_statics[index];
  const void *candidate=hipcGetStaticAddress(v);
  uint64_t n=hipcGetStaticSize(v);
  if(candidate&&n){ptr=candidate;len=n;}
 }
 if(!ptr&&index<req->meta.num_send_buffers){
  const HipcBufferDescriptor *v=&req->data.send_buffers[index];
  const void *candidate=hipcGetBufferAddress(v);
  uint64_t n=hipcGetBufferSize(v);
  if(candidate&&n){ptr=candidate;len=n;}
 }
 if(len<1||len>0x301||!valid_memory(ptr,len,0))return 0;
 const unsigned char *src=ptr;
 unsigned n=0;
 for(;n<len&&n<NX2_PATH_MAX-1;n++){
  if(!src[n])break;
  if(src[n]=='\\'||src[n]==':'||src[n]<32)return 0;
  out[n]=src[n];
 }
 if(n==len||n>=NX2_PATH_MAX-1)return 0;
 out[n]=0;return 1;
}
static int data_buffer(const HipcParsedRequest *req,int output,
                       void **dst,uint64_t *size){
 *dst=0;*size=0;
 if(output){
  if(req->meta.num_recv_buffers){
   const HipcBufferDescriptor *d=&req->data.recv_buffers[0];
   *dst=hipcGetBufferAddress(d);*size=hipcGetBufferSize(d);
  }
  /* cmifRequestOutAutoBuffer can use a C recv-list pointer rather than
   * a B buffer when its data fits in the server's pointer buffer. */
  if((!*dst||!*size) && req->meta.num_recv_statics &&
     req->meta.num_recv_statics!=HIPC_AUTO_RECV_STATIC &&
     req->data.recv_list){
   const HipcRecvListEntry *v=&req->data.recv_list[0];
   *dst=(void*)(uintptr_t)((uint64_t)v->address_low |
                          ((uint64_t)v->address_high<<32));
   *size=v->size;
  }
 }else{
  if(req->meta.num_send_buffers){
   const HipcBufferDescriptor *d=&req->data.send_buffers[0];
   *dst=hipcGetBufferAddress(d);*size=hipcGetBufferSize(d);
  }
  if((!*dst||!*size)&&req->meta.num_send_statics){
   const HipcStaticDescriptor *v=&req->data.send_statics[0];
   *dst=hipcGetStaticAddress(v);*size=hipcGetStaticSize(v);
  }
 }
 return *size && *size<=0x4000000U && valid_memory(*dst,*size,output);
}
static int scoped_path(const VfsObject *obj,const char *path,
                       char out[NX2_PATH_MAX]){
 if(!obj->root[0])return 0;
 while(*path=='/')path++;
 /* Use plain bounded copies, not formatted I/O from an IPC hook. */
 size_t nr=strlen(obj->root),np=strlen(path);
 if(nr>=NX2_PATH_MAX||np>NX2_PATH_MAX-nr-1)return 0;
 memcpy(out,obj->root,nr);
 if(np)out[nr++]='/';
 if(np>=NX2_PATH_MAX-nr)return 0;
 memcpy(out+nr,path,np);
 out[nr+np]=0;
 return 1;
}
static uint32_t close_child(const VfsObject *obj){
 if(obj->kind==VFS_FILE)
  return nx2_rpc(RPC_SAVE_CLOSE,obj->token,0,0,0,0,0,0,0);
 if(obj->kind==VFS_DIR)
  return nx2_rpc(RPC_DIR_CLOSE,obj->token,0,0,0,0,0,0,0);
 return 0;
}
static uint32_t do_fs(const VfsObject *obj,uint32_t cmd,
                      const unsigned char *in,uint64_t len,
                      const HipcParsedRequest *rq,
                      uint32_t session,uint32_t *new_id,
                      unsigned char out[64],uint32_t *out_size){
 char pa[NX2_PATH_MAX],pb[NX2_PATH_MAX];
 char a[NX2_PATH_MAX],b[NX2_PATH_MAX];
 int needs_path=cmd<=9||cmd==11||cmd==12||cmd==13||cmd==14;
 if(needs_path){
  if(!path_argument(rq,0,pa)||!scoped_path(obj,pa,a))
   return NX2_INVALID;
 }
 if(obj->read_only&&(cmd==0||cmd==1||cmd==2||cmd==3||
                     cmd==4||cmd==5||cmd==6||cmd==8||cmd==13)){
  if(cmd!=8)return NX2_FS_UNSUPPORTED;
  if(len<4||(read32(in)&2U))return NX2_FS_UNSUPPORTED;
 }
 if(cmd==5||cmd==6){
  if(!path_argument(rq,1,pb)||!scoped_path(obj,pb,b))
   return NX2_INVALID;
 }
 uint64_t answer=0;uint32_t rc=NX2_FS_UNSUPPORTED,op=0;
 /* Initialize the isolated guest root only when creating contents.
  * Reads and file opens retain genuine not-found behavior. */
 if((cmd==0||cmd==2)&&!obj->read_only &&
    strncmp(obj->root,"cache-storage/",14)!=0){
  /* Cache directories are created by RPC_CACHE_CREATE, not vsave-*. */
  uint32_t prepared=nx2_rpc(RPC_VFS_PREPARE_ROOT,0,0,0,0,0,
                            obj->root,0,0);
  if(prepared)return prepared;
 }
 switch(cmd){
  case 0:
   if(len<16)return NX2_INVALID;
   return nx2_rpc(RPC_SAVE_CREATE,read64(in+8),read32(in),0,0,0,a,0,0);
  case 1:op=RPC_SAVE_DELETE;break;
  case 2:op=RPC_SAVE_MKDIR;break;
  case 3:op=RPC_SAVE_RMDIR;break;
  case 4:op=RPC_SAVE_RMTREE;break;
  case 5:op=RPC_SAVE_RENAME_FILE;break;
  case 6:op=RPC_SAVE_RENAME_DIR;break;
  case 7:op=RPC_SAVE_TYPE;break;
  case 8:op=RPC_SAVE_OPEN;break;
  case 9:op=RPC_DIR_OPEN;break;
  case 10:
   if(obj->read_only)return NX2_FS_UNSUPPORTED;
   return nx2_rpc(RPC_COMMIT,0,0,0,0,0,0,0,0);
  case 11:op=RPC_SAVE_FREE;break;
  case 12:op=RPC_SAVE_TOTAL;break;
  case 13:op=RPC_SAVE_CLEAN;break;
  case 14:
   rc=nx2_rpc(RPC_SAVE_TIMESTAMP,0,0,0,0,out,a,0,0);
   if(!rc)*out_size=32;
   return rc;
  default:return NX2_FS_UNSUPPORTED;
 }
 if(cmd==8||cmd==9){
  if(len<4)return NX2_INVALID;
  uint32_t mode=read32(in);
  if(obj->read_only&&cmd==8&&(mode&2U))return NX2_FS_UNSUPPORTED;
  rc=nx2_rpc(op,mode,0,0,0,0,a,0,&answer);
  if(rc)return rc;
  *new_id=allocate_object(session,cmd==8?VFS_FILE:VFS_DIR,
                         obj->read_only,answer,obj->root);
  if(!*new_id){
   nx2_rpc(cmd==8?RPC_SAVE_CLOSE:RPC_DIR_CLOSE,
           answer,0,0,0,0,0,0,0);
   return NX2_LIMIT;
  }
  return 0;
 }
 rc=nx2_rpc(op,0,0,0,0,0,a,cmd==5||cmd==6?b:0,&answer);
 if(rc)return rc;
 if(cmd==7){put32(out,(uint32_t)answer);*out_size=4;}
 if(cmd==11||cmd==12){memcpy(out,&answer,8);*out_size=8;}
 return 0;
}
static uint32_t do_file(const VfsObject *obj,uint32_t cmd,
                        const unsigned char *in,uint64_t len,
                        const HipcParsedRequest *rq,
                        unsigned char out[64],uint32_t *out_size){
 uint64_t answer=0;
 switch(cmd){
  case 0:
  case 1:{
   if(len<24||((int64_t)read64(in+8))<0)return NX2_INVALID;
   if(cmd==1&&obj->read_only)return NX2_FS_UNSUPPORTED;
   uint64_t offset=read64(in+8),n=read64(in+16);
   /* Native IFile zero-byte reads/writes need no transfer descriptor.
    * Return an actual 0-byte read count without touching SD state. */
   if(!n){
    if(cmd==0){memset(out,0,8);*out_size=8;}
    return 0;
   }
   void *buf=0;uint64_t cap=0;
   if(n>0x4000000U||n>UINT64_MAX-offset||
      !data_buffer(rq,cmd==0,&buf,&cap)||n>cap)
    return NX2_INVALID;
   /* Complete large valid save IO in 1 MiB chunks, keeping the
    * single storage worker responsive to other guest operations. */
   uint64_t processed=0;
   while(processed<n){
    uint64_t amount=n-processed;
    if(amount>0x100000U)amount=0x100000U;
    uint64_t transferred=0;
    uint32_t flags=(cmd==1 && processed+amount==n)?read32(in):0;
    uint32_t rc=nx2_rpc(cmd==0?RPC_SAVE_READ:RPC_SAVE_WRITE,
                        obj->token,offset+processed,amount,flags,
                        (unsigned char*)buf+processed,0,0,&transferred);
    if(rc)return rc;
    if(cmd==0){
     if(transferred>amount)return NX2_INVALID;
     processed+=transferred;
     if(transferred<amount)break;
    }else processed+=amount;
   }
   if(cmd==0){memcpy(out,&processed,8);*out_size=8;}
   return 0;
  }
  case 2:return nx2_rpc(RPC_SAVE_FLUSH,obj->token,0,0,0,0,0,0,0);
  case 3:
   if(obj->read_only||len<8||(int64_t)read64(in)<0)
    return NX2_FS_UNSUPPORTED;
   return nx2_rpc(RPC_SAVE_RESIZE,obj->token,read64(in),0,0,0,0,0,0);
  case 4:{
   uint32_t rc=nx2_rpc(RPC_SAVE_SIZE,obj->token,0,0,0,0,0,0,&answer);
   if(!rc){memcpy(out,&answer,8);*out_size=8;}
   return rc;
  }
  case 5:{
   /* Native IFile::OperateRange: u32 operation (padding 4),
    * s64 offset, s64 length -> inline FsRangeInfo[0x40].
    * Only non-mutating op 2 (InvalidateCache) and op 3 (QueryRange) route.
    * Op 0 is Clear (destructive), NEVER forward it.
    * No invented successful response: return host service status. */
   if(len<24)return NX2_INVALID;
   uint32_t operation=read32(in);
   uint64_t offset=read64(in+8),amount=read64(in+16);
   if((operation!=2 && operation!=3)||offset>INT64_MAX||
      amount>INT64_MAX||offset>INT64_MAX-amount)
    return NX2_FS_UNSUPPORTED;
   memset(out,0,64);
   uint32_t rc=nx2_rpc(RPC_SAVE_OPERATE_RANGE,obj->token,
                       operation,offset,amount,out,0,0,0);
   if(!rc)*out_size=64;
   return rc;
  }
  default:return NX2_FS_UNSUPPORTED;
 }
}
static uint32_t do_dir(const VfsObject *obj,uint32_t cmd,
                       const HipcParsedRequest *rq,
                       unsigned char out[64],uint32_t *out_size){
 uint64_t answer=0;
 if(cmd==1){
  uint32_t rc=nx2_rpc(RPC_DIR_COUNT,obj->token,0,0,0,0,0,0,&answer);
  if(!rc){memcpy(out,&answer,8);*out_size=8;}
  return rc;
 }
 if(cmd==0){
  void *buf=0;uint64_t n=0;
  if(!data_buffer(rq,1,&buf,&n)||n%0x310U||n/0x310U>1024)
   return NX2_INVALID;
  uint32_t rc=nx2_rpc(RPC_DIR_READ,obj->token,n/0x310U,
                      0,0,buf,0,0,&answer);
  if(!rc){memcpy(out,&answer,8);*out_size=8;}
  return rc;
 }
 return NX2_FS_UNSUPPORTED;
}
/* ISaveDataInfoReader::Read: command 0, an output array of
 * 0x60-byte SaveDataInfo records, and an inline u64 read count.
 * Only the current guest's SD-backed caches are enumerated. */
static uint32_t do_cache_reader(const VfsObject *obj,uint32_t cmd,
                                const HipcParsedRequest *rq,
                                unsigned char out[64],uint32_t *out_size){
 if(cmd!=0)return NX2_FS_UNSUPPORTED;
 void *buf=0;uint64_t n=0;
 if(!data_buffer(rq,1,&buf,&n)||n%NX2_CACHE_INFO_BYTES)
  return NX2_INVALID;
 uint64_t capacity=n/NX2_CACHE_INFO_BYTES;
 if(capacity>256)capacity=256;
 if(!capacity)return NX2_INVALID;
 uint64_t cursor=0;
 if(!cache_reader_begin(obj->object_id,&cursor))return NX2_INVALID;
 uint64_t actual=0;
 uint32_t rc=nx2_rpc(RPC_CACHE_ENUM,cursor,capacity,0,0,buf,0,0,&actual);
 int valid=!rc&&actual<=capacity;
 cache_reader_end(obj->object_id,actual,valid);
 if(!valid)return rc?rc:NX2_INVALID;
 memcpy(out,&actual,8);*out_size=8;
 return 0;
}
/* A native SaveDataAttribute is exactly 0x40 bytes. Canonicalize the
 * resulting subdirectory once, for both explicit creation and opening.
 * This namespace never escapes the selected guest title's SD root. */
/* No formatted I/O or allocation from the guest's intercepted FSP call.
 * Preserve existing private save namespace bytes exactly. */
static char *hex_fixed(char *dst,uint64_t n,unsigned digits){
 static const char hex[]="0123456789abcdef";
 for(unsigned i=digits;i;i--)
  dst[digits-i]=hex[(n>>((i-1)*4))&15U];
 return dst+digits;
}
/* Native fsp-srv and SDK MountCacheStorage must resolve to one directory.
 * This is a guest-only cache, never the Mario Kart World host cache. */
static int save_namespace(uint8_t space,const unsigned char *attr,
                          char name[128]){
 if(!attr||!name)return 0;
 if(attr[32]==5)
  return nx2_guest_cache_path(nx2_launch->guest_program_id,space,
                              attr,0x40,name,128);
 char *p=name;
 memcpy(p,"vsave-",6);p+=6;
 p=hex_fixed(p,space,2);*p++='-';
 p=hex_fixed(p,attr[32],2);*p++='-';
 uint16_t index=(uint16_t)attr[34]|((uint16_t)attr[35]<<8);
 p=hex_fixed(p,index,4);*p++='-';
 p=hex_fixed(p,read64(attr),16);*p++='-';
 p=hex_fixed(p,read64(attr+8),16);*p++='-';
 p=hex_fixed(p,read64(attr+16),16);*p++='-';
 p=hex_fixed(p,read64(attr+24),16);
 *p=0;
 return (size_t)(p-name)<128;
}
/* Shared-memory breadcrumbs replace synchronous RPC_LOG calls in
 * the FSP dispatch path. This avoids waiting for SD writes while the game
 * is inside a synchronous SendSyncRequest hook. */
static void fsp_stage(uint32_t stage,Nx2IpcDecision d,
                      uint32_t result,uint32_t child){
 Nx2Launch *l=nx2_launch;
 __atomic_store_n(&l->fsp_route_command,d.command,__ATOMIC_RELAXED);
 __atomic_store_n(&l->fsp_route_object,d.object_id,__ATOMIC_RELAXED);
 __atomic_store_n(&l->fsp_route_result,result,__ATOMIC_RELAXED);
 __atomic_store_n(&l->fsp_route_child,child,__ATOMIC_RELAXED);
 __atomic_store_n(&l->fsp_route_stage,stage,__ATOMIC_RELEASE);
}
int nx2_guest_fsp_route(void *buffer,uint64_t size,uint32_t session,
                        uint32_t session_kind,Nx2IpcDecision d){
 if(session_kind!=2||!d.valid||!d.domain||d.control)return 0;
 VfsObject obj=find_object(session,d.object_id);
 if(!obj.kind&&d.close_kind==0&&
    (d.command!=22&&d.command!=23&&
     d.command!=51&&d.command!=52&&d.command!=53&&d.command!=62))
  return 0;
 if(!obj.kind&&d.close_kind)return 0;
 if(size<64)return 0;
 if(!obj.kind && !d.close_kind)
  fsp_stage(1,d,UINT32_MAX,0);
 if(d.close_kind==1){
  VfsObject old={0};
  if(forget_object(session,d.object_id,&old))
   close_child(&old);
  /* CMIF domain closes have no SFCO payload. */
  memset(buffer,0,8);
  return 1;
 }
 if(d.close_kind)return 0;
 const unsigned char *head=0,*in=0;
 uint64_t avail=0;
 uint32_t token=0;
 if(!native_domain_payload(buffer,size,d,&head,&in,&avail,&token)){
  /* Known save-open methods must NOT escape to the native host service.
   * Return a syntactically valid error response even when the guest
   * request has incorrect length/layout; no extra pointer reparse. */
  fsp_stage(6,d,NX2_INVALID,0);
  return write_reply(buffer,size,1,NX2_INVALID,0,0,0,0);
 }
 if(!obj.kind)
  fsp_stage(2,d,UINT32_MAX,0);
 uint32_t rc=NX2_FS_UNSUPPORTED,new_id=0;
 uint32_t out_size=0;unsigned char out[64]={0};
 if(!obj.kind){
  char name[128]={0};
  if(d.command==62){
   /* Cache-storage list must not be read under the MKW host identity. */
   new_id=allocate_object(session,VFS_CACHE_READER,1,0,0);
   rc=new_id?0:NX2_LIMIT;
   fsp_stage(rc?6:4,d,rc,new_id);
  }else if(d.command==22||d.command==23){
   /* IFileSystemProxy::CreateSaveDataFileSystem [22] and
    * CreateSaveDataFileSystemBySystemSaveDataId [23] carry a 0x40-byte
    * attribute followed by 0x40-byte creation metadata and a 0x10-byte
    * owner/options field for [22]. Save creation is a private directory,
    * never a native save created under Mario Kart World's identity.
    * Root names use the identical formatter as the corresponding open. */
   size_t minimum=d.command==22?0x90:0x80;
   if(avail<minimum||!save_namespace(d.command==22?1:0,in,name))
    rc=NX2_INVALID;
   else if(in[32]==5){
    /* Native CreateCacheStorage participates in the same quota and
     * directory lifecycle as the nn::fs high-level cache adapter. */
    uint64_t size=read64(in+0x40),journal=read64(in+0x48);
    /* FsSaveDataCreationInfo.save_data_space_id is byte 0x24
     * after the 0x40-byte attribute. Never let a non-user cache
     * request silently acquire the guest's user cache namespace. */
    if(d.command!=22||in[0x64]!=1||size>INT64_MAX||
       journal>INT64_MAX||size>INT64_MAX-journal)
      rc=NX2_INVALID;
    else{
     uint16_t index=(uint16_t)in[34]|((uint16_t)in[35]<<8);
     rc=nx2_rpc(RPC_CACHE_CREATE,index,size,journal,0,0,0,0,0);
    }
   }else rc=nx2_rpc(RPC_VFS_PREPARE_ROOT,0,0,0,0,0,name,0,0);
   fsp_stage(rc?6:4,d,rc,0);
  }else{
   /* OpenSaveDataFileSystem [51], BySystemSaveDataId [52], or
    * OpenReadOnlySaveDataFileSystem [53].
    * Input = u8 SaveDataSpaceId + padding + 0x40 SaveDataAttribute. */
   if(avail<0x48||!save_namespace(in[0],in+8,name))
    rc=NX2_INVALID;
   else{
    /* Real cache mounts must already exist. Unknown/foreign cache
     * identities cannot be silently mapped to an empty vsave-* root. */
    if(in[8+32]==5){
     uint16_t index=(uint16_t)in[8+34]|((uint16_t)in[8+35]<<8);
     rc=nx2_rpc(RPC_CACHE_CHECK,index,0,0,0,0,0,0,0);
    }else rc=0;
    if(!rc){
     new_id=allocate_object(session,VFS_FS,d.command==53,0,name);
     rc=new_id?0:NX2_LIMIT;
    }
   }
   fsp_stage(rc?6:4,d,rc,new_id);
  }
 }else{
  /* Parse descriptor lists only for already allocated virtual child
   * interfaces, where path/read/write buffers are actually required. */
  HipcParsedRequest rq=hipcParseRequest(buffer);
  if(obj.kind==VFS_FS)
   rc=do_fs(&obj,d.command,in,avail,&rq,session,&new_id,out,&out_size);
  else if(obj.kind==VFS_FILE)
   rc=do_file(&obj,d.command,in,avail,&rq,out,&out_size);
  else if(obj.kind==VFS_DIR)
   rc=do_dir(&obj,d.command,&rq,out,&out_size);
  else if(obj.kind==VFS_CACHE_READER)
   rc=do_cache_reader(&obj,d.command,&rq,out,&out_size);
 }
 if(!write_reply(buffer,size,1,rc,rc?0:new_id,
                 rc?0:out,rc?0:out_size,token)){
  if(!obj.kind)fsp_stage(6,d,NX2_INVALID,new_id);
  return 0;
 }
 if(!obj.kind)fsp_stage(5,d,rc,new_id);
 return 1;
}
