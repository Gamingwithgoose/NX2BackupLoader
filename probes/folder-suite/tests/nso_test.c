#include "../source/nso.h"
#include <windows.h>
#include <bcrypt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int checks,failures;
#define CHECK(name,ok) do{checks++;if(!(ok)){failures++;printf("FAIL %s\n",name);}else printf("PASS %s\n",name);}while(0)
static void word(void *p,uint32_t v){memcpy(p,&v,4);}
static void hash(unsigned char out[32],const void *data,size_t size){if(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,NULL,0,(PUCHAR)data,(ULONG)size,out,32)<0)abort();}
static void initial(unsigned char b[0x150]){memset(b,0,0x150);memcpy(b,"NSO0",4);for(unsigned i=0;i<3;i++){word(b+16+i*16,i==0?0x100:i==1?0x110:0x130);word(b+20+i*16,i*4096);word(b+24+i*16,i?32:16);}word(b+0x104,4096);memcpy(b+0x110,"MOD0",4);word(b+0x114,4096);}
static int prepare(const char *path,const unsigned char *data,size_t bytes,const char *expected){FILE *f=fopen(path,"wb");if(!f||fwrite(data,1,bytes,f)!=bytes)exit(2);fclose(f);void *memory=NULL;size_t image,bss;char error[160];int ok=nx2_prepare_nso(path,expected,hash,&memory,&image,&bss,error);if(ok)nx2_free_pages(memory);return ok;}
int main(int argc,char **argv){
    if(argc!=2)return 2;unsigned char bytes[0x150];const char *path=argv[1];initial(bytes);
    CHECK("raw NSO with MOD0 in rodata",prepare(path,bytes,sizeof(bytes),NULL));
    CHECK("short NSO rejected",!prepare(path,bytes,4,NULL));
    initial(bytes);bytes[0]='X';CHECK("bad NSO magic rejected",!prepare(path,bytes,sizeof(bytes),NULL));
    initial(bytes);word(bytes+0x10,0xffffff00);CHECK("outside-file segment rejected",!prepare(path,bytes,sizeof(bytes),NULL));
    initial(bytes);word(bytes+0x24,0);CHECK("overlapping segments rejected",!prepare(path,bytes,sizeof(bytes),NULL));
    initial(bytes);word(bytes+0x3c,0xffffffff);CHECK("oversized BSS rejected",!prepare(path,bytes,sizeof(bytes),NULL));
    initial(bytes);word(bytes+0x104,0xffffffff);CHECK("invalid MOD0 pointer rejected",!prepare(path,bytes,sizeof(bytes),NULL));
    initial(bytes);word(bytes+0xc,1);word(bytes+0x60,3);memset(bytes+0x100,0,3);CHECK("invalid LZ4 backreference rejected",!prepare(path,bytes,sizeof(bytes),NULL));
    initial(bytes);word(bytes+0xc,0x81);word(bytes+0x60,3);CHECK("invalid ZBIC rejected",!prepare(path,bytes,sizeof(bytes),NULL));
    initial(bytes);word(bytes+0xc,8);CHECK("segment hash mismatch rejected",!prepare(path,bytes,sizeof(bytes),NULL));
    initial(bytes);CHECK("wrong full-file hash rejected",!prepare(path,bytes,sizeof(bytes),"0000000000000000000000000000000000000000000000000000000000000000"));
    remove(path);printf("%d checks, %d failures\n",checks,failures);return failures?1:0;
}
