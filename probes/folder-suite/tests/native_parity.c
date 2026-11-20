#include "../source/module.h"
#include "../source/nso.h"
#include "../source/yoshi_hashes.h"
#include <windows.h>
#include <bcrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void hash(unsigned char out[32],const void *d,size_t n){if(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,NULL,0,(PUCHAR)d,(ULONG)n,out,32)<0)abort();}
static unsigned long long get(const void *p){unsigned long long v;memcpy(&v,p,8);return v;}
static unsigned long long count,bad;
static void compare(Nx2Module *m,const unsigned char *expected,unsigned long long off,unsigned type){
 unsigned long long a=get(m->guest+off),b=get(expected+off);count++;
 if(a!=b){if(bad<20)printf("DIFF type=%u offset=%llx ours=%llx native=%llx\n",type,off,a,b);bad++;}
}
int main(int argc,char **argv){
 if(argc!=3&&argc!=4)return 2;const char *names[]={"rtld","main","subsdk0","subsdk1","subsdk2","sdk"};
 uintptr_t bases[]={0x80b1000,0x82b1000,0x122b1000,0x15901000,0x16d81000,0x16fc1000};
 Nx2Module modules[6];Nx2Group g={0};g.modules=modules;g.count=6;
 for(unsigned i=0;i<6;i++){
  char path[2048],error[160];void *source;size_t bytes,bss;snprintf(path,sizeof(path),"%s/%s",argv[1],names[i]);
  if(!nx2_prepare_nso(path,yoshi_files[i].sha,hash,&source,&bytes,&bss,error)){puts(error);return 1;}
  void *fixed=VirtualAlloc((void *)(bases[i]-4096),bytes+bss,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);if(fixed!=(void *)(bases[i]-4096)){puts("fixed allocation unavailable");return 1;}
  memcpy(fixed,source,bytes+bss);nx2_free_pages(source);int rc=nx2_module_inspect(fixed,bytes+bss,&modules[i]);if(rc){puts(nx2_module_error(rc));return 1;}
 }
 Nx2LinkStats stats;
 if(nx2_group_validate(&g,&stats)!=NX2_TLS_LAYOUT||stats.tls_unknown!=1){puts("strict TLS policy changed");return 1;}
 const Nx2PreservedTls policy={2,1152944};g.preserved_tls=&policy;g.preserved_tls_count=1;
 int rc=nx2_group_relocate(&g,&stats);if(rc||stats.tls_preserved!=1){puts(nx2_module_error(rc));return 1;}
 for(unsigned i=0;i<6;i++){
  Nx2Module *m=&modules[i];char path[2048];snprintf(path,sizeof(path),"%s/%s.native-linked.bin",argv[2],names[i]);FILE *f=fopen(path,"rb");if(!f)return 1;
  size_t size=m->total-4096;unsigned char *expected=malloc(size);if(fread(expected,1,size,f)!=size)return 1;fclose(f);uint64_t before=count,previous=bad;
  uint64_t starts[]={m->rela,m->plt},sizes[]={m->rela_size,m->plt_size};
  for(unsigned t=0;t<2;t++)for(uint64_t p=0;p<sizes[t];p+=24){unsigned char *r=m->guest+starts[t]+p;unsigned type=(uint32_t)get(r+8);if(type)compare(m,expected,get(r),type);}
  uint64_t cursor=0;for(uint64_t p=0;p<m->relr_size;p+=8){uint64_t v=get(m->guest+m->relr+p);if(!(v&1)){cursor=v;compare(m,expected,cursor,0x24);cursor+=8;}else{for(unsigned bit=1;bit<64;bit++)if(v&(1ULL<<bit))compare(m,expected,cursor+(bit-1)*8,0x24);cursor+=63*8;}}
  printf("PARITY %s targets=%llu differences=%llu\n",names[i],count-before,bad-previous);free(expected);
 }
 if(argc==4){char path[2048];snprintf(path,sizeof(path),"%s/sdk.ours-linked.bin",argv[3]);FILE *f=fopen(path,"wb");if(!f)return 1;size_t n=modules[5].total-4096;if(fwrite(modules[5].guest,1,n,f)!=n||fclose(f))return 1;}
 printf("TOTAL targets=%llu differences=%llu\n",count,bad);return bad?1:0;
}
