/* SPDX-License-Identifier: GPL-2.0-only */
/* Synthetic loose-file fixture created by test_folder_union.py. */
#include "folder.h"
#include "rom_view.h"
#include "index_cache.h"
#include "nup.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int nx2_nup_active(void){return 0;}
int nx2_nup_probe(const char *p,uint64_t *id,uint64_t *n){(void)p;(void)id;(void)n;return 0;}
const char *nx2_nup_probe_error(void){return "unused";}
int nx2_nup_file_info(const char *p,uint64_t *n,unsigned *c){(void)p;(void)n;(void)c;return 0;}
int nx2_nup_romfs_walk(Nx2NupVisit v,void *p){(void)v;(void)p;return 0;}
typedef struct {Nx2RomView view;const char *primary,*base;} Fixture;
static uint32_t read_file(void *context,uint32_t file,uint64_t off,void *out,uint64_t n){
 Fixture *f=context;char path[2400];
 const char *root=nx2_rom_source(&f->view,file)?f->base:f->primary;
 snprintf(path,sizeof(path),"%s/%s",root,nx2_rom_path(&f->view,file));
 FILE *input=fopen(path,"rb");if(!input)return 1;
 if(fseek(input,(long)off,SEEK_SET)){fclose(input);return 1;}
 size_t actual=fread(out,1,(size_t)n,input);fclose(input);return actual!=n;
}
static void expect(Fixture *f,const char *name,const char *value,unsigned source){
 for(uint32_t i=0;i<f->view.count;i++)if(!strcmp(nx2_rom_path(&f->view,i),name)){
  const unsigned char *e=f->view.entries+32*i;uint64_t offset,size;
  memcpy(&offset,e,8);memcpy(&size,e+8,8);
  assert(nx2_rom_source(&f->view,i)==source && size==strlen(value));
  char bytes[128]={0};assert(size<sizeof(bytes));
  assert(!nx2_rom_read(&f->view,offset,bytes,size,read_file,f));
  assert(!strcmp(bytes,value));return;
 }
 fprintf(stderr,"Missing expected path %s\n",name);assert(0);
}
static int cancel(void *unused){(void)unused;return 1;}
int main(int argc,char **argv){
 assert(argc==2);Nx2Catalog c;assert(nx2_catalog(argv[1],&c) && c.count==1);
 assert(c.items[0].updates==2 && c.items[0].update_version==917504);
 assert(strstr(c.items[0].root,"updates/917504") && strstr(c.items[0].base_root,"/base"));
 char root[1400],base[1400],error[160];size_t bytes=0;
 snprintf(root,sizeof(root),"%s/romfs",c.items[0].root);
 snprintf(base,sizeof(base),"%s/romfs",c.items[0].base_root);
 void *blob=nx2_folder_rom_layered(root,base,&bytes,error);
 if(!blob){fprintf(stderr,"Index failed: %s\n",error);assert(0);}
 Fixture f={.primary=root,.base=base};assert(nx2_rom_view(blob,bytes,&f.view));
 assert(f.view.count==7);
 expect(&f,"arc/base.bin","base archive",1);
 expect(&f,"audio/music.bin","base audio",1);
 expect(&f,"common.bin","updated",0);
 expect(&f,"new.bin","new update asset",0);
 expect(&f,"shared/keep.bin","unchanged",1);
 expect(&f,"dir_to_file","replacement file",0);
 expect(&f,"file_to_dir/new.bin","replacement directory",0);
 assert(nx2_cache_save_layered(root,base,blob,bytes));
 size_t loaded_bytes=0;void *loaded=nx2_cache_load_layered(root,base,&loaded_bytes);
 assert(loaded && loaded_bytes==bytes && !memcmp(loaded,blob,bytes));free(loaded);
 assert(!nx2_cache_load(root,&loaded_bytes));
 assert(!nx2_cache_load_layered(root,argv[1],&loaded_bytes));
 size_t single_bytes=0;void *single=nx2_folder_rom(root,&single_bytes,error);
 assert(single && nx2_cache_save(root,single,single_bytes));
 loaded=nx2_cache_load_layered(root,base,&loaded_bytes);
 assert(loaded && loaded_bytes==bytes);free(loaded);free(single);
 /* A version-1 index may not smuggle a base selector into legacy readers. */
 unsigned char *raw=blob;raw[8]=1;assert(!nx2_rom_view(blob,bytes,&f.view));raw[8]=2;
 nx2_folder_cancel(cancel,NULL);
 void *cancelled=nx2_folder_rom_layered(root,base,&loaded_bytes,error);assert(!cancelled);
 nx2_folder_cancel(NULL,NULL);
 free(blob);nx2_catalog_free(&c);puts("Folder base/update union and cache isolation: PASS");return 0;
}
