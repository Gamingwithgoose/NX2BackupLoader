/* SPDX-License-Identifier: GPL-2.0-only */
#include "../source/path_guard.h"
#include "../source/ipc_policy.h"
#include "../source/rom_view.h"
#include "../source/patch.h"
#include "../source/heap_plan.h"
#include "../source/vm_memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
static unsigned checks;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"FAILED line %u: %s\n",__LINE__,#x);exit(1);}}while(0)
static uint64_t q(const unsigned char *p){uint64_t v=0;for(unsigned i=0;i<8;i++)v|=(uint64_t)p[i]<<(8*i);return v;}
static void put(unsigned char *p,uint64_t v,unsigned n){for(unsigned i=0;i<n;i++)p[i]=v>>(8*i);}
static uint32_t read_extent(void *context,uint32_t file,uint64_t offset,void *buf,uint64_t n){uint64_t *seen=context;seen[0]=file;seen[1]=offset;seen[2]+=n;memset(buf,0x6a,(size_t)n);return 0;}
static void packet(unsigned char *p,unsigned type,unsigned cmd){memset(p,0,256);put(p,type,4);put(p+4,8,4);put(p+16,0x49434653,4);put(p+24,cmd,4);}
int main(int argc,char **argv){
 CHECK(argc==2);char out[1200];const char *root="/switch/nx2-backup-loader/saves/yoshi-demo";
 CHECK(nx2_save_path(out,sizeof(out),root,"/slot/data.sav"));CHECK(!strcmp(out,"/switch/nx2-backup-loader/saves/yoshi-demo/slot/data.sav"));
 CHECK(nx2_save_path(out,sizeof(out),root,"/"));CHECK(!strcmp(out,root));
 const char *bad[]={"../other","/../other","a/../../x","a/./b","a//b","a/","//a","a\\b","sdmc:/x","a\001b",".",".."};
 for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++)CHECK(!nx2_save_path(out,sizeof(out),root,bad[i]));
 CHECK(!nx2_save_path(out,8,root,"file"));char huge[1100];memset(huge,'x',1099);huge[1099]=0;CHECK(!nx2_save_path(out,sizeof(out),root,huge));
 unsigned char p[256];packet(p,4,1);memcpy(p+32,"fsp-srv",8);Nx2IpcDecision d=nx2_ipc_decide(p,256,1);CHECK(d.valid&&d.command==1&&d.want_fsp);
 memset(p+32,0,8);memcpy(p+160,"fsp-srv",8);CHECK(!nx2_ipc_decide(p,256,1).want_fsp);
 put(p+4,2,4);CHECK(!nx2_ipc_decide(p,256,1).valid);CHECK(!nx2_ipc_decide(p,7,1).valid);
 memset(p,0,256);put(p,17,4);put(p+4,2,4);memcpy(p+8,"fsp-srv",8);CHECK(nx2_ipc_decide(p,256,1).want_fsp);CHECK(!nx2_ipc_decide(p,15,1).valid);
 unsigned allowed[]={1,1005};for(unsigned i=0;i<2;i++){packet(p,4,allowed[i]);CHECK(nx2_ipc_fsp_allowed(nx2_ipc_decide(p,256,2)));}
 unsigned denied[]={0,18,21,22,51,1004,65535};for(unsigned i=0;i<7;i++){packet(p,4,denied[i]);CHECK(!nx2_ipc_fsp_allowed(nx2_ipc_decide(p,256,2)));}
 for(unsigned i=0;i<5;i++){packet(p,5,i);CHECK(nx2_ipc_fsp_allowed(nx2_ipc_decide(p,256,2)));}
 packet(p,5,5);CHECK(!nx2_ipc_fsp_allowed(nx2_ipc_decide(p,256,2)));packet(p,6,1005);put(p+28,123,4);CHECK(nx2_ipc_fsp_allowed(nx2_ipc_decide(p,256,2)));
 packet(p,4,1005);put(p+4,1023,4);CHECK(!nx2_ipc_fsp_allowed(nx2_ipc_decide(p,256,2)));
 char service[9];uint32_t reply_result=0;
 packet(p,4,1);memcpy(p+32,"lm",3);CHECK(nx2_ipc_service_name(p,256,service)&&!strcmp(service,"lm"));
 packet(p,4,1);memcpy(p+160,"lm",3);CHECK(nx2_ipc_service_name(p,256,service)&&!service[0]);
 put(p+4,2,4);CHECK(!nx2_ipc_service_name(p,256,service));
 memset(p,0,256);put(p,17,4);put(p+4,2,4);memcpy(p+8,"csrng",6);CHECK(nx2_ipc_service_name(p,256,service)&&!strcmp(service,"csrng"));
 memset(p,0,256);put(p+4,8,4);put(p+16,0x4f434653,4);put(p+24,0xe15,4);CHECK(nx2_ipc_reply_result(p,256,4,&reply_result)&&reply_result==0xe15);
 put(p+4,1,4);put(p+8,0x415,4);CHECK(nx2_ipc_reply_result(p,256,17,&reply_result)&&reply_result==0x415);
 CHECK(!nx2_ipc_reply_result(p,10,17,&reply_result));put(p+4,1023,4);CHECK(!nx2_ipc_reply_result(p,256,17,&reply_result));
 FILE *f=fopen(argv[1],"rb");CHECK(f!=0);CHECK(!fseek(f,0,SEEK_END));long n=ftell(f);rewind(f);CHECK(n>80);unsigned char *map=malloc(n),*copy=malloc(n);CHECK(map&&copy);CHECK(fread(map,1,n,f)==(size_t)n);fclose(f);
 Nx2RomView v;CHECK(nx2_rom_view(map,n,&v));CHECK(v.count==147);unsigned char b[8192];uint64_t seen[3]={0};CHECK(!nx2_rom_read(&v,0,b,80,read_extent,seen));CHECK(!memcmp(b,map+q(map+64),80));CHECK(!seen[2]);
 unsigned big=0;for(unsigned i=0;i<v.count;i++)if(q(v.entries+32*i+8)>UINT64_C(0x80020000)){big=i;break;}CHECK(q(v.entries+32*big+8)>UINT64_C(0x80020000));
 CHECK(!nx2_rom_read(&v,q(v.entries+32*big)+UINT64_C(0x80010000),b,4096,read_extent,seen));CHECK(seen[0]==big&&seen[1]==UINT64_C(0x80010000)&&seen[2]==4096);CHECK(b[0]==0x6a&&b[4095]==0x6a);
 CHECK(!nx2_rom_read(&v,v.metadata_size,b,v.data_base-v.metadata_size,read_extent,seen));for(unsigned i=0;i<v.data_base-v.metadata_size;i++)CHECK(!b[i]);CHECK(!nx2_rom_read(&v,v.total,0,0,0,0));CHECK(nx2_rom_read(&v,v.total,b,1,read_extent,seen));CHECK(nx2_rom_read(&v,UINT64_MAX,b,4096,read_extent,seen));
 unsigned mutations[]={0,8,12,32,36,40,48,56,64,72};for(unsigned i=0;i<sizeof(mutations)/sizeof(*mutations);i++){memcpy(copy,map,n);memset(copy+mutations[i],0xff,4);CHECK(!nx2_rom_view(copy,n,&v));}
 memcpy(copy,map,n);put(copy+q(copy+40)+16,UINT32_MAX,4);CHECK(!nx2_rom_view(copy,n,&v));memcpy(copy,map,n);copy[q(copy+48)]=':';CHECK(!nx2_rom_view(copy,n,&v));free(copy);free(map);
 unsigned char image[512]={0};uint32_t cursor=256;uintptr_t original=0;put(image+16,0xd503237f,4);CHECK(nx2_patch_gate(image,256,512,&cursor,16,0x123456789ULL,0x100000,&original));CHECK(cursor==288&&original==0x100110);CHECK(q(image+264)==0x123456789ULL);CHECK((uint32_t)q(image+272)==0xd503237f);
 unsigned rejected[]={0x90000000,0x10000000,0x58000000,0x54000000,0x34000000,0x36000000,0x94000000};for(unsigned i=0;i<7;i++){cursor=256;put(image+16,rejected[i],4);CHECK(!nx2_patch_gate(image,256,512,&cursor,16,4,0,&original));CHECK(cursor==256);}
 cursor=496;CHECK(!nx2_patch_gate(image,256,512,&cursor,16,4,0,&original));cursor=257;CHECK(!nx2_patch_gate(image,256,512,&cursor,16,4,0,0));cursor=256;CHECK(!nx2_patch_gate(image,256,512,&cursor,255,4,0,0));
 Nx2HeapPlan hp;
 CHECK(nx2_heap_plan(UINT64_C(0x46c8493000),UINT64_C(9501593600),&hp)==NX2_HEAP_READY);
 CHECK(hp.host_bytes>=NX2_HOST_MIN_BYTES&&hp.game_bytes>=NX2_GAME_MIN_BYTES);
 CHECK(hp.host_begin+hp.host_bytes==hp.game_begin);
 CHECK(hp.game_begin+hp.game_bytes<=hp.original_begin+hp.original_bytes);
 CHECK(!(hp.game_begin&(NX2_GAME_ALIGNMENT-1))&&!(hp.game_bytes&(NX2_GAME_ALIGNMENT-1)));
 CHECK(nx2_heap_plan(0,9501593600ULL,&hp)==NX2_HEAP_INVALID);
 CHECK(!hp.game_begin&&!hp.game_bytes);
 CHECK(nx2_heap_plan(UINT64_MAX-8,16,&hp)==NX2_HEAP_INVALID);
 CHECK(nx2_heap_plan(0x100000,0x20000000,&hp)==NX2_HEAP_TOO_SMALL);
 CHECK(hp.host_bytes==0x20000000&&!hp.game_begin&&!hp.game_bytes);
 CHECK(nx2_heap_plan(0x200000,0x140000000ULL,&hp)==NX2_HEAP_READY);
 CHECK(hp.game_bytes==NX2_GAME_MIN_BYTES&&hp.host_bytes==NX2_HOST_MIN_BYTES);
 CHECK(nx2_heap_plan(0x200001,0x140000000ULL,&hp)==NX2_HEAP_TOO_SMALL);
 CHECK(nx2_heap_plan(0x200000,0x140000000ULL-1,&hp)==NX2_HEAP_TOO_SMALL);
 CHECK(nx2_heap_plan(0x200000,0x1000000000ULL,&hp)==NX2_HEAP_READY);
 CHECK(hp.game_bytes==NX2_GAME_MAX_BYTES);
 uint64_t seed=123;
 for(unsigned i=0;i<10000;i++){
  seed=seed*UINT64_C(6364136223846793005)+1;uint64_t begin=(seed>>12)|1;
  seed=seed*UINT64_C(6364136223846793005)+1;uint64_t bytes=seed&UINT64_C(0x7ffffffff);
  uint32_t status=nx2_heap_plan(begin,bytes,&hp);
  if(status==NX2_HEAP_READY){
   CHECK(hp.host_bytes>=NX2_HOST_MIN_BYTES&&hp.game_bytes>=NX2_GAME_MIN_BYTES&&hp.game_bytes<=NX2_GAME_MAX_BYTES);
   CHECK(hp.game_begin==begin+hp.host_bytes&&hp.game_bytes<=bytes-hp.host_bytes);
   CHECK(!(hp.game_begin&(NX2_GAME_ALIGNMENT-1))&&!(hp.game_bytes&(NX2_GAME_ALIGNMENT-1)));
  }else CHECK(!hp.game_begin&&!hp.game_bytes);
 }
 CHECK(nx2_heap_plan(UINT64_C(0x2ed569e000),UINT64_C(9321193472),&hp)==NX2_HEAP_READY);
 Nx2HeapView view={UINT64_C(0x2ed1600000),UINT64_C(0x300000000),UINT64_C(0x2ed6070000),UINT64_C(9310896128),5,3,0,hp.host_begin,hp.game_begin};
 Nx2ReleasePlan release;
 CHECK(nx2_plan_heap_release(&hp,&view,&release));
 CHECK(release.retained_bytes==UINT64_C(1142947840));
 CHECK(release.released_begin==UINT64_C(0x2f15800000)&&release.released_bytes==UINT64_C(8246001664));
 CHECK(view.heap_begin+release.retained_bytes==hp.host_begin+hp.host_bytes);
 for(unsigned i=0;i<9;i++){
  Nx2HeapView badview=view;
  switch(i){case 0:badview.heap_begin=hp.game_begin;break;case 1:badview.heap_reserved_bytes=1;break;case 2:badview.tail_begin=hp.game_begin+4096;break;case 3:badview.tail_bytes--;break;case 4:badview.tail_type=4;break;case 5:badview.tail_permission=1;break;case 6:badview.tail_attributes=1;break;case 7:badview.allocator_begin++;break;case 8:badview.allocator_end+=4096;break;}
  CHECK(!nx2_plan_heap_release(&hp,&badview,&release));
  CHECK(!release.retained_bytes&&!release.released_begin&&!release.released_bytes);
 }
 view.allocator_begin=hp.game_begin;CHECK(nx2_plan_heap_release(&hp,&view,&release));
 view.allocator_begin=hp.game_begin-4096;CHECK(!nx2_plan_heap_release(&hp,&view,&release));
 view.allocator_begin=hp.game_begin+4096;CHECK(!nx2_plan_heap_release(&hp,&view,&release));
 view.allocator_begin=hp.host_begin;
 Nx2HeapPlan badplan=hp;badplan.host_bytes--;CHECK(!nx2_plan_heap_release(&badplan,&view,&release));
 badplan=hp;badplan.original_bytes=UINT64_MAX;CHECK(!nx2_plan_heap_release(&badplan,&view,&release));
 printf("PASS %u checks: path isolation, IPC policy, RomFS bounds/large offsets, executable gates, startup heap partition/release\n",checks);return 0;
}
