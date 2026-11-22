/* SPDX-License-Identifier: GPL-2.0-only */
#include "ipc_policy.h"
static uint32_t word(const unsigned char *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static int name(const unsigned char *p){static const unsigned char s[8]={'f','s','p','-','s','r','v',0};for(unsigned i=0;i<8;i++)if(p[i]!=s[i])return 0;return 1;}
/* A destroyed IPC session is no longer a valid service capability.
 * HIPC Close usually reports ConnectionClosed (0xF601) even though it
 * successfully destroys the server side. */
int nx2_ipc_session_closed(Nx2IpcDecision d,uint32_t result){
 return d.valid&&d.close_kind==2&&(result==0||result==0xF601U);
}
/* Decode the declared HIPC data area; bytes left over in a reused TLS buffer
 * must never affect service acquisition or the FSP allow-list. */
Nx2IpcDecision nx2_ipc_decide(const void *buffer,uint64_t size,uint32_t kind){
 Nx2IpcDecision d={0};const unsigned char *p=buffer;if(!p||size<8)return d;
 uint32_t a=word(p),b=word(p+4),type=a&65535;uint64_t off=8;
 /* HIPC type 2 releases an entire session and has no CMIF SFCI header.
  * The previous FSP command gate misclassified it as an invalid method. */
 if(type==2){
   if(a==2 && b==0){d.valid=1;d.close_kind=2;}
   return d;
 }
 if(b&0x80000000){if(size<12)return d;uint32_t s=word(p+8);off=12+((s&1)?8:0)+4*(((s>>1)&15)+((s>>5)&15));}
 off+=8*((a>>16)&15)+12*(((a>>20)&15)+((a>>24)&15)+((a>>28)&15));
 uint64_t bytes=4*(b&1023);if(off>size||bytes>size-off)return d;uint64_t end=off+bytes;
 if(kind==1&&type==17){d.command=1;d.valid=1;d.want_fsp=bytes>=8&&name(p+off);return d;}
 if(type!=4&&type!=5&&type!=6&&type!=7)return d;
 uint64_t cmif=(off+15)&~UINT64_C(15);if(cmif+16>end)return d;
 if(word(p+cmif)!=0x49434653){
   /* Domain messages address an object on a shared session handle.
    * Close (domain kind 2) contains only the domain header, no SFCI.
    * Match the exact canonical zero-payload release header so it cannot
    * grant arbitrary requests access to the host filesystem service. */
   if(p[cmif]==2 &&
      word(p+cmif)==2 &&
      word(p+cmif+8)==0 && word(p+cmif+12)==0 &&
      word(p+cmif+4)!=0){
     d.valid=1;d.domain=1;d.object_id=word(p+cmif+4);
     d.close_kind=1;
     return d;
   }
   /* Invoke (kind 1) carries a separate 16-byte SFCI command header. */
   if(cmif+32>end||p[cmif]!=1||word(p+cmif+16)!=0x49434653)
     return d;
   d.domain=1;
   d.object_id=word(p+cmif+4);
   if(!d.object_id)return d;
   cmif+=16;
 }
 d.command=word(p+cmif+8);d.valid=1;d.control=type==5||type==7;
 if(kind==1&&!d.control&&d.command==1&&cmif+24<=end)d.want_fsp=name(p+cmif+16);
 return d;
}
/* Forward real native service operations for process initialization and
 * process-independent FSP administration, rather than inventing a reply.
 * FSP 180 is an interface-level method observed on Switch 2. It is NOT an
 * open/create/mount/delete save call. Preserve the guest's SD-backed
 * filesystem virtualization for commands 22/23/34/51/52/53/62 and every
 * known save child object. No title IDs, names or ROM formats are checked.
 * The underlying service returns its actual CMIF status and permissions. */
int nx2_ipc_fsp_allowed(Nx2IpcDecision d){
 if(!d.valid)return 0;
 if(d.control)return d.command<=4;
 if(d.close_kind)return 1;
 /* Only non-mutating system-state operations or real device/notifier
  * interface constructors are safe under the launcher's native PID. */
 switch(d.command){
 case 1: case 27: case 29: case 180: case 400:
 case 500: case 501: case 502: case 503: case 1005:return 1;
 default:return 0;
 }
}
int nx2_ipc_fsp_native_child_allowed(Nx2IpcDecision d,uint32_t kind){
 if(!d.valid||(kind!=4&&kind!=5))return 0;
 if(d.control)return d.command<=4;
 if(d.close_kind)return 1;
 if(kind==5)return d.command==0; /* Native notifier kernel event */
 switch(d.command){
 case 0: case 1: case 2: case 3: case 4: case 5: case 6:
 case 100: case 101: case 111: case 112: case 114:
 case 200: case 202: case 203: case 205: case 206:
 case 207: case 208: case 211: case 212: case 214:
 case 216: case 217: case 218: case 220: case 221:
 case 222: case 224: case 226: case 301: case 402:return 1;
 default:return 0;
 }
}
/* ISaveDataInfoReader supports a read-only enumeration call at command 0.
 * Reject other calls on this interface instead of giving it proxy access. */
int nx2_ipc_save_reader_allowed(Nx2IpcDecision d){
 return d.valid&&((d.control&&d.command<=4)||
                  (!d.control&&d.command==0));
}

static int data_area(const unsigned char *p,uint64_t size,uint64_t *start,uint64_t *end){
 if(!p||size<8)return 0;
 uint32_t a=word(p),b=word(p+4);uint64_t off=8;
 if(b&0x80000000){if(size<12)return 0;uint32_t s=word(p+8);off=12+((s&1)?8:0)+4*(((s>>1)&15)+((s>>5)&15));}
 off+=8*((a>>16)&15)+12*(((a>>20)&15)+((a>>24)&15)+((a>>28)&15));
 uint64_t bytes=4*(b&1023);if(off>size||bytes>size-off)return 0;
 *start=off;*end=off+bytes;return 1;
}
int nx2_ipc_service_name(const void *buffer,uint64_t size,char out[9]){
 if(!out)return 0;
 out[0]=0;Nx2IpcDecision d=nx2_ipc_decide(buffer,size,1);
 if(!d.valid||d.control||d.command!=1)return 0;
 const unsigned char *p=buffer;uint64_t at,end;if(!data_area(p,size,&at,&end))return 0;
 if((word(p)&65535)!=17){at=(at+15)&~UINT64_C(15);if(at+16>end)return 0;if(word(p+at)!=0x49434653)at+=16;at+=16;}
 if(at+8>end)return 0;
 for(unsigned i=0;i<8;i++){unsigned char c=p[at+i];if(!c){out[i]=0;return 1;}if(c<32||c>126){out[0]=0;return 0;}out[i]=(char)c;}out[8]=0;return 1;
}
int nx2_ipc_reply_result(const void *buffer,uint64_t size,uint32_t request_type,uint32_t *out){
 const unsigned char *p=buffer;uint64_t at,end;if(!out||!data_area(p,size,&at,&end))return 0;
 if(request_type>=16){if(end-at<4)return 0;*out=word(p+at);return 1;}
 if(request_type<4||request_type>7)return 0;
 at=(at+15)&~UINT64_C(15);if(at+16>end)return 0;
 if(word(p+at)!=0x4f434653){at+=16;if(at+16>end||word(p+at)!=0x4f434653)return 0;}
 *out=word(p+at+8);return 1;
}


/* HIPC control 1 (CopyFromCurrentDomain) converts a domain object to a
 * separate session handle. Preserve its interface kind after this operation.
 * Decode only the 4-byte object ID in a bounded control SFCI request. */
int nx2_ipc_control_copy_domain_object(const void *buffer,uint64_t size,
                                       uint32_t *out){
  if(!out)return 0;
  Nx2IpcDecision d=nx2_ipc_decide(buffer,size,2);
  if(!d.valid||!d.control||d.domain||d.command!=1)return 0;
  const unsigned char *p=buffer;uint64_t at=0,end=0;
  if(!data_area(p,size,&at,&end))return 0;
  at=(at+15)&~UINT64_C(15);
  if(at>end||end-at<20||word(p+at)!=0x49434653)return 0;
  uint32_t object=word(p+at+16);
  if(!object)return 0;
  *out=object;
  return 1;
}
/* A cloned session is returned as a MOVED HIPC handle, not a copied
 * handle. Do not confuse its ID with a domain object ID or accept a
 * stale value past the actual buffer boundary. */
int nx2_ipc_reply_first_moved_handle(const void *buffer,uint64_t size,
                                     uint32_t *out){
  const unsigned char *p=buffer;
  if(!p||!out||size<12||(word(p+4)&0x80000000U)==0)return 0;
  uint32_t sh=word(p+8);
  uint32_t copies=(sh>>1)&15U,moves=(sh>>5)&15U;
  uint64_t off=12U+((sh&1U)?8U:0U)+(uint64_t)copies*4U;
  if(!moves||off>size||(uint64_t)moves*4U>size-off)return 0;
  uint32_t handle=word(p+off);
  if(!handle)return 0;
  *out=handle;
  return 1;
}

/* A zero-payload domain response that creates a child object places its
 * first object ID immediately after the 16-byte SFCO response header.
 * Read only if the HIPC data bounds, response magic and object count
 * establish this exact layout. Callers should fall back to normal
 * non-domain returned handles if no domain object can be verified. */
int nx2_ipc_domain_response_first_object(const void *buffer,uint64_t size,
                                         uint32_t *out){
 const unsigned char *p=buffer;
 uint64_t at=0,end=0;
 if(!out||!data_area(p,size,&at,&end))return 0;
 at=(at+15)&~UINT64_C(15);
 if(at>end||end-at<36)return 0;
 uint32_t count=word(p+at);
 if(!count||count>8||word(p+at+16)!=0x4f434653||
    word(p+at+24)!=0)return 0;
 uint32_t id=word(p+at+32);
 if(!id)return 0;
 *out=id;
 return 1;
}
