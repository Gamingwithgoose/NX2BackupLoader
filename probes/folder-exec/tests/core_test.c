#include "../source/module.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
static int failures,checks;
#define CHECK(label,condition) do{checks++;if(!(condition)){printf("FAIL %s\n",label);failures++;}else printf("PASS %s\n",label);}while(0)
int main(int argc,char **argv){
    if(argc!=2)return 2;FILE *f=fopen(argv[1],"rb");if(!f)return 2;
    fseek(f,0,SEEK_END);long bytes=ftell(f);rewind(f);unsigned char *p=malloc(bytes),*copy=malloc(bytes);
    if(!p||!copy||fread(p,1,bytes,f)!=(size_t)bytes)return 2;fclose(f);memcpy(copy,p,bytes);
    Nx2Module m;CHECK("valid mapping inspected",nx2_module_inspect(p,bytes,&m)==NX2_OK);
    uintptr_t entry;CHECK("function found",nx2_module_function(&m,"nx2_module_entry",&entry)==NX2_OK);
    CHECK("entry lies in code",entry>=(uintptr_t)m.guest&&entry<(uintptr_t)m.guest+m.file_size[0]);
    CHECK("relocations applied",nx2_module_relocate(&m)==NX2_OK);
    uint64_t offset;memcpy(&offset,m.guest+m.rela,8);uint64_t target;memcpy(&target,m.guest+offset,8);
    CHECK("GOT points at mapped writable data",target==(uintptr_t)m.guest+m.address[2]);
    CHECK("missing entry rejected",nx2_module_function(&m,"absent",&entry)==NX2_MISSING_SYMBOL);
    memcpy(p,copy,bytes);p[0x180]='X';CHECK("unknown envelope rejected",nx2_module_inspect(p,bytes,&m)==NX2_BAD_IMAGE);
    memcpy(p,copy,bytes);CHECK("truncated mapping rejected",nx2_module_inspect(p,bytes-1,&m)==NX2_BAD_IMAGE);
    memcpy(p,copy,bytes);nx2_module_inspect(p,bytes,&m);uint32_t unsupported=1031;memcpy(m.guest+m.rela+8,&unsupported,4);
    CHECK("TLS relocation rejected before execution",nx2_module_relocate(&m)==NX2_UNSUPPORTED);
    memcpy(p,copy,bytes);nx2_module_inspect(p,bytes,&m);uint64_t code=0;memcpy(m.guest+m.rela,&code,8);
    CHECK("relocation cannot write code",nx2_module_relocate(&m)==NX2_BAD_RELOCATION);
    memcpy(p,copy,bytes);nx2_module_inspect(p,bytes,&m);uint32_t invalid=0xffffffff;memcpy(m.guest+m.rela+12,&invalid,4);
    CHECK("out of range symbol rejected",nx2_module_relocate(&m)==NX2_BAD_SYMBOL);
    free(p);free(copy);printf("%d checks, %d failures\n",checks,failures);return failures?1:0;
}
