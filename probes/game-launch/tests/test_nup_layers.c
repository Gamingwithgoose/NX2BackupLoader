/* SPDX-License-Identifier: GPL-2.0-only */
/* Source fixture: link with source/zbic.c; no console or real game I/O.
 * Not built/executed by the source-only editing workflow. */
#include "../source/nup.c"
#include <assert.h>
static void put32(unsigned char *p,uint32_t v){memcpy(p,&v,4);}
static void put64(unsigned char *p,uint64_t v){memcpy(p,&v,8);}
static unsigned seen;
static int collect(void *unused,const char *path,int directory,uint64_t bytes){
 (void)unused;
 if(!strcmp(path,"base"))seen|=1;
 else if(!strcmp(path,"same"))seen|=2;
 else if(!strcmp(path,"new"))seen|=4;
 else if(!strcmp(path,"tree"))seen|=8;
 else if(!strcmp(path,"tree/fresh"))seen|=16;
 else assert(0);
 if(!directory){
  uint32_t file;
  char full[128];snprintf(full,sizeof(full),"romfs/%s",path);
  file=lookup_file_uncached(full);assert(file!=UINT32_MAX);
  assert(bytes==rd64(mounted.files+(size_t)file*80+8));
 }
 return 1;
}
static int accept(void *u,const char *p,int d,uint64_t n){(void)u;(void)p;(void)d;(void)n;return 1;}
int main(void){
 unsigned char dirs[5*24]={0},files[7*80]={0},children[9*4]={0};
 unsigned char names[]="base\0same\0tree\0inside\0new\0fresh\0";
 const uint32_t file_names[]={0,5,15,5,10,22,26};
 const uint32_t file_lengths[]={4,4,6,4,4,3,5};
 const uint32_t child_nodes[]={5,6,2,10,8,9,7,4,11};
 const uint32_t first[]={0,3,6,7,8},count[]={3,3,1,1,1};
 for(unsigned i=0;i<5;i++){
  put32(dirs+i*24+8,first[i]);put32(dirs+i*24+12,count[i]);
  if(i==2||i==4){put32(dirs+i*24,10);put32(dirs+i*24+4,4);}
 }
 for(unsigned i=0;i<7;i++){
  put32(files+i*80,file_names[i]);put32(files+i*80+4,file_lengths[i]);
  put64(files+i*80+8,(i+1)*10);
 }
 for(unsigned i=0;i<9;i++)put32(children+i*4,child_nodes[i]);
 mounted.f=(FILE *)(uintptr_t)1;mounted.ndirs=5;mounted.nfiles=7;
 mounted.dirs=dirs;mounted.files=files;mounted.children=children;
 mounted.pool=names;mounted.pool_size=sizeof(names);mounted.child_count=9;
 mounted.layers=calloc(3,sizeof(*mounted.layers));assert(mounted.layers);
 mounted.ordered=calloc(5,1);assert(mounted.ordered);mounted.ordered[0]=1;
 mounted.layers[0]=(NupLayer){0,0,0};mounted.layers[1]=(NupLayer){1,2,1};
 mounted.layers[2]=(NupLayer){3,3,2};mounted.layer_count=2;
 assert(lookup_file_uncached("romfs/base")==0); /* inherited base */
 assert(lookup_file_uncached("romfs/same")==3); /* update overwrites */
 assert(lookup_file_uncached("romfs/new")==5); /* unsorted children */
 assert(lookup_file_uncached("romfs/tree")==4); /* file hides old directory */
 assert(lookup_file_uncached("romfs/tree/inside")==UINT32_MAX);
 assert(nx2_nup_romfs_walk(collect,NULL)&&seen==15);
 mounted.layer_count=3;seen=0;
 assert(lookup_file_uncached("romfs/tree")==UINT32_MAX);
 assert(lookup_file_uncached("romfs/tree/fresh")==6);
 assert(lookup_file_uncached("romfs/tree/inside")==UINT32_MAX);
 assert(nx2_nup_romfs_walk(collect,NULL)&&seen==31);
 put32(children+8*4,4); /* cycle in newest directory: reject */
 assert(!nx2_nup_romfs_walk(accept,NULL));
 mounted.f=NULL;nx2_nup_close();
 puts("Ordered NUP inheritance, overwrite, lookup/walk agreement and cycles: PASS");
 return 0;
}
