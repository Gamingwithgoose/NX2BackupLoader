#include "module.h"
#include <string.h>
#define PREFIX 4096U
static uint32_t u32(const void *p) { uint32_t n;memcpy(&n,p,4);return n; }
static uint64_t u64(const void *p) { uint64_t n;memcpy(&n,p,8);return n; }
static int region(Nx2Module *m,uint64_t at,uint64_t size,int write) {
    unsigned first=write?2:0;
    for(unsigned i=first;i<3;i++) {
        uint64_t length=i==2?m->memory_size[i]:m->file_size[i];
        if(at>=m->address[i] && at-m->address[i]<=length && size<=length-(at-m->address[i]))return 1;
    }
    return 0;
}
static const unsigned char *at(Nx2Module *m,uint64_t address,uint64_t size) {
    return region(m,address,size,0)?m->guest+(size_t)address:NULL;
}
int nx2_module_inspect(void *image,size_t bytes,Nx2Module *out) {
    if(!image||!out||bytes<4*4096||bytes>512U*1024*1024)return NX2_BAD_IMAGE;
    unsigned char *p=image;
    if(memcmp(p+0x10,"NRO0",4)||memcmp(p+0x180,"NX2MOD01",8)||u32(p+0x188)!=1)return NX2_BAD_IMAGE;
    uint64_t size=u32(p+0x18),bss=u32(p+0x38);
    if(size+bss!=bytes||(size&4095)||(bss&4095)||u32(p+0x20)!=0)return NX2_BAD_IMAGE;
    uint64_t text=u32(p+0x24),ro=u32(p+0x28),rosize=u32(p+0x2c),data=u32(p+0x30),datasize=u32(p+0x34);
    if(text!=ro||ro+rosize!=data||data+datasize!=size||text<PREFIX||!rosize||!datasize||((text|ro|rosize|data|datasize)&4095))return NX2_BAD_IMAGE;
    Nx2Module m={0};m.image=p;m.guest=p+PREFIX;m.total=bytes;
    for(unsigned i=0;i<3;i++) {
        m.address[i]=u32(p+0x190+i*12);m.file_size[i]=u32(p+0x194+i*12);m.memory_size[i]=u32(p+0x198+i*12);
        if(m.file_size[i]>m.memory_size[i]||(uint64_t)PREFIX+m.address[i]+m.memory_size[i]>bytes)return NX2_BAD_IMAGE;
        if(i&&(uint64_t)m.address[i-1]+m.memory_size[i-1]>m.address[i])return NX2_BAD_IMAGE;
    }
    if(m.address[0]||PREFIX+(uint64_t)m.address[1]!=ro||PREFIX+(uint64_t)m.address[2]!=data)return NX2_BAD_IMAGE;
    if(m.file_size[0]>(uint64_t)text-PREFIX||m.file_size[1]>rosize||m.file_size[2]>datasize)return NX2_BAD_IMAGE;
    uint64_t dynamic=u32(p+0x18c),hash=0,syment=24,relaent=24,plttype=7;int ended=0;
    for(unsigned i=0;i<4096;i++) {
        const unsigned char *d=at(&m,dynamic+i*16ULL,16);if(!d)return NX2_BAD_DYNAMIC;
        uint64_t tag=u64(d),value=u64(d+8);if(!tag){ended=1;break;}
        switch(tag) {
        case 4:hash=value;break;
        case 5:m.strings=value;break;
        case 6:m.symbols=value;break;
        case 10:m.string_size=value;break;
        case 11:syment=value;break;
        case 7:m.rela=value;break;
        case 8:m.rela_size=value;break;
        case 9:relaent=value;break;
        case 23:m.plt=value;break;
        case 2:m.plt_size=value;break;
        case 20:plttype=value;break;
        case 17:case 18:case 35:case 36:case 37:return NX2_UNSUPPORTED;
        default:break;
        }
    }
    if(!ended||syment!=24||relaent!=24||plttype!=7||m.rela_size%24||m.plt_size%24)return NX2_BAD_DYNAMIC;
    const unsigned char *h=at(&m,hash,8);if(!h||!hash)return NX2_BAD_DYNAMIC;
    m.symbol_count=u32(h+4);
    if(!m.symbol_count||m.symbol_count>65536||u32(h)>65536||!at(&m,hash,8+4ULL*(u32(h)+m.symbol_count)))return NX2_BAD_DYNAMIC;
    if(!m.string_size||m.string_size>16*1024*1024||!at(&m,m.strings,m.string_size)||!at(&m,m.symbols,m.symbol_count*24))return NX2_BAD_DYNAMIC;
    if((m.rela_size&&!at(&m,m.rela,m.rela_size))||(m.plt_size&&!at(&m,m.plt,m.plt_size)))return NX2_BAD_DYNAMIC;
    *out=m;return NX2_OK;
}
static int symbol(Nx2Module *m,uint64_t index,const char **name,uint64_t *value,unsigned *type) {
    if(index>=m->symbol_count)return NX2_BAD_SYMBOL;
    const unsigned char *s=at(m,m->symbols+index*24,24);uint32_t offset=u32(s);
    if(offset>=m->string_size)return NX2_BAD_SYMBOL;
    *name=(const char *)(m->guest+m->strings+offset);
    if(!memchr(*name,0,(size_t)(m->string_size-offset)))return NX2_BAD_SYMBOL;
    *type=s[4]&15;
    uint16_t section;memcpy(&section,s+6,2);
    if(*type==6)return NX2_UNSUPPORTED;
    if(!section) {
        if(index==0||(s[4]>>4)==2){*value=0;return NX2_OK;}
        return NX2_MISSING_SYMBOL;
    }
    uint64_t v=u64(s+8);
    if(section==0xfff1){*value=v;return NX2_OK;}
    if(!region(m,v,1,0))return NX2_BAD_SYMBOL;
    *value=(uintptr_t)m->guest+v;return NX2_OK;
}
static int relocate_table(Nx2Module *m,uint64_t start,uint64_t bytes,int apply) {
    for(uint64_t pos=0;pos<bytes;pos+=24) {
        const unsigned char *r=at(m,start+pos,24);uint64_t offset=u64(r),info=u64(r+8),addend=u64(r+16),value=0;
        uint32_t type=(uint32_t)info;
        if(type==0)continue;
        if(type!=257&&type!=1025&&type!=1026&&type!=1027)return NX2_UNSUPPORTED;
        if(!region(m,offset,8,1)||(offset&7))return NX2_BAD_RELOCATION;
        if(type==1027) {if(info>>32)return NX2_BAD_RELOCATION;value=(uintptr_t)m->guest;}
        else {const char *name;unsigned stype;int rc=symbol(m,info>>32,&name,&value,&stype);if(rc)return rc;}
        value+=addend;
        if(apply)memcpy(m->guest+offset,&value,8);
    }
    return NX2_OK;
}
int nx2_module_relocate(Nx2Module *m) {
    /* Validate both tables before changing any target word. */
    int rc=relocate_table(m,m->rela,m->rela_size,0);if(rc)return rc;
    rc=relocate_table(m,m->plt,m->plt_size,0);if(rc)return rc;
    rc=relocate_table(m,m->rela,m->rela_size,1);if(rc)return rc;
    return relocate_table(m,m->plt,m->plt_size,1);
}
int nx2_module_function(Nx2Module *m,const char *wanted,uintptr_t *address) {
    for(uint64_t i=1;i<m->symbol_count;i++) {
        const char *name;uint64_t value;unsigned type;int rc=symbol(m,i,&name,&value,&type);
        if(rc==NX2_MISSING_SYMBOL||rc==NX2_UNSUPPORTED)continue;
        if(rc)return rc;
        if(!strcmp(name,wanted)) {
            if(type!=2||value<(uintptr_t)m->guest||value-(uintptr_t)m->guest>=m->file_size[0])return NX2_BAD_SYMBOL;
            *address=(uintptr_t)value;return NX2_OK;
        }
    }
    return NX2_MISSING_SYMBOL;
}
const char *nx2_module_error(int code) {
    static const char *names[]={"OK","bad mapping envelope","bad dynamic metadata","bad symbol","invalid relocation target","unsupported relocation/format","unresolved symbol"};
    return code>=0&&code<7?names[code]:"unknown error";
}
