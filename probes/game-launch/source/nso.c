#include "nso.h"
#include "nup.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#ifdef _WIN32
#define seek64 _fseeki64
#define tell64 _ftelli64
void *nx2_pages(size_t bytes){return _aligned_malloc(bytes,4096);}
void nx2_free_pages(void *memory){_aligned_free(memory);}
#else
#define seek64 fseeko
#define tell64 ftello
void *nx2_pages(size_t bytes){return memalign(4096,bytes);}
void nx2_free_pages(void *memory){free(memory);}
#endif
static uint32_t read32(const void *p){uint32_t v;memcpy(&v,p,4);return v;}
static void write32(void *p,uint32_t v){memcpy(p,&v,4);}
static uint64_t page(uint64_t n){return (n+4095)&~4095ULL;}
/* Common input-size boundary for both NUP and folder executable modules.
 * Do not use a lower title-specific cap than the 512 MiB mapped image boundary. */
#define NX2_NSO_INPUT_LIMIT (UINT64_C(512)*1024*1024)
static int lz4(void *output,size_t capacity,const unsigned char *in,size_t size){
    unsigned char *out=output;size_t a=0,b=0;
    while(a<size){
        unsigned token=in[a++];size_t literal=token>>4,match=token&15;
        if(literal==15){unsigned v;do{if(a==size)return 0;v=in[a++];if(literal>capacity||v>capacity-literal)return 0;literal+=v;}while(v==255);}
        if(literal>size-a||literal>capacity-b)return 0;
        memcpy(out+b,in+a,literal);a+=literal;b+=literal;
        if(a==size)break;
        if(size-a<2)return 0;
        size_t distance=in[a]|((size_t)in[a+1]<<8);a+=2;
        if(!distance||distance>b)return 0;
        if(match==15){unsigned v;do{if(a==size)return 0;v=in[a++];if(match>capacity||v>capacity-match)return 0;match+=v;}while(v==255);}
        match+=4;if(match>capacity-b)return 0;for(size_t i=0;i<match;i++)out[b+i]=out[b+i-distance];b+=match;
    }return b==capacity;
}
static void hex(char text[65],const unsigned char bytes[32]){static const char digits[]="0123456789abcdef";for(unsigned i=0;i<32;i++){text[2*i]=digits[bytes[i]>>4];text[2*i+1]=digits[bytes[i]&15];}text[64]=0;}
int nx2_prepare_nso(const char *path,const char *expected,Nx2Sha256 hash,void **image,size_t *image_size,size_t *bss_size,char error[160]){
    *image=NULL;FILE *f=NULL;unsigned char *raw=NULL,*out=NULL;int ok=0;int64_t length=0;
#define FAIL(message) do{snprintf(error,160,"%s",message);goto finish;}while(0)
    if(nx2_nup_active()) {
        const char *leaf=strstr(path,"/exefs/");
        if(!leaf)FAIL("NUP ExeFS module path missing");
        char internal[64];
        snprintf(internal,sizeof(internal),"exefs/%s",leaf+7);
        size_t n=0;
        uint64_t source_bytes=0;
        if(!nx2_nup_file_info(internal,&source_bytes,NULL))
            FAIL("NUP ExeFS module missing");
        if(source_bytes<256 || source_bytes>NX2_NSO_INPUT_LIMIT){
            snprintf(error,160,"NUP %.20s: NSO size=%llu exceeds shared limit=%llu",
                     leaf+7,(unsigned long long)source_bytes,
                     (unsigned long long)NX2_NSO_INPUT_LIMIT);
            goto finish;
        }
        raw=nx2_nup_read_all(internal,(size_t)NX2_NSO_INPUT_LIMIT,&n);
        if(!raw) {
            snprintf(error,160,"NUP %.20s: %.120s",
                     leaf+7,nx2_nup_read_error());
            goto finish;
        }
        length=(int64_t)n;
    } else {
        f=fopen(path,"rb");
        if(!f)FAIL("cannot open NSO");
        if(seek64(f,0,SEEK_END))FAIL("NSO seek failed");
        length=tell64(f);
        if(length<256||length>(int64_t)NX2_NSO_INPUT_LIMIT){
            snprintf(error,160,"NSO input bytes=%lld outside shared limit=%llu",
                     (long long)length,(unsigned long long)NX2_NSO_INPUT_LIMIT);
            goto finish;
        }
        if(seek64(f,0,SEEK_SET))FAIL("NSO rewind failed");
        raw=malloc((size_t)length);if(!raw)FAIL("NSO input allocation failed");
        if(fread(raw,1,(size_t)length,f)!=(size_t)length||ferror(f))FAIL("NSO read failed");
    }
    if(length<256||length>(int64_t)NX2_NSO_INPUT_LIMIT){
        snprintf(error,160,"NSO input bytes=%lld outside shared limit=%llu",
                 (long long)length,(unsigned long long)NX2_NSO_INPUT_LIMIT);
        goto finish;
    }
    if(memcmp(raw,"NSO0",4))FAIL("NSO0 header missing");
    unsigned char digest[32];char digest_text[65];hash(digest,raw,(size_t)length);hex(digest_text,digest);
    if(expected&&strcmp(expected,digest_text))FAIL("NSO differs from verified PC copy");
    uint32_t flags=read32(raw+12),addr[3],size[3],offset[3],packed[3];
    for(unsigned i=0;i<3;i++){
        offset[i]=read32(raw+16+16*i);addr[i]=read32(raw+20+16*i);size[i]=read32(raw+24+16*i);
        packed[i]=(flags&(1U<<i))?read32(raw+0x60+4*i):size[i];
        if(size[i]>256*1024*1024||offset[i]>(uint64_t)length||packed[i]>(uint64_t)length-offset[i]||(size[i]&&offset[i]<256))FAIL("NSO segment outside file/limit");
        if(i&&(uint64_t)addr[i-1]+size[i-1]>addr[i])FAIL("NSO segments overlap");
    }
    if(addr[0]||!size[0]||!size[1]||!size[2]||(addr[1]&4095)||(addr[2]&4095))FAIL("unsupported NSO segment alignment/layout");
    uint64_t total=4096+(uint64_t)addr[2]+page((uint64_t)size[2]+read32(raw+0x3c));
    uint64_t file_bytes=4096+(uint64_t)addr[2]+page(size[2]),bss=total-file_bytes;
    if(total>512*1024*1024||total<file_bytes)FAIL("NSO mapped size outside limit");
    out=nx2_pages((size_t)total);if(!out)FAIL("NSO mapped allocation failed");memset(out,0,(size_t)total);
    for(unsigned i=0;i<3;i++){
        void *destination=out+4096+addr[i];
        if(flags&(1U<<i)){
            int decoded=(flags&0x80)?nx2_zbic_decode(destination,size[i],raw+offset[i],packed[i]):lz4(destination,size[i],raw+offset[i],packed[i]);
            if(!decoded)FAIL("NSO segment decompression failed");
        }else memcpy(destination,raw+offset[i],size[i]);
        if(flags&(1U<<(i+3))){hash(digest,destination,size[i]);if(memcmp(digest,raw+0xa0+32*i,32))FAIL("decompressed NSO segment SHA256 mismatch");}
    }
    if(size[0]<8)FAIL("NSO text too short");
    uint32_t mod=read32(out+4096+4);int mod_valid=0;
    for(unsigned i=0;i<3;i++)if(mod>=addr[i]&&(uint64_t)mod+8<=(uint64_t)addr[i]+size[i])mod_valid=1;
    if(!mod_valid||memcmp(out+4096+mod,"MOD0",4))FAIL("NSO MOD0 missing/outside backed segments");
    int32_t delta;memcpy(&delta,out+4096+mod+4,4);int64_t dynamic=(int64_t)mod+delta;
    if(dynamic<0||(uint64_t)dynamic+16>total-4096)FAIL("NSO dynamic pointer outside image");
    write32(out,0xd65f03c0);write32(out+4,0x100);memcpy(out+0x10,"NRO0",4);write32(out+0x18,(uint32_t)file_bytes);write32(out+0x1c,1);
    write32(out+0x24,4096+addr[1]);write32(out+0x28,4096+addr[1]);write32(out+0x2c,addr[2]-addr[1]);
    write32(out+0x30,4096+addr[2]);write32(out+0x34,(uint32_t)page(size[2]));write32(out+0x38,(uint32_t)bss);memcpy(out+0x40,raw+0x40,32);
    memcpy(out+0x100,"MOD0",4);const char *extensions[]={"LNY0","LNY1","LNY2","OUN0"};for(unsigned i=0;i<4;i++)memcpy(out+0x11c+12*i,extensions[i],4);
    write32(out+0x144,2);write32(out+0x148,1);memcpy(out+0x180,"NX2MOD01",8);write32(out+0x188,1);write32(out+0x18c,(uint32_t)dynamic);
    for(unsigned i=0;i<3;i++){write32(out+0x190+12*i,addr[i]);write32(out+0x194+12*i,size[i]);write32(out+0x198+12*i,size[i]+(i==2?read32(raw+0x3c):0));}
    *image=out;*image_size=(size_t)file_bytes;*bss_size=(size_t)bss;out=NULL;ok=1;error[0]=0;
finish:
    if(f&&fclose(f)!=0&&ok){nx2_free_pages(*image);*image=NULL;ok=0;snprintf(error,160,"NSO close failed");}
    free(raw);nx2_free_pages(out);return ok;
#undef FAIL
}
