/* SPDX-License-Identifier: GPL-2.0-only */
/* Compile with -D__SWITCH__ -Itests/nup-native-stubs and source/zbic.c.
 * Includes implementation to exercise the exact native chunk helper using
 * deterministic native-service outcomes, without actual console/SD I/O. */
#define _CRT_SECURE_NO_WARNINGS
#include "../source/nup.c"
#include <assert.h>
static unsigned calls,mode;
static uint64_t expected_offset;
static const unsigned char frame[]={0x28,0xb5,0x2f,0xfd,0x20,9,0x49,0,0,
                                    'n','u','p','-','b','y','t','e','s'};
FsFileSystem *fsdevGetDeviceFileSystem(const char *name){(void)name;return NULL;}
Result fsFsOpenFile(FsFileSystem *sd,const char *p,uint32_t m,FsFile *f){
 (void)sd;(void)p;(void)m;(void)f;return 1;
}
void fsFileClose(FsFile *f){(void)f;}
Result fsFileGetSize(FsFile *f,s64 *size){(void)f;*size=0;return 0;}
Result fsFileRead(FsFile *f,uint64_t offset,void *out,uint64_t n,
                   uint32_t option,uint64_t *actual){
 (void)f;assert(offset==expected_offset && n<=65536 && option==0);
 calls++;expected_offset+=n;
 if(mode==1){*actual=0;return 0x1234;}
 *actual=mode==2?n-1:n;
 if(mode==3){assert(n==sizeof(frame));memcpy(out,frame,sizeof(frame));}
 else memset(out,0x5a,(size_t)*actual);
 return 0;
}
int main(void){
 unsigned char out[65540];
 mounted.file_length=UINT64_C(0x200000000);mounted.native_open=1;
 uint64_t offset=UINT64_C(0x100000010);
 expected_offset=offset;progress_begin(0,0,sizeof(out));
 assert(read_archive(offset,out,sizeof(out)) && calls==2);
 for(unsigned i=0;i<sizeof(out);i++)assert(out[i]==0x5a);
 Nx2NupReadProgress p;nx2_nup_get_progress(&p);
 assert(p.native_io==1 && p.sd_bytes==sizeof(out) && !p.io_inflight);
 assert(p.chunk_offset==offset+65536 && p.chunk_bytes==4 && p.io_result==0);
 mode=1;calls=0;expected_offset=offset;progress_begin(0,0,sizeof(out));
 assert(!read_archive(offset,out,sizeof(out)) && calls==1);
 nx2_nup_get_progress(&p);assert(p.io_result==0x1234 && !p.io_inflight && p.sd_bytes==0);
 assert(strstr(nx2_nup_read_error(),"result=00001234"));
 mode=2;calls=0;expected_offset=offset;progress_begin(0,0,sizeof(out));
 assert(!read_archive(offset,out,sizeof(out)) && calls==1);
 nx2_nup_get_progress(&p);assert(p.io_result==0 && p.sd_bytes==65535);
 assert(strstr(nx2_nup_read_error(),"requested=65536 read=65535"));
 calls=0;assert(!read_archive(mounted.file_length-3,out,4) && calls==0);
 assert(!read_archive(UINT64_MAX,out,4) && calls==0);
 /* Exercise real standard-Zstd decoding, partial reads and cached rereads
  * on top of the same native read helper. The frame is an independent
  * standards-format single-segment/raw-block Zstd frame, not a decoder stub. */
 unsigned char dirs[24]={0},files[80]={0},children[4]={1,0,0,0};
 unsigned char pool[]="test";
 dirs[12]=1;files[4]=4;files[8]=9;files[16]=0x10;files[20]=1;
 files[24]=(unsigned char)sizeof(frame);files[64]=1;
 sha256((const unsigned char *)"nup-bytes",9,files+32);
 mounted.f=(FILE *)(uintptr_t)1;
 mounted.ndirs=1;mounted.nfiles=1;mounted.romfs_dir=0;
 mounted.dirs=dirs;mounted.files=files;mounted.children=children;
 mounted.child_count=1;mounted.pool=pool;mounted.pool_size=4;
 mounted.cached_file[0]=mounted.cached_file[1]=UINT32_MAX;
 calls=0;mode=3;expected_offset=offset;
 assert(nx2_nup_read("romfs/test",2,out,4) && calls==1);
 assert(!memcmp(out,"p-by",4));
 assert(nx2_nup_read("romfs/test",0,out,9) && calls==1);
 assert(!memcmp(out,"nup-bytes",9));
 size_t all_bytes=0;void *all=nx2_nup_read_all("romfs/test",32,&all_bytes);
 assert(all && all_bytes==9 && !memcmp(all,"nup-bytes",9));free(all);
 assert(!nx2_nup_read("romfs/test",8,out,2));
 mounted.f=NULL;mounted.native_open=0;nx2_nup_close();
 puts("Native NUP offsets, short reads and errors: PASS");return 0;
}
