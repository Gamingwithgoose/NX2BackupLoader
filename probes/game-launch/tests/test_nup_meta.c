/* SPDX-License-Identifier: GPL-2.0-only */
#include "../source/nup_meta.h"
#include <assert.h>
static void put32(unsigned char *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(8*i));}
int main(void){
 unsigned char npdm[160]={0},tlv[168]={0};
 memcpy(npdm,"META",4);put32(npdm+0x70,128);put32(npdm+0x74,32);memcpy(npdm+128,"ACI0",4);
 assert(nx2_meta_npdm(npdm,sizeof(npdm)));
 put32(tlv+4,sizeof(npdm));memcpy(tlv+8,npdm,sizeof(npdm));
 assert(nx2_meta_base(tlv,sizeof(tlv))==0);
 tlv[0]=1;assert(nx2_meta_base(tlv,sizeof(tlv))==1);
 tlv[0]=4;assert(nx2_meta_base(tlv,sizeof(tlv))==0); /* A title is not an NPDM fact. */
 tlv[0]=1;assert(nx2_meta_base(tlv,sizeof(tlv)-1)==0);
 put32(npdm+0x70,UINT32_MAX);assert(!nx2_meta_npdm(npdm,sizeof(npdm)));
 memcpy(npdm,"METAL GEAR",10);assert(!nx2_meta_npdm(npdm,sizeof(npdm)));
 assert(!nx2_meta_npdm((const unsigned char *)"META",4));
 return 0;
}
