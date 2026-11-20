#include "module.h"
#include <string.h>
#include <stdio.h>
#define PREFIX 4096U
static uint32_t u32(const void *p){uint32_t n;memcpy(&n,p,4);return n;}
static uint64_t u64(const void *p){uint64_t n;memcpy(&n,p,8);return n;}
static int region(const Nx2Module *m,uint64_t off,uint64_t size,int write){
    for(unsigned i=write?2:0;i<3;i++){
        uint64_t n=i==2?m->memory_size[i]:m->file_size[i];
        if(off>=m->address[i]&&off-m->address[i]<=n&&size<=n-(off-m->address[i]))return 1;
    }return 0;
}
static const unsigned char *at(const Nx2Module *m,uint64_t off,uint64_t size){return region(m,off,size,0)?m->guest+(size_t)off:NULL;}
static int add_ok(uint64_t a,uint64_t b,uint64_t *v){*v=a+b;return *v>=a;}
int nx2_module_inspect(void *image,size_t bytes,Nx2Module *out){
    if(!image||!out||bytes<4*4096||bytes>512U*1024*1024)return NX2_BAD_IMAGE;
    unsigned char *p=image;
    if(memcmp(p+0x10,"NRO0",4)||memcmp(p+0x180,"NX2MOD01",8)||u32(p+0x188)!=1)return NX2_BAD_IMAGE;
    uint64_t size=u32(p+0x18),bss=u32(p+0x38);
    if(size+bss!=bytes||(size&4095)||(bss&4095)||u32(p+0x20))return NX2_BAD_IMAGE;
    uint64_t text=u32(p+0x24),ro=u32(p+0x28),rs=u32(p+0x2c),data=u32(p+0x30),ds=u32(p+0x34);
    if(text!=ro||ro+rs!=data||data+ds!=size||text<PREFIX||!rs||!ds||((text|ro|rs|data|ds)&4095))return NX2_BAD_IMAGE;
    Nx2Module m={0};m.image=p;m.guest=p+PREFIX;m.total=bytes;
    for(unsigned i=0;i<3;i++){
        m.address[i]=u32(p+0x190+i*12);m.file_size[i]=u32(p+0x194+i*12);m.memory_size[i]=u32(p+0x198+i*12);
        if(m.file_size[i]>m.memory_size[i]||(uint64_t)PREFIX+m.address[i]+m.memory_size[i]>bytes)return NX2_BAD_IMAGE;
        if(i&&(uint64_t)m.address[i-1]+m.memory_size[i-1]>m.address[i])return NX2_BAD_IMAGE;
    }
    if(m.address[0]||PREFIX+(uint64_t)m.address[1]!=ro||PREFIX+(uint64_t)m.address[2]!=data)return NX2_BAD_IMAGE;
    if(m.file_size[0]>text-PREFIX||m.file_size[1]>rs||m.file_size[2]>ds)return NX2_BAD_IMAGE;
    m.tls_address=u32(p+0x1c0);m.tls_file_size=u32(p+0x1c4);m.tls_memory_size=u32(p+0x1c8);m.tls_alignment=u32(p+0x1cc);
    if(m.tls_memory_size){
        if(m.tls_file_size>m.tls_memory_size||m.tls_memory_size>65536||!m.tls_alignment||(m.tls_alignment&(m.tls_alignment-1))||m.tls_alignment>4096)return NX2_BAD_IMAGE;
        if(m.tls_file_size&&!at(&m,m.tls_address,m.tls_file_size))return NX2_BAD_IMAGE;
    }else if(m.tls_file_size||m.tls_alignment||m.tls_address)return NX2_BAD_IMAGE;
    uint64_t dynamic=u32(p+0x18c),syment=24,relaent=24,relrent=8,plt_type=7;int ended=0;
    for(unsigned i=0;i<4096;i++){
        const unsigned char *d=at(&m,dynamic+16ULL*i,16);if(!d)return NX2_BAD_DYNAMIC;
        uint64_t tag=u64(d),v=u64(d+8);if(!tag){ended=1;break;}
        switch(tag){
        case 4:m.hash=v;break;case 0x6ffffef5:m.gnu_hash=v;break;
        case 5:m.strings=v;break;case 6:m.symbols=v;break;case 10:m.string_size=v;break;case 11:syment=v;break;
        case 7:m.rela=v;break;case 8:m.rela_size=v;break;case 9:relaent=v;break;
        case 23:m.plt=v;break;case 2:m.plt_size=v;break;case 20:plt_type=v;break;
        case 35:m.relr_size=v;break;case 36:m.relr=v;break;case 37:relrent=v;break;
        case 12:m.init=v;break;case 13:m.fini=v;break;case 25:m.init_array=v;break;case 27:m.init_size=v;break;
        case 26:m.fini_array=v;break;case 28:m.fini_size=v;break;
        case 17:case 18:if(v)return NX2_UNSUPPORTED;break;
        default:break;
        }
    }
    if(!ended||syment!=24||relaent!=24||relrent!=8||plt_type!=7||m.rela_size%24||m.plt_size%24||m.relr_size%8||m.init_size%8||m.fini_size%8)return NX2_BAD_DYNAMIC;
    if(m.hash){
        const unsigned char *h=at(&m,m.hash,8);if(!h)return NX2_BAD_DYNAMIC;
        uint32_t buckets=u32(h);m.symbol_count=u32(h+4);
        if(!buckets||buckets>65536||!m.symbol_count||m.symbol_count>65536||!at(&m,m.hash,8+4ULL*(buckets+m.symbol_count)))return NX2_BAD_DYNAMIC;
    }else if(m.gnu_hash){
        const unsigned char *h=at(&m,m.gnu_hash,16);if(!h)return NX2_BAD_DYNAMIC;
        uint32_t buckets=u32(h),first=u32(h+4),bloom=u32(h+8),highest=0;
        if(!buckets||buckets>65536||!bloom||bloom>65536||first>65536)return NX2_BAD_DYNAMIC;
        uint64_t bucket=m.gnu_hash+16+8ULL*bloom,chain=bucket+4ULL*buckets;
        if(!at(&m,m.gnu_hash,16+8ULL*bloom+4ULL*buckets))return NX2_BAD_DYNAMIC;
        for(unsigned i=0;i<buckets;i++){const unsigned char *b=at(&m,bucket+4ULL*i,4);if(u32(b)>highest)highest=u32(b);}
        m.symbol_count=first;
        if(highest){
            if(highest<first||highest>=65536)return NX2_BAD_DYNAMIC;
            for(uint64_t index=highest;;index++){
                if(index>=65536)return NX2_BAD_DYNAMIC;
                const unsigned char *c=at(&m,chain+4*(index-first),4);if(!c)return NX2_BAD_DYNAMIC;
                if(u32(c)&1){m.symbol_count=index+1;break;}
            }
        }
    }else return NX2_BAD_DYNAMIC;
    if(!m.symbol_count||!m.string_size||m.string_size>16*1024*1024||!at(&m,m.strings,m.string_size)||!at(&m,m.symbols,m.symbol_count*24))return NX2_BAD_DYNAMIC;
    if((m.rela_size&&!at(&m,m.rela,m.rela_size))||(m.plt_size&&!at(&m,m.plt,m.plt_size))||(m.relr_size&&!at(&m,m.relr,m.relr_size)))return NX2_BAD_DYNAMIC;
    if((m.init_size&&!at(&m,m.init_array,m.init_size))||(m.fini_size&&!at(&m,m.fini_array,m.fini_size)))return NX2_BAD_DYNAMIC;
    *out=m;return NX2_OK;
}
typedef struct {const char *name;uint64_t value,size;unsigned binding,type,visibility,section;} Symbol;
static int get_symbol(Nx2Module *m,uint64_t index,Symbol *s){
    if(index>=m->symbol_count)return NX2_BAD_SYMBOL;
    const unsigned char *p=at(m,m->symbols+index*24,24);uint32_t name=u32(p);
    if(name>=m->string_size)return NX2_BAD_SYMBOL;
    s->name=(const char *)(m->guest+m->strings+name);
    if(!memchr(s->name,0,(size_t)(m->string_size-name)))return NX2_BAD_SYMBOL;
    s->binding=p[4]>>4;s->type=p[4]&15;s->visibility=p[5]&3;uint16_t section;memcpy(&section,p+6,2);s->section=section;s->value=u64(p+8);s->size=u64(p+16);
    return NX2_OK;
}
static uint32_t sysv_hash(const char *name){uint32_t h=0;for(;*name;name++){h=(h<<4)+(unsigned char)*name;uint32_t top=h&0xf0000000;if(top)h^=top>>24;h&=~top;}return h;}
static uint32_t gnu_hash(const char *name){uint32_t h=5381;for(;*name;name++)h=h*33+(unsigned char)*name;return h;}
static int named_symbol(Nx2Module *m,const char *name,Symbol *out){
    uint64_t index=0,chain=0,first=0;uint32_t hash=0;int gnu=!m->hash;
    if(!gnu){const unsigned char *h=at(m,m->hash,8);uint32_t n=u32(h);index=u32(at(m,m->hash+8+4ULL*(sysv_hash(name)%n),4));chain=m->hash+8+4ULL*n;}
    else {const unsigned char *h=at(m,m->gnu_hash,16);uint32_t n=u32(h),bloom=u32(h+8);first=u32(h+4);hash=gnu_hash(name);uint64_t bucket=m->gnu_hash+16+8ULL*bloom;index=u32(at(m,bucket+4ULL*(hash%n),4));chain=bucket+4ULL*n;}
    if(!index)return NX2_MISSING_SYMBOL;
    for(uint64_t steps=0;steps<m->symbol_count;steps++){
        if(index>=m->symbol_count||(gnu&&index<first))return NX2_BAD_SYMBOL;
        uint32_t link=0;
        if(gnu){const unsigned char *p=at(m,chain+4*(index-first),4);if(!p)return NX2_BAD_SYMBOL;link=u32(p);}
        if(!gnu||((link^hash)&~1U)==0){Symbol s;int rc=get_symbol(m,index,&s);if(rc)return rc;if(!strcmp(s.name,name)&&s.section&&s.binding&&s.visibility!=2&&s.visibility!=1){*out=s;return NX2_OK;}}
        if(gnu){if(link&1)break;index++;}
        else {const unsigned char *p=at(m,chain+4*index,4);if(!p)return NX2_BAD_SYMBOL;index=u32(p);if(!index)break;}
    }return NX2_MISSING_SYMBOL;
}
static int resolve(Nx2Group *g,size_t owner,uint64_t index,Symbol *result,size_t *definition,uintptr_t *value){
    Symbol request;int rc=get_symbol(&g->modules[owner],index,&request);if(rc)return rc;
    *result=request;*definition=owner;
    if(index==0){*value=0;return NX2_OK;}
    int own=request.section&&(!request.binding||request.visibility==2||request.visibility==3);
    if(!own){
        int found=0;Symbol weak={0};size_t weak_owner=0;
        for(size_t i=0;i<g->count;i++){
            Symbol s;rc=named_symbol(&g->modules[i],request.name,&s);
            if(rc!=NX2_OK&&rc!=NX2_MISSING_SYMBOL)return rc;
            if(!rc){if(s.binding==1){*result=s;*definition=i;found=1;break;}if(!weak.section){weak=s;weak_owner=i;}}
        }
        if(!found&&weak.section){*result=weak;*definition=weak_owner;found=1;}
        if(!found){
            for(size_t i=0;i<g->external_count;i++)if(!strcmp(request.name,g->external[i].name)){*value=g->external[i].address;return request.type==6?NX2_UNSUPPORTED:NX2_OK;}
            if(!request.section&&request.binding==2){*value=0;return NX2_OK;}
            if(!request.section)return NX2_MISSING_SYMBOL;
        }
    }
    Nx2Module *m=&g->modules[*definition];
    if(result->type==6){*value=result->value;return NX2_OK;}
    if(result->section==0xfff1){*value=result->value;return NX2_OK;}
    if(!region(m,result->value,1,0))return NX2_BAD_SYMBOL;
    *value=(uintptr_t)m->guest+result->value;return NX2_OK;
}
static int rela_table(Nx2Group *g,size_t owner,uint64_t start,uint64_t bytes,int apply,Nx2LinkStats *stats,size_t *tls_needed){
    Nx2Module *m=&g->modules[owner];int tls_unknown=0;
    for(uint64_t pos=0;pos<bytes;pos+=24){
        const unsigned char *r=at(m,start+pos,24);uint64_t offset=u64(r),info=u64(r+8),addend=u64(r+16);uint32_t type=(uint32_t)info;
        if(!type)continue;
        if(type!=257&&type!=1025&&type!=1026&&type!=1027&&type!=1031)return NX2_UNSUPPORTED;
        if((offset&7)||!region(m,offset,type==1031?16:8,1))return NX2_BAD_RELOCATION;
        uintptr_t value=0;Symbol s={0};size_t definition=owner;
        if(type==1027){if(info>>32)return NX2_BAD_RELOCATION;value=(uintptr_t)m->guest;}
        else {int rc=resolve(g,owner,info>>32,&s,&definition,&value);if(rc){if(stats&&rc==NX2_MISSING_SYMBOL){Symbol miss;get_symbol(m,info>>32,&miss);snprintf(stats->missing,sizeof(stats->missing),"%s",miss.name);}return rc;}}
        if(!apply&&stats){if(type==257)stats->abs64++;else if(type==1025)stats->global++;else if(type==1026)stats->jump++;else if(type==1027)stats->relative++;else stats->tls++;}
        if(type==1031){
            int preserve=0;
            for(size_t j=0;j<g->preserved_tls_count;j++)if(g->preserved_tls[j].module==owner&&g->preserved_tls[j].offset==offset)preserve=1;
            if(preserve){
                /* The observed original rtld leaves this exact anonymous pair
                 * untouched. Reject named, initialized or template-backed TLS. */
                if((info>>32)||addend||m->tls_memory_size||u64(m->guest+offset)||u64(m->guest+offset+8))return NX2_BAD_RELOCATION;
                if(stats&&!apply)stats->tls_preserved++;
                continue;
            }
            Nx2Module *provider=&g->modules[definition];uint64_t tls_offset=value+addend;
            if((info>>32)&&s.type!=6)return NX2_BAD_SYMBOL;
            if(!provider->tls_memory_size){tls_unknown=1;if(stats&&!apply)stats->tls_unknown++;continue;}
            if(tls_offset>=provider->tls_memory_size||s.size>provider->tls_memory_size-tls_offset||!g->tls_resolver)return NX2_TLS_LAYOUT;
            if(!apply){if(++*tls_needed>128)return NX2_TLS_LAYOUT;}
            else {if(g->tls_used>=128)return NX2_TLS_LAYOUT;Nx2TlsArg *arg=&g->tls_args[g->tls_used++];*arg=(Nx2TlsArg){(uint32_t)definition,0,tls_offset};uint64_t pair[2]={g->tls_resolver,(uintptr_t)arg};memcpy(m->guest+offset,pair,16);}
        }else {
            if(s.type==6)return NX2_UNSUPPORTED;
            uint64_t target=value+addend;if(apply)memcpy(m->guest+offset,&target,8);
        }
    }return tls_unknown?NX2_TLS_LAYOUT:NX2_OK;
}
static int relr_table(Nx2Module *m,int apply,Nx2LinkStats *stats){
    uint64_t cursor=0;int have_cursor=0;
    for(uint64_t pos=0;pos<m->relr_size;pos+=8){
        uint64_t value=u64(at(m,m->relr+pos,8));
        if(!(value&1)){
            if((value&7)||(have_cursor&&value<cursor)||!region(m,value,8,1))return NX2_BAD_RELOCATION;
            if(apply){uint64_t word=u64(m->guest+value)+(uintptr_t)m->guest;memcpy(m->guest+value,&word,8);}else if(stats)stats->relr++;
            if(!add_ok(value,8,&cursor))return NX2_BAD_RELOCATION;
            have_cursor=1;
        }else {
            if(!have_cursor)return NX2_BAD_RELOCATION;
            for(unsigned bit=1;bit<64;bit++)if(value&(1ULL<<bit)){
                uint64_t offset;if(!add_ok(cursor,8ULL*(bit-1),&offset)||!region(m,offset,8,1))return NX2_BAD_RELOCATION;
                if(apply){uint64_t word=u64(m->guest+offset)+(uintptr_t)m->guest;memcpy(m->guest+offset,&word,8);}else if(stats)stats->relr++;
            }
            if(!add_ok(cursor,63*8,&cursor))return NX2_BAD_RELOCATION;
        }
    }return NX2_OK;
}
int nx2_group_validate(Nx2Group *g,Nx2LinkStats *stats){
    if(!g||!g->modules||!g->count||g->count>16)return NX2_BAD_IMAGE;
    if(g->preserved_tls_count>16||(g->preserved_tls_count&&!g->preserved_tls))return NX2_BAD_IMAGE;
    for(size_t i=0;i<g->preserved_tls_count;i++){
        if(g->preserved_tls[i].module>=g->count)return NX2_BAD_IMAGE;
        for(size_t j=0;j<i;j++)if(g->preserved_tls[j].module==g->preserved_tls[i].module&&g->preserved_tls[j].offset==g->preserved_tls[i].offset)return NX2_BAD_IMAGE;
    }
    if(stats)memset(stats,0,sizeof(*stats));
    size_t tls_needed=0;int pending=0;
    for(size_t i=0;i<g->count;i++){
        Nx2Module *m=&g->modules[i];if(m->linked)return NX2_ALREADY_LINKED;
        int rc=rela_table(g,i,m->rela,m->rela_size,0,stats,&tls_needed);if(rc==NX2_TLS_LAYOUT)pending=rc;else if(rc)return rc;
        rc=rela_table(g,i,m->plt,m->plt_size,0,stats,&tls_needed);if(rc==NX2_TLS_LAYOUT)pending=rc;else if(rc)return rc;
        rc=relr_table(m,0,stats);if(rc)return rc;
    }return pending;
}
int nx2_group_relocate(Nx2Group *g,Nx2LinkStats *stats){
    int rc=nx2_group_validate(g,stats);if(rc)return rc;size_t unused=0;g->tls_used=0;
    for(size_t i=0;i<g->count;i++){
        Nx2Module *m=&g->modules[i];rc=rela_table(g,i,m->rela,m->rela_size,1,NULL,&unused);if(rc)return rc;
        rc=rela_table(g,i,m->plt,m->plt_size,1,NULL,&unused);if(rc)return rc;
        rc=relr_table(m,1,NULL);if(rc)return rc;m->linked=1;
    }return NX2_OK;
}
int nx2_module_relocate(Nx2Module *m){Nx2Group g={0};g.modules=m;g.count=1;return nx2_group_relocate(&g,NULL);}
int nx2_module_function(Nx2Module *m,const char *name,uintptr_t *address){
    Symbol s;int rc=named_symbol(m,name,&s);if(rc)return rc;
    if(s.type!=2||s.value>=m->file_size[0])return NX2_BAD_SYMBOL;
    *address=(uintptr_t)m->guest+s.value;return NX2_OK;
}
int nx2_module_address(Nx2Module *m,const char *name,uintptr_t *address){
    Symbol s;int rc=named_symbol(m,name,&s);if(rc)return rc;
    if(!address||s.type==6||!at(m,s.value,1))return NX2_BAD_SYMBOL;
    *address=(uintptr_t)m->guest+s.value;return NX2_OK;
}
int nx2_module_callbacks(Nx2Group *g,size_t index,int final,uintptr_t *functions,size_t capacity,size_t *count){
    if(index>=g->count)return NX2_BAD_IMAGE;
    Nx2Module *m=&g->modules[index];if(!m->linked)return NX2_BAD_IMAGE;
    uint64_t direct=final?m->fini:m->init,array=final?m->fini_array:m->init_array,bytes=final?m->fini_size:m->init_size;
    size_t needed=(direct?1:0)+(size_t)(bytes/8);if(needed>capacity)return NX2_BAD_DYNAMIC;
    *count=0;if(direct&&!final)functions[(*count)++]=(uintptr_t)m->guest+direct;
    for(uint64_t at_off=0;at_off<bytes;at_off+=8){uint64_t off=final?bytes-8-at_off:at_off;uint64_t address=u64(at(m,array+off,8));if(address&&address!=UINT64_MAX)functions[(*count)++]=(uintptr_t)address;}
    if(direct&&final)functions[(*count)++]=(uintptr_t)m->guest+direct;
    for(size_t i=0;i<*count;i++){
        int valid=0;for(size_t k=0;k<g->count;k++){uintptr_t start=(uintptr_t)g->modules[k].guest;if(functions[i]>=start&&functions[i]-start<g->modules[k].file_size[0])valid=1;}
        if(!valid)return NX2_BAD_SYMBOL;
    }return NX2_OK;
}
const char *nx2_module_error(int code){
    static const char *names[]={"OK","bad mapping envelope","bad dynamic metadata","bad symbol","invalid relocation target","unsupported relocation/format","unresolved symbol","TLS template/layout unavailable","already linked"};
    return code>=0&&code<9?names[code]:"unknown error";
}
