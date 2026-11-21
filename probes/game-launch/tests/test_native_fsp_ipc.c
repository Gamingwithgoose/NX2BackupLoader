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
 printf("FSP native CMIF fixture: %s\n",fail?"FAILED":"PASS");
 return fail?1:0;
}
