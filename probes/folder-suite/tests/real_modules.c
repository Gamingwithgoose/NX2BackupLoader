#include "../source/module.h"
#include "../source/nso.h"
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <bcrypt.h>
static void hash(unsigned char out[32],const void *data,size_t bytes){
    if(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,NULL,0,(PUCHAR)data,(ULONG)bytes,out,32)<0)abort();
}
int main(int argc,char **argv){
    if(argc!=2)return 2;const char *names[]={"rtld","main","subsdk0","subsdk1","subsdk2","sdk"};
    Nx2Module modules[6];void *memory[6]={0};Nx2Group group={0};group.modules=modules;group.count=6;group.tls_resolver=1;int result=1;
    for(unsigned i=0;i<6;i++){
        char path[1024],error[160];size_t image,bss;snprintf(path,sizeof(path),"%s/%s",argv[1],names[i]);
        if(!nx2_prepare_nso(path,NULL,hash,&memory[i],&image,&bss,error)){printf("PREP FAIL %s %s\n",names[i],error);goto finish;}
        int rc=nx2_module_inspect(memory[i],image+bss,&modules[i]);printf("PREP %s bytes=%zu bss=%zu parse=%s\n",names[i],image,bss,nx2_module_error(rc));if(rc)goto finish;
    }
    Nx2LinkStats stats;int rc=nx2_group_validate(&group,&stats);
    printf("VALIDATE %s abs=%llu global=%llu jump=%llu relative=%llu relr=%llu tls=%llu unknown_tls=%u missing=%s\n",nx2_module_error(rc),(unsigned long long)stats.abs64,(unsigned long long)stats.global,(unsigned long long)stats.jump,(unsigned long long)stats.relative,(unsigned long long)stats.relr,(unsigned long long)stats.tls,stats.tls_unknown,stats.missing);
    result=rc==NX2_TLS_LAYOUT&&stats.tls_unknown==1&&stats.relr>0&&!stats.missing[0]?0:1;
finish:for(unsigned i=0;i<6;i++)nx2_free_pages(memory[i]);return result;
}
