/* SPDX-License-Identifier: GPL-2.0-only */
#include "media.h"
#include "nup_meta.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#ifdef __SWITCH__
#include <switch.h>
#endif
#ifdef _WIN32
#define seek_file _fseeki64
#define tell_file _ftelli64
#else
#define seek_file fseeko
#define tell_file ftello
#endif
#define MEDIA_LIMIT (4U<<20)
extern size_t ZSTD_decompress(void *,size_t,const void *,size_t);
extern unsigned ZSTD_isError(size_t);
static uint32_t read32(const unsigned char *p){return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static uint64_t read64(const unsigned char *p){return read32(p)|((uint64_t)read32(p+4)<<32);}
static int placeholder(const char *s){
 const char *labels[]={"meta","npdm","nacp","unknown","unknown title","base","update"};
 while(*s==' ')s++;
 for(unsigned i=0;i<sizeof(labels)/sizeof(*labels);i++){
  const unsigned char *a=(const unsigned char *)s,*b=(const unsigned char *)labels[i];
  while(*a&&*b&&tolower(*a)==*b){a++;b++;}
  while(*a==' ')a++;
  if(!*a&&!*b)return 1;
 }
 return !*s;
}
static int text(char out[256],const unsigned char *p,size_t n){
 size_t k=0;while(k<n&&p[k]){if(p[k]<32)return 0;k++;}
 if(!k||k>=n)return 0;
 if(k>255){k=255;while(k && (p[k]&0xc0)==0x80)k--;}
 memcpy(out,p,k);out[k]=0;return !placeholder(out);
}
static int title_bytes(char out[256],const unsigned char *p,size_t n){
 unsigned char value[513];if(!n||n>512)return 0;
 memcpy(value,p,n);value[n]=0;return text(out,value,n+1);
}
static int nacp_title(char out[256],const unsigned char *p,size_t n){
 /* NPDM starts with META. Some older packers put it in a NACP-tagged TLV.
  * A short binary header is not a language table or an application title. */
 if(n<16*0x300||nx2_meta_npdm(p,n))return 0;
 const unsigned order[]={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
 for(unsigned i=0;i<16;i++){
  size_t at=(size_t)order[i]*0x300;
  if(at<=n&&n-at>=0x200&&text(out,p+at,0x200))return 1;
 }
 return 0;
}
static unsigned char *file_bytes(const char *path,size_t *size){
 *size=0;FILE *f=fopen(path,"rb");if(!f)return NULL;
 if(seek_file(f,0,SEEK_END)){fclose(f);return NULL;}
 int64_t n=tell_file(f);
 if(n<=0||n>MEDIA_LIMIT||seek_file(f,0,SEEK_SET)){fclose(f);return NULL;}
 unsigned char *b=malloc((size_t)n);size_t got=b?fread(b,1,(size_t)n,f):0;
 int closed=fclose(f);if(!b||got!=(size_t)n||closed){free(b);return NULL;}
 *size=got;return b;
}
static int image_bytes(const unsigned char *p,size_t n){
 return (n>=2&&p[0]==0xff&&p[1]==0xd8)||
   (n>=8&&!memcmp(p,"\x89PNG\r\n\x1a\n",8));
}
static unsigned char *folder_bytes(const Nx2Game *g,const char *suffix,size_t *size){
 char base[1250],path[1600];
 int nb=snprintf(base,sizeof(base),"%s/base",g->library_root);
 const char *roots[]={g->root,g->base_root,g->library_root,nb>0&&nb<(int)sizeof(base)?base:""};
 for(unsigned i=0;i<4;i++){
  if(!*roots[i])continue;
  int n=snprintf(path,sizeof(path),"%s/%s",roots[i],suffix);
  if(n<=0||n>=(int)sizeof(path))continue;
  unsigned char *b=file_bytes(path,size);if(b)return b;
 }
 return NULL;
}
#ifdef __SWITCH__
static int ns_attempted,ns_live;
static void cached_control(const Nx2Game *g,char title[256],unsigned char **image,size_t *bytes){
 if(!g->id)return;
 if(!ns_attempted){ns_attempted=1;ns_live=R_SUCCEEDED(nsInitialize());}
 if(!ns_live)return;
 NsApplicationControlData *data=calloc(1,sizeof(*data));if(!data)return;
 u64 actual=0;
 /* CacheOnly never requests a download or imports game data into the system. */
 Result rc=nsGetApplicationControlData(NsApplicationControlSource_CacheOnly,g->id,data,sizeof(*data),&actual);
 if(R_SUCCEEDED(rc)&&actual<=sizeof(*data)){
  char name[256];
  if(actual>=sizeof(data->nacp)&&nacp_title(name,(const unsigned char *)&data->nacp,sizeof(data->nacp)))snprintf(title,256,"%s",name);
  size_t offset=offsetof(NsApplicationControlData,icon);
  if(image&&bytes&&!*image&&actual>offset&&actual-offset<=sizeof(data->icon)){
   size_t n=(size_t)(actual-offset);
   if(n>=2&&data->icon[0]==0xff&&data->icon[1]==0xd8){
    *image=malloc(n);if(*image){memcpy(*image,data->icon,n);*bytes=n;}
   }
  }
 }
 free(data);
}
void nx2_ui_media_exit(void){if(ns_live)nsExit();ns_live=0;ns_attempted=0;}
#else
static void cached_control(const Nx2Game *g,char title[256],unsigned char **image,size_t *bytes){(void)g;(void)title;(void)image;(void)bytes;}
void nx2_ui_media_exit(void){}
#endif
/* Actual NUP v1 localized records: u8 tag length, tag, u32 value length, value. */
static const unsigned char *localized(const unsigned char *b,size_t n,size_t *bytes){
 size_t at=0;const unsigned char *first=NULL;size_t first_size=0;
 while(at<n){
  unsigned k=b[at++];if(!k||k>n-at||n-at-k<4)return NULL;
  const unsigned char *tag=b+at;at+=k;uint32_t len=read32(b+at);at+=4;
  if(len>n-at)return NULL;
  if(!first){first=b+at;first_size=len;}
  if((k==5&&!memcmp(tag,"en-us",5))||(k==2&&!memcmp(tag,"en",2))){*bytes=len;return b+at;}
  at+=len;
 }
 *bytes=first_size;return first;
}
/* Read only candidate artwork records; never mount the guest archive. */
static void embedded_media(FILE *f,uint64_t total,uint64_t toc,uint32_t packages,
                           char title[256],int *title_rank,unsigned char **image,size_t *bytes){
 unsigned char h[48];
 if(seek_file(f,128,SEEK_SET)||fread(h,1,48,f)!=48)return;
 uint64_t dirs=read64(h+8),files=read64(h+16),pool=read64(h+24),pool_size=read64(h+32);
 if(toc>(128U<<20)||dirs>toc/24||files>toc/80||pool>toc||pool_size>toc-pool)return;
 uint64_t start=48+64ULL*packages+24*dirs;
 if(start>toc||files>(toc-start)/80)return;
 unsigned char *names=malloc((size_t)pool_size),*block=malloc(80*1024);
 if(!names||!block){free(names);free(block);return;}
 if(seek_file(f,(int64_t)(128+pool),SEEK_SET)||fread(names,1,(size_t)pool_size,f)!=pool_size){free(names);free(block);return;}
 uint64_t block_at=UINT64_MAX;
 for(uint64_t i=0;i<files;i++){
  if(block_at==UINT64_MAX||i>=block_at+1024){
   block_at=i;size_t n=(size_t)(files-i<1024?files-i:1024)*80;
   if(seek_file(f,(int64_t)(128+start+80*i),SEEK_SET)||fread(block,1,n,f)!=n)break;
  }
  const unsigned char *record=block+(size_t)(i-block_at)*80;
  uint32_t no=read32(record),nl=read32(record+4);
  if(!nl||nl>255||no>pool_size||nl>pool_size-no)continue;
  char name[256];memcpy(name,names+no,nl);name[nl]=0;
  const char *leaf=strrchr(name,'/');leaf=leaf?leaf+1:name;
  int icon=image&&!*image&&(!strcmp(leaf,"icon_AmericanEnglish.dat")||!strcmp(leaf,"icon_BritishEnglish.dat")||!strcmp(leaf,"icon.jpg")||!strcmp(leaf,"icon.png"));
  int nacp=*title_rank<3&&!strcmp(leaf,"control.nacp");
  if(!icon&&!nacp)continue;
  uint64_t unpacked=read64(record+8),off=read64(record+16),packed=read64(record+24);
  if(!unpacked||unpacked>MEDIA_LIMIT||!packed||packed>MEDIA_LIMIT||off>total||packed>total-off||record[64]>1)continue;
  unsigned char *in=malloc((size_t)packed),*out=NULL;
  if(!in)break;
  if(seek_file(f,(int64_t)off,SEEK_SET)||fread(in,1,(size_t)packed,f)!=packed){free(in);break;}
  if(record[64]==0){if(packed==unpacked){out=in;in=NULL;}}
  else{out=malloc((size_t)unpacked);if(out){size_t got=ZSTD_decompress(out,(size_t)unpacked,in,(size_t)packed);if(ZSTD_isError(got)||got!=unpacked){free(out);out=NULL;}}}
  free(in);if(!out)continue;
  if(nacp&&nacp_title(title,out,(size_t)unpacked))*title_rank=3;
  if(icon&&image_bytes(out,(size_t)unpacked)){*image=out;*bytes=(size_t)unpacked;out=NULL;}
  free(out);
  if(*title_rank==3&&(!image||*image))break;
 }
 free(block);free(names);
}
static void nup_media(const Nx2Game *g,char title[256],unsigned char **image,size_t *bytes){
 FILE *f=fopen(g->root,"rb");if(!f)return;
 unsigned char h[128],pkg[64];
 if(fread(h,1,sizeof(h),f)!=sizeof(h)||memcmp(h,"NUP\1",4)||
    seek_file(f,0,SEEK_END)){fclose(f);return;}
 int64_t actual=tell_file(f);uint64_t total=read64(h+0x40),toc=read64(h+0x10);
 uint32_t count=read32(h+0x48);
 if(actual<0||total!=(uint64_t)actual||!count||count>4096||
    toc<48+64ULL*count||toc>total||total<128||toc>total-128){fclose(f);return;}
 int title_rank=0;
 for(uint32_t i=0;i<count;i++){
  if(seek_file(f,128+48+64ULL*i,SEEK_SET)||fread(pkg,1,64,f)!=64)break;
  if(pkg[24]>1)continue; /* Ignore unrelated add-on metadata. */
  uint64_t off=read64(pkg+32);uint32_t n=read32(pkg+40);
  if(!n||n>MEDIA_LIMIT||off>total||n>total-off||off>INT64_MAX)continue;
  unsigned char *b=malloc(n);if(!b)break;
  if(seek_file(f,(int64_t)off,SEEK_SET)||fread(b,1,n,f)!=n){free(b);break;}
  size_t pos=0;unsigned fact_base=nx2_meta_base(b,n);
  while(n-pos>=8){
   unsigned type=b[pos]|((unsigned)b[pos+1]<<8),flags=b[pos+2]|((unsigned)b[pos+3]<<8);
   uint32_t len=read32(b+pos+4);pos+=8;
   if(flags||len>n-pos)break;
   if(type<fact_base){pos+=len;continue;}type-=fact_base;
   const unsigned char *value=b+pos;size_t size=len;char candidate[256];int rank=0;
   if(type==1&&nacp_title(candidate,value,size))rank=3;
   else if(type==4&&title_bytes(candidate,value,size))rank=1;
   else if(type==8){value=localized(value,size,&size);if(value&&title_bytes(candidate,value,size))rank=2;}
   if(rank>title_rank){snprintf(title,256,"%s",candidate);title_rank=rank;}
   if(image&&!*image&&(type==2||type==9)){
    value=b+pos;size=len;if(type==9)value=localized(value,size,&size);
    if(value&&size&&size<=MEDIA_LIMIT&&image_bytes(value,size)){
     *image=malloc(size);if(*image){memcpy(*image,value,size);*bytes=size;}
    }
   }
   pos+=len;
  }
  free(b);
 }
 /* Avoid scanning large file tables just to name the initial library. */
 if(image&&!*image)embedded_media(f,total,toc,count,title,&title_rank,image,bytes);
 fclose(f);
}
void nx2_ui_media(const Nx2Game *g,char title[256],unsigned char **image,size_t *bytes){
 snprintf(title,256,"%s",g->name);
 for(unsigned i=0;title[i];i++)if(title[i]=='_')title[i]=' ';
 if(image)*image=NULL;if(bytes)*bytes=0;
 if(g->is_nup){
  if(bytes)nup_media(g,title,image,bytes);else nup_media(g,title,NULL,NULL);
  if(!image||!*image)cached_control(g,title,image,bytes);
  return;
 }
 const char *nacps[]={"control.nacp","control/control.nacp","control/romfs/control.nacp","romfs/control.nacp"};
 for(unsigned i=0;i<4;i++){
  size_t n=0;unsigned char *b=folder_bytes(g,nacps[i],&n);
  if(!b)continue;int ok=nacp_title(title,b,n);free(b);if(ok)break;
 }
 if(!image||!bytes){cached_control(g,title,NULL,NULL);return;}
 const char *icons[]={"icon_AmericanEnglish.dat","control/icon_AmericanEnglish.dat",
   "control/romfs/icon_AmericanEnglish.dat","romfs/icon_AmericanEnglish.dat",
   "icon_BritishEnglish.dat","control/icon_BritishEnglish.dat",
   "icon.jpg","icon.png","icon.dat","icon.jpeg","control/icon.jpg","control/icon.png",
   "control/icon.dat","control/icon.jpeg","control/romfs/icon.jpg","control/romfs/icon.png",
   "icon_AmericanEnglish.jpg","icon_AmericanEnglish.png","control/icon_AmericanEnglish.jpg"};
 for(unsigned i=0;i<sizeof(icons)/sizeof(*icons);i++){
  *image=folder_bytes(g,icons[i],bytes);
  if(*image&&image_bytes(*image,*bytes))break;
  free(*image);*image=NULL;*bytes=0;
 }
 if(!*image)cached_control(g,title,image,bytes);
}
