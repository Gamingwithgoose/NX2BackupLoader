/* SPDX-License-Identifier: GPL-2.0-only */
/* Portable CMIF regression fixtures; compile on desktop with:
 * cc -std=c11 -Wall -Wextra -Werror -I../source \
 *    test_native_fsp_ipc.c ../source/ipc_policy.c -o test_native_fsp_ipc
 * This does not launch a game or access any host filesystem. */
#include "ipc_policy.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static void put32(uint8_t *p,uint32_t v){
 for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));
}
static int expect(int test,const char *name){
 if(!test)fprintf(stderr,"FAIL %s\n",name);
 return !test;
}
int main(void){
 uint8_t p[256]={0};
 int fail=0;
 /* HIPC Request / CMIF domain send / FSP root object 0xc8. */
 put32(p,4);
 put32(p+4,48); /* 16-byte alignment + 16-byte domain + 16-byte SFCI
                   + 144-byte CreateSaveData payload. */
 p[16]=1;
 p[18]=0xa0; /* SFCI and payload: 0x90+0x10 bytes */
 put32(p+20,0xc8);
 put32(p+32,0x49434653);
 put32(p+40,22);
 Nx2IpcDecision d=nx2_ipc_decide(p,sizeof(p),2);
 fail+=expect(d.valid&&d.domain&&d.object_id==0xc8&&d.command==22,
              "native FSP create-save domain command");
 fail+=expect(!nx2_ipc_fsp_allowed(d),
              "native save must not be sent into host FSP");
 put32(p+40,23);
 d=nx2_ipc_decide(p,sizeof(p),2);
 fail+=expect(d.valid&&d.command==23,
              "native FSP system-save creation command");
 put32(p+40,51);
 d=nx2_ipc_decide(p,sizeof(p),2);
 fail+=expect(d.valid&&d.command==51,
              "native FSP save open command");
 put32(p+40,53);
 d=nx2_ipc_decide(p,sizeof(p),2);
 fail+=expect(d.valid&&d.command==53,
              "native FSP read-only save open");
 put32(p+40,62);
 d=nx2_ipc_decide(p,sizeof(p),2);
 fail+=expect(d.valid&&d.domain&&d.command==62,
              "native cache reader open interface");
 fail+=expect(!nx2_ipc_fsp_allowed(d),
              "cache enumeration never forwarded to host title fsp-srv");
 memset(p,0,sizeof(p));
 put32(p,4);put32(p+4,8);
 p[16]=2;put32(p+20,0xe0000001U);
 d=nx2_ipc_decide(p,sizeof(p),2);
 fail+=expect(d.valid&&d.domain&&d.close_kind==1 &&
              d.object_id==0xe0000001U,
              "virtual FSP domain object close");
 memset(p,0,sizeof(p));
 put32(p,0);put32(p+4,13);
 put32(p+16,1); /* domain output has one child */
 put32(p+32,0x4f434653); /* SFCO */
 put32(p+48,0xe0000001U);
 uint32_t obj=0,rc=UINT32_MAX;
 fail+=expect(nx2_ipc_domain_response_first_object(p,sizeof(p),&obj)&&
              obj==0xe0000001U,"domain response object extraction");
 fail+=expect(nx2_ipc_reply_result(p,sizeof(p),4,&rc)&&rc==0,
              "domain response CMIF result extraction");
 /* HIPC manager Control 1: CopyFromCurrentDomain, with one object ID
  * after the SFCI header. This must survive exported reader handles. */
 memset(p,0,sizeof(p));
 put32(p,5);put32(p+4,9);
 put32(p+16,0x49434653U);put32(p+24,1);
 put32(p+32,0x12345678U);
 uint32_t id=0;
 fail+=expect(nx2_ipc_control_copy_domain_object(p,sizeof(p),&id)&&
              id==0x12345678U,"copy-from-domain object ID");
 fail+=expect(!nx2_ipc_control_copy_domain_object(p,34,&id),
              "reject truncated copy-from-domain input");
 put32(p+24,2);
 fail+=expect(!nx2_ipc_control_copy_domain_object(p,sizeof(p),&id),
              "clone command is not a domain-copy command");
 /* An HIPC response may return copied handles before the moved session
  * handle; only the MOVED handle is the newly cloned session. */
 memset(p,0,sizeof(p));
 put32(p,0);put32(p+4,0x80000008U);
 put32(p+8,(1U<<1)|(1U<<5)); /* one copy then one move */
 put32(p+12,0x77U);put32(p+16,0x1234U);
 put32(p+32,0x4f434653U);
 uint32_t moved=0;
 fail+=expect(nx2_ipc_reply_first_moved_handle(p,sizeof(p),&moved)&&
              moved==0x1234U,"returned moved session, not copied handle");
 fail+=expect(!nx2_ipc_reply_first_moved_handle(p,19,&moved),
              "reject truncated moved-handle reply");
 put32(p+8,1U<<1); /* copies only */
 fail+=expect(!nx2_ipc_reply_first_moved_handle(p,sizeof(p),&moved),
              "reject copy-only reply");
 /* HIPC type 2 ends the server-side session even when the kernel
  * returns ConnectionClosed, but a failed unrelated IPC must not. */
 memset(p,0,sizeof(p));
 put32(p,2);
 d=nx2_ipc_decide(p,sizeof(p),0);
 fail+=expect(nx2_ipc_session_closed(d,0xF601U),
              "connection-closed means IPC session already destroyed");
 fail+=expect(nx2_ipc_session_closed(d,0),
              "normal IPC close means session destroyed");
 fail+=expect(!nx2_ipc_session_closed(d,0x1015U),
              "failed IPC close preserves its handle state");
 put32(p,4);
 d=nx2_ipc_decide(p,sizeof(p),0);
 fail+=expect(!nx2_ipc_session_closed(d,0),
              "ordinary method is not a session close");
 printf("FSP native CMIF fixture: %s\n",fail?"FAILED":"PASS");
 return fail?1:0;
}
