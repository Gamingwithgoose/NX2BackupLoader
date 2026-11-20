#include "../source/module.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
static int checks,failures;
#define CHECK(label,condition) do{checks++;if(!(condition)){printf("FAIL %s\n",label);failures++;}else printf("PASS %s\n",label);}while(0)
typedef struct{unsigned char *bytes;size_t file,total;} Image;
static uint32_t word(const void *p){uint32_t v;memcpy(&v,p,4);return v;}
static uint64_t wide(const void *p){uint64_t v;memcpy(&v,p,8);return v;}
static Image load(const char *path){Image r={0};FILE *f=fopen(path,"rb");if(!f)exit(2);fseek(f,0,SEEK_END);r.file=ftell(f);rewind(f);unsigned char header[128];if(fread(header,1,128,f)!=128)exit(2);r.total=r.file+word(header+0x38);r.bytes=calloc(1,r.total);rewind(f);if(!r.bytes||fread(r.bytes,1,r.file,f)!=r.file)exit(2);fclose(f);return r;}
static void clear_hash(Image *image){uint64_t d=word(image->bytes+0x18c);for(unsigned i=0;i<64;i++){unsigned char *at=image->bytes+4096+d+16*i;uint64_t tag=wide(at);if(!tag)break;if(tag==4){tag=0x60000000;memcpy(at,&tag,8);return;}}exit(2);}
static uint64_t slot_for(Nx2Module *m,const char *name){for(uint64_t table=0;table<2;table++){uint64_t start=table?m->plt:m->rela,size=table?m->plt_size:m->rela_size;for(uint64_t i=0;i<size;i+=24){unsigned char *r=m->guest+start+i;uint64_t info=wide(r+8),symbol=info>>32;unsigned char *s=m->guest+m->symbols+symbol*24;const char *n=(const char *)(m->guest+m->strings+word(s));if(!strcmp(n,name))return wide(r);}}return UINT64_MAX;}
int main(int argc,char **argv){
    if(argc!=3)return 2;Image a=load(argv[1]),b=load(argv[2]);Nx2Module m[2];Nx2Group g={0};g.modules=m;g.count=2;g.tls_resolver=0x1000;Nx2LinkStats stats;
    CHECK("provider parse",nx2_module_inspect(a.bytes,a.total,&m[0])==0);CHECK("client parse with BSS",nx2_module_inspect(b.bytes,b.total,&m[1])==0&&b.total>b.file);
    Nx2Group isolated={0};isolated.modules=&m[1];isolated.count=1;isolated.tls_resolver=0x1000;
    CHECK("missing provider rejected",nx2_group_validate(&isolated,&stats)==NX2_MISSING_SYMBOL&&stats.missing[0]);
    CHECK("group validates",nx2_group_validate(&g,&stats)==0);CHECK("TLS descriptors found",stats.tls>=2);CHECK("RELR bitmap found",stats.relr>=2);
    g.tls_resolver=0;CHECK("missing TLS resolver rejected",nx2_group_validate(&g,&stats)==NX2_TLS_LAYOUT);g.tls_resolver=0x1000;
    CHECK("group relocates",nx2_group_relocate(&g,&stats)==0);
    uintptr_t provider=0;CHECK("provider function lookup",nx2_module_function(&m[0],"nx2_provider",&provider)==0);
    uint64_t jump=slot_for(&m[1],"nx2_provider");CHECK("cross-module PLT points to provider",jump!=UINT64_MAX&&wide(m[1].guest+jump)==provider);
    uint64_t weak=slot_for(&m[1],"nx2_missing_weak");CHECK("undefined weak remains null",weak!=UINT64_MAX&&!wide(m[1].guest+weak));
    uint64_t tls=slot_for(&m[0],"nx2_tls_counter");CHECK("TLS resolver installed",tls!=UINT64_MAX&&wide(m[0].guest+tls)==0x1000);
    Nx2TlsArg *arg=(Nx2TlsArg *)(uintptr_t)wide(m[0].guest+tls+8);CHECK("TLS points at provider block",arg->module==0&&arg->offset==0);
    uintptr_t callbacks[8];size_t count=0;CHECK("constructor list relocated",nx2_module_callbacks(&g,1,0,callbacks,8,&count)==0&&count==1);
    CHECK("destructor list relocated",nx2_module_callbacks(&g,1,1,callbacks,8,&count)==0&&count==1);
    CHECK("double relocation rejected",nx2_group_relocate(&g,&stats)==NX2_ALREADY_LINKED);
    uint64_t bad=1;memcpy(m[1].guest+m[1].init_array,&bad,8);CHECK("invalid constructor target rejected",nx2_module_callbacks(&g,1,0,callbacks,8,&count)==NX2_BAD_SYMBOL);
    free(a.bytes);free(b.bytes);
    a=load(argv[1]);b=load(argv[2]);clear_hash(&a);clear_hash(&b);g.tls_used=0;
    CHECK("provider GNU-only hash parse",nx2_module_inspect(a.bytes,a.total,&m[0])==0&&!m[0].hash&&m[0].gnu_hash);
    CHECK("client GNU-only hash parse",nx2_module_inspect(b.bytes,b.total,&m[1])==0&&!m[1].hash&&m[1].gnu_hash);
    CHECK("GNU-only cross-module linking",nx2_group_validate(&g,&stats)==0);
    uint64_t saved=wide(m[1].guest+m[1].relr);bad=3;memcpy(m[1].guest+m[1].relr,&bad,8);CHECK("RELR bitmap without address rejected",nx2_group_validate(&g,&stats)==NX2_BAD_RELOCATION);
    bad=0;memcpy(m[1].guest+m[1].relr,&bad,8);CHECK("RELR cannot target text",nx2_group_validate(&g,&stats)==NX2_BAD_RELOCATION);memcpy(m[1].guest+m[1].relr,&saved,8);
    m[0].tls_memory_size=0;CHECK("unknown TLS template blocks linking",nx2_group_validate(&g,&stats)==NX2_TLS_LAYOUT&&stats.tls_unknown>=2);
    free(a.bytes);free(b.bytes);
    a=load(argv[1]);nx2_module_inspect(a.bytes,a.total,&m[0]);g.count=1;
    unsigned char *descriptor_rela=NULL;
    for(unsigned table=0;table<2;table++){uint64_t start=table?m[0].plt:m[0].rela,bytes=table?m[0].plt_size:m[0].rela_size;for(uint64_t p=0;p<bytes;p+=24)if((uint32_t)wide(m[0].guest+start+p+8)==1031)descriptor_rela=m[0].guest+start+p;}
    if(!descriptor_rela)return 2;
    uint64_t info=1031;memcpy(descriptor_rela+8,&info,8);m[0].tls_memory_size=0;
    Nx2PreservedTls policy={0,wide(descriptor_rela)};g.preserved_tls=&policy;g.preserved_tls_count=1;
    CHECK("explicit anonymous preservation validates",nx2_group_validate(&g,&stats)==0&&stats.tls_preserved==1&&!stats.tls_unknown);
    policy.offset+=16;CHECK("policy cannot cover another offset",nx2_group_validate(&g,&stats)==NX2_TLS_LAYOUT);policy.offset-=16;
    bad=1;memcpy(m[0].guest+policy.offset,&bad,8);CHECK("initialized descriptor cannot be preserved",nx2_group_validate(&g,&stats)==NX2_BAD_RELOCATION);bad=0;memcpy(m[0].guest+policy.offset,&bad,8);
    info=1031|(1ULL<<32);memcpy(descriptor_rela+8,&info,8);CHECK("named descriptor cannot be preserved",nx2_group_validate(&g,&stats)!=0);info=1031;memcpy(descriptor_rela+8,&info,8);
    bad=1;memcpy(descriptor_rela+16,&bad,8);CHECK("nonzero TLS addend cannot be preserved",nx2_group_validate(&g,&stats)==NX2_BAD_RELOCATION);bad=0;memcpy(descriptor_rela+16,&bad,8);
    m[0].tls_memory_size=8;CHECK("template-backed TLS cannot be preserved",nx2_group_validate(&g,&stats)==NX2_BAD_RELOCATION);m[0].tls_memory_size=0;
    policy.module=1;CHECK("out-of-range preservation module rejected",nx2_group_validate(&g,&stats)==NX2_BAD_IMAGE);policy.module=0;
    g.preserved_tls=NULL;CHECK("missing preservation list rejected",nx2_group_validate(&g,&stats)==NX2_BAD_IMAGE);g.preserved_tls=&policy;
    CHECK("preserved descriptor links",nx2_group_relocate(&g,&stats)==0&&stats.tls_preserved==1);
    CHECK("preserved descriptor stays zero",!wide(m[0].guest+policy.offset)&&!wide(m[0].guest+policy.offset+8));
    free(a.bytes);printf("%d checks, %d failures\n",checks,failures);return failures?1:0;
}
