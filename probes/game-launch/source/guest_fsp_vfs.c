/* SPDX-License-Identifier: GPL-2.0-only */
/* Interface-based guest FSP domain bridge.
 * All files remain under the currently selected title's private SD tree;
 * never forward guest save opens to the host title's fsp-srv identity.
 * Native IFileSystem/IFile/IDirectory commands share the existing RPC backend.
 */
#include "guest_fsp_vfs.h"
#include "launch.h"
#include <switch.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#define VFS_MAX_OBJECTS 96
#define VFS_FS 1
#define VFS_FILE 2
#define VFS_DIR 3
typedef struct {
 uint32_t session,object_id,kind,read_only;
 uint64_t token;
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
  if(root)snprintf(vfs_objects[i].root,sizeof(vfs_objects[i].root),"%s",root);
  else vfs_objects[i].root[0]=0;
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
static int valid_memory(const void *ptr,uint64_t bytes,int writable){
 uintptr_t at=(uintptr_t)ptr;
 if(!ptr||!bytes||bytes>0x1000000U||at>UINTPTR_MAX-bytes)return 0;
 MemoryInfo mi;u32 page;
 Result rc=svcQueryMemory(&mi,&page,at);
 if(R_FAILED(rc)||mi.addr>at||mi.size>UINTPTR_MAX-mi.addr||
    mi.addr+mi.size-at<bytes)return 0;
 return (mi.perm&(writable?2U:1U))==(writable?2U:1U);
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
int nx2_guest_fsp_unsupported(void *buffer,uint64_t size,Nx2IpcDecision d){
 if(!d.valid||d.control||d.close_kind)return 0;
 /* Snapshot the request context token before replacing its CMIF header. */
 uint32_t token=0;
 HipcParsedRequest q=hipcParseRequest(buffer);
 const unsigned char *start=cmifGetAlignedDataStart(q.data.data_words,buffer);
 const unsigned char *hdr=start+(d.domain?16:0);
 if(hdr+16<=(const unsigned char*)buffer+size)
  token=read32(hdr+12);
 return write_reply(buffer,size,d.domain,NX2_FS_UNSUPPORTED,0,0,0,token);
}
/* An nn::sf auto-buffer is encoded as a PAIR of descriptors: one X-static
 * and one A-buffer, with the unused side explicitly set to NULL/zero.
 * They are the same logical argument index, not two consecutive paths.
 * This also handles pure static and pure transfer-buffer calls. */
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
 return *size && *size<=0x1000000U && valid_memory(*dst,*size,output);
}
static int scoped_path(const VfsObject *obj,const char *path,
                       char out[NX2_PATH_MAX]){
 if(!obj->root[0])return 0;
 while(*path=='/')path++;
 int n=snprintf(out,NX2_PATH_MAX,"%s%s%s",obj->root,
                *path?"/":"",path);
 return n>=0&&n<NX2_PATH_MAX;
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
  case 10:return nx2_rpc(RPC_COMMIT,0,0,0,0,0,0,0,0);
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
   void *buf=0;uint64_t cap=0;
   if(n>0x1000000U||!data_buffer(rq,cmd==0,&buf,&cap)||n>cap)
    return NX2_INVALID;
   uint32_t rc=nx2_rpc(cmd==0?RPC_SAVE_READ:RPC_SAVE_WRITE,
                       obj->token,offset,n,read32(in),buf,0,0,&answer);
   if(!rc&&cmd==0){memcpy(out,&answer,8);*out_size=8;}
   return rc;
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
/* A native SaveDataAttribute is exactly 0x40 bytes. Canonicalize the
 * resulting subdirectory once, for both explicit creation and opening.
 * This namespace never escapes the selected guest title's SD root. */
static int save_namespace(uint8_t space,const unsigned char *attr,
                          char name[128]){
 uint64_t app=read64(attr),uid0=read64(attr+8);
 uint64_t uid1=read64(attr+16),system_id=read64(attr+24);
 uint32_t type=attr[32],index=(uint32_t)attr[34] |
                               ((uint32_t)attr[35]<<8);
 int n=snprintf(name,128,
     "vsave-%02x-%02x-%04x-%016llx-%016llx-%016llx-%016llx",
     (unsigned)space,type,index,
     (unsigned long long)app,(unsigned long long)uid0,
     (unsigned long long)uid1,(unsigned long long)system_id);
 return n>0&&n<128;
}
int nx2_guest_fsp_route(void *buffer,uint64_t size,uint32_t session,
                        uint32_t session_kind,Nx2IpcDecision d){
 if(session_kind!=2||!d.valid||!d.domain||d.control)return 0;
 VfsObject obj=find_object(session,d.object_id);
 if(!obj.kind&&d.close_kind==0&&
    (d.command!=22&&d.command!=23&&
     d.command!=51&&d.command!=52&&d.command!=53))
  return 0;
 if(!obj.kind&&d.close_kind)return 0;
 if(size<64)return 0;
 if(d.close_kind==1){
  VfsObject old={0};
  if(forget_object(session,d.object_id,&old))
   close_child(&old);
  /* CMIF domain closes have no SFCO payload. */
  memset(buffer,0,8);
  return 1;
 }
 if(d.close_kind)return 0;
 HipcParsedRequest rq=hipcParseRequest(buffer);
 const unsigned char *cmif=cmifGetAlignedDataStart(rq.data.data_words,buffer);
 const unsigned char *head=cmif+16;
 const unsigned char *in=head+16;
 const unsigned char *end=(const unsigned char*)buffer+size;
 const unsigned char *declared_end=(const unsigned char*)rq.data.data_words+
                                    (uint64_t)rq.meta.num_data_words*4;
 /* Validate the declared HIPC payload, not leftover data in reused TLS. */
 if(declared_end>end||in>declared_end||read32(head)!=0x49434653U)
  return 0;
 uint64_t avail=(uint64_t)(declared_end-in);
 uint32_t token=read32(head+12);
 uint32_t rc=NX2_FS_UNSUPPORTED,new_id=0;
 uint32_t out_size=0;unsigned char out[64]={0};
 if(!obj.kind){
  char name[128]={0};
  if(d.command==22||d.command==23){
   /* IFileSystemProxy::CreateSaveDataFileSystem [22] and
    * CreateSaveDataFileSystemBySystemSaveDataId [23] carry a 0x40-byte
    * attribute followed by 0x40-byte creation metadata and a 0x10-byte
    * owner/options field for [22]. Save creation is a private directory,
    * never a native save created under Mario Kart World's identity.
    * Root names use the identical formatter as the corresponding open. */
   size_t minimum=d.command==22?0x90:0x80;
   if(avail<minimum||!save_namespace(d.command==22?1:0,in,name))
    rc=NX2_INVALID;
   else rc=nx2_rpc(RPC_VFS_PREPARE_ROOT,0,0,0,0,0,name,0,0);
   nx2_rpc(RPC_LOG,session,d.command,rc,0,0,
           "GUEST_FSP_SAVE_CREATE",0,0);
  }else{
   /* OpenSaveDataFileSystem [51], BySystemSaveDataId [52], or
    * OpenReadOnlySaveDataFileSystem [53].
    * Input = u8 SaveDataSpaceId + padding + 0x40 SaveDataAttribute. */
   if(avail<0x48||!save_namespace(in[0],in+8,name))
    rc=NX2_INVALID;
   else{
    rc=nx2_rpc(RPC_VFS_PREPARE_ROOT,0,0,0,0,0,name,0,0);
    if(!rc){
     new_id=allocate_object(session,VFS_FS,d.command==53,0,name);
     if(!new_id)rc=NX2_LIMIT;
    }
   }
   nx2_rpc(RPC_LOG,session,d.object_id,d.command,rc,0,
           "GUEST_FSP_SAVE_OPEN",0,0);
  }
 }else if(obj.kind==VFS_FS)
  rc=do_fs(&obj,d.command,in,avail,&rq,session,&new_id,out,&out_size);
 else if(obj.kind==VFS_FILE)
  rc=do_file(&obj,d.command,in,avail,&rq,out,&out_size);
 else if(obj.kind==VFS_DIR)
  rc=do_dir(&obj,d.command,&rq,out,&out_size);
 if(!write_reply(buffer,size,1,rc,rc?0:new_id,
                 rc?0:out,rc?0:out_size,token))return 0;
 return 1;
}
