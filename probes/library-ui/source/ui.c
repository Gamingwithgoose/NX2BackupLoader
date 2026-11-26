/* SPDX-License-Identifier: GPL-2.0-only */
#include "ui.h"
#include "media.h"
#include "launch_request.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <sys/stat.h>
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_MAX_DIMENSIONS 2048
#define STB_IMAGE_IMPLEMENTATION
#include "../vendor/stb_image.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "../vendor/stb_truetype.h"
#define WIDTH 1280
#define HEIGHT 720
#define TILE_W 224
#define ART_H 132
#define TILE_H 174
#define ICON_SLOTS 20
#define GLYPH_SLOTS 512
#define CONFIG_PATH "sdmc:/switch/BackupLoaderMKW/ui.cfg"
typedef struct {const char *name;unsigned bg,panel,card,text,muted,accent;} Theme;
static const Theme themes[]={
 {"Red",0x171319,0x241b24,0x30242d,0xfff5f6,0xc5b8c1,0xff566b},
 {"Green",0x101b18,0x1b2923,0x26372e,0xf0fff5,0xb0cbbd,0x56ed99},
 {"Blue",0x111923,0x1a2636,0x25354a,0xf1f7ff,0xb2c3db,0x59c8ff},
 {"Purple",0x191420,0x281f34,0x382b48,0xfbf3ff,0xcbb8dc,0xc08aff},
 {"Black",0x121212,0x1c1c1c,0x242424,0xf5f5f5,0xbcbcbc,0xe0e0e0},
 {"White",0xe5e8ed,0xf5f6f9,0xffffff,0x18202d,0x4b576b,0x0064a7},
 {"Pink",0xf1dfe9,0xffeff7,0xfff8fc,0x372332,0x705364,0xa62270},
 {"Orange",0x21180f,0x30251b,0x413124,0xfff7ee,0xd4bd9f,0xffad50},
 {"Teal",0x101d21,0x1a2c31,0x243d43,0xf0ffff,0xb0ced2,0x45e5de}
};
static const char *sort_names[]={"A-Z","Z-A"};
typedef struct {unsigned theme,sort,filter,detailed;} Settings;
typedef struct {unsigned cp;int px,w,h,xoff,yoff,advance;unsigned char *pixels;u64 stamp;} Glyph;
typedef struct {size_t item;unsigned char *pixels;u64 stamp;int used;} Icon;
typedef struct {
 Nx2Catalog *catalog;char (*titles)[256];size_t *view,count,selected,first;
 Settings settings;Framebuffer fb;int fb_live,pl_live,psm_live,nifm_live,font_live;
 stbtt_fontinfo font,extension;int extension_live;ConsoleFont fallback;Glyph glyphs[GLYPH_SLOTS];Icon icons[ICON_SLOTS];
 unsigned *pixels;u32 stride;u64 stamp,status_tick;int battery,charging,network,strength;
 s64 free_bytes,total_bytes;int modal,choice,dirty;char notice[192];
} UI;
static unsigned rgba(unsigned c){return 0xff000000U|((c&255)<<16)|(c&0xff00)|((c>>16)&255);}
static unsigned mix(unsigned a,unsigned b,unsigned alpha){
 unsigned result=0;for(int shift=0;shift<24;shift+=8)result|=((((a>>shift)&255)*(255-alpha)+((b>>shift)&255)*alpha)/255)<<shift;
 return result;
}
static const Theme *theme(UI *u){return &themes[u->settings.theme];}
static void pixel(UI *u,int x,int y,unsigned c,unsigned alpha){
 if(x<0||x>=WIDTH||y<0||y>=HEIGHT)return;
 unsigned *p=u->pixels+y*u->stride+x;
 if(alpha==255)*p=rgba(c);else{
  unsigned old=((*p&255)<<16)|(*p&0xff00)|((*p>>16)&255);*p=rgba(mix(old,c,alpha));
 }
}
/* Analytic edge coverage keeps corners and circles smooth at native resolution. */
static unsigned round_alpha(int x,int y,int w,int h,int r){
 if(r>w/2)r=w/2;if(r>h/2)r=h/2;
 if(!r||((x>=r&&x<w-r)||(y>=r&&y<h-r)))return 255;
 float cx=x<r?(float)r:(float)(w-r),cy=y<r?(float)r:(float)(h-r);
 float dx=x+0.5f-cx,dy=y+0.5f-cy;
 float coverage=r+0.5f-sqrtf(dx*dx+dy*dy);
 return coverage<=0?0:coverage>=1?255:(unsigned)(coverage*255);
}
static void rect(UI *u,int x,int y,int w,int h,int radius,unsigned color){
 if(w<=0||h<=0)return;
 for(int yy=0;yy<h;yy++)for(int xx=0;xx<w;xx++){
  unsigned alpha=round_alpha(xx,yy,w,h,radius);if(alpha)pixel(u,x+xx,y+yy,color,alpha);
 }
}
static void arc(UI *u,int cx,int cy,float radius,float width,unsigned color){
 int bound=(int)(radius+width+2);
 for(int y=-bound;y<=0;y++)for(int x=-bound;x<=bound;x++){
  float dx=x+0.5f,dy=y+0.5f;
  if(fabsf(dx)>-dy)continue;
  float coverage=width*0.5f+0.5f-fabsf(sqrtf(dx*dx+dy*dy)-radius);
  if(coverage>0)pixel(u,cx+x,cy+y,color,coverage>=1?255:(unsigned)(coverage*255));
 }
}
static void line(UI *u,int x,int y,int xx,int yy,unsigned c,int thickness){
 int dx=abs(xx-x),dy=abs(yy-y),sx=x<xx?1:-1,sy=y<yy?1:-1,err=dx-dy;
 for(;;){rect(u,x,y,thickness,thickness,0,c);if(x==xx&&y==yy)break;int e=2*err;if(e>-dy){err-=dy;x+=sx;}if(e<dx){err+=dx;y+=sy;}}
}
static unsigned codepoint(const char **cursor){
 const unsigned char *p=(const unsigned char *)*cursor;unsigned c=*p++;
 if(c>=128){int n=c>=0xf0&&c<=0xf4?3:c>=0xe0&&c<=0xef?2:c>=0xc2&&c<=0xdf?1:0;
  if(!n){*cursor=(const char *)p;return '?';}c&=(1U<<(6-n))-1;
  for(int k=0;k<n;k++){if((*p&0xc0)!=0x80){*cursor=(const char *)p;return '?';}c=(c<<6)|(*p++&63);}
  if(c>0x10ffff||(c>=0xd800&&c<=0xdfff))c='?';
 }
 *cursor=(const char *)p;return c;
}
static Glyph *glyph(UI *u,unsigned cp,int px){
 Glyph *g=&u->glyphs[0];
 for(unsigned i=0;i<GLYPH_SLOTS;i++){
  Glyph *v=&u->glyphs[i];if(v->cp==cp&&v->px==px){v->stamp=++u->stamp;return v;}
  if(!v->px||v->stamp<g->stamp)g=v;
 }
 free(g->pixels);memset(g,0,sizeof(*g));g->cp=cp;g->px=px;g->stamp=++u->stamp;
 stbtt_fontinfo *font=&u->font;
 if(cp>=0xe000&&cp<=0xf8ff&&u->extension_live&&stbtt_FindGlyphIndex(&u->extension,(int)cp))font=&u->extension;
 float scale=stbtt_ScaleForPixelHeight(font,(float)px);int advance,bearing;
 stbtt_GetCodepointHMetrics(font,(int)cp,&advance,&bearing);g->advance=(int)(advance*scale+0.5f);
 g->pixels=stbtt_GetCodepointBitmap(font,0,scale,(int)cp,&g->w,&g->h,&g->xoff,&g->yoff);
 return g;
}
static int advance(UI *u,unsigned c,int px){return u->font_live?glyph(u,c,px)->advance:px*3/5;}
static int text_width(UI *u,const char *s,int px){int n=0;while(*s)n+=advance(u,codepoint(&s),px);return n;}
/* Pixel height and baseline stay consistent across labels; clipping never splits UTF-8. */
static void text(UI *u,int x,int baseline,int px,unsigned color,const char *s,int limit){
 int end=x+limit;
 while(*s){unsigned c=codepoint(&s);int a=advance(u,c,px);if(x+a>end)break;
  if(u->font_live){Glyph *g=glyph(u,c,px);if(g->pixels)for(int yy=0;yy<g->h;yy++)for(int xx=0;xx<g->w;xx++)pixel(u,x+g->xoff+xx,baseline+g->yoff+yy,color,g->pixels[yy*g->w+xx]);}
  else if(u->fallback.gfx&&u->fallback.tileWidth==16&&u->fallback.tileHeight==16){
   if(c<u->fallback.asciiOffset||c>=u->fallback.asciiOffset+u->fallback.numChars)c='?';
   const u16 *bits=(const u16 *)u->fallback.gfx+16*(c-u->fallback.asciiOffset);
   for(int yy=0;yy<px;yy++)for(int xx=0;xx<a;xx++)if(bits[yy*16/px]&(0x8000U>>(xx*16/a)))pixel(u,x+xx,baseline-px+yy,color,255);
  }
  x+=a;
 }
}
static void caption(UI *u,int x,int y,const char *s,unsigned color){
 const int width=174,px=20;const char *p=s,*cut=s,*space=NULL;int used=0;
 while(*p){const char *before=p;unsigned c=codepoint(&p);int a=advance(u,c,px);if(used+a>width){p=before;break;}used+=a;cut=p;if(c==' ')space=before;}
 if(!*p){text(u,x,y+27,px,color,s,width);return;}
 if(space&&space>s)cut=space;
 char first[256];size_t n=(size_t)(cut-s);if(n>255)n=255;memcpy(first,s,n);first[n]=0;
 text(u,x,y+17,px,color,first,width);while(*cut==' ')cut++;
 char second[256];snprintf(second,sizeof(second),"%s",cut);
 if(text_width(u,second,px)>width){
  const char *q=second,*last=second;int w=0,ellipsis=advance(u,0x2026,px);
  while(*q){const char *old=q;int a=advance(u,codepoint(&q),px);if(w+a+ellipsis>width){last=old;break;}w+=a;last=q;}
  size_t k=(size_t)(last-second);if(k>252){k=252;while(k&&((unsigned char)second[k]&0xc0)==0x80)k--;}
  memcpy(second+k,"\xe2\x80\xa6",4);
 }
 text(u,x,y+37,px,color,second,width);
}
static void folder_icon(UI *u,int x,int y,int n,int card){
 if(card){rect(u,x+2,y,n-4,n,3,0xe4e7ed);rect(u,x+4,y+2,n-8,5,1,0xdb4650);rect(u,x+5,y+10,n-10,n-14,1,0x636a76);}
 else{rect(u,x+1,y+3,n/2,6,2,0xffd559);rect(u,x,y+7,n,n-8,3,0xf2bd43);line(u,x+3,y+n-3,x+n-4,y+n-3,0xffdf73,1);}
}
static Settings settings_read(void){
 Settings s={4,0,2,0};FILE *f=fopen(CONFIG_PATH,"r");if(!f)return s;
 unsigned version,t,sort,filter,detail;
 if(fscanf(f,"NX2UI %u %u %u %u %u",&version,&t,&sort,&filter,&detail)==5&&version==1&&t<sizeof(themes)/sizeof(*themes)&&sort<4&&filter<4&&detail<2)s=(Settings){t,sort==1?1:0,2,detail};
 fclose(f);return s;
}
static int settings_write(UI *u){
 mkdir("sdmc:/switch/BackupLoaderMKW",0777);FILE *f=fopen(CONFIG_PATH ".tmp","w");if(!f)return 0;
 int ok=fprintf(f,"NX2UI 1 %u %u %u %u\n",u->settings.theme,u->settings.sort,u->settings.filter,u->settings.detailed)>0;
 if(fflush(f))ok=0;if(fclose(f))ok=0;if(!ok){remove(CONFIG_PATH ".tmp");return 0;}
 remove(CONFIG_PATH);if(rename(CONFIG_PATH ".tmp",CONFIG_PATH)){remove(CONFIG_PATH ".tmp");return 0;}return 1;
}
static UI *sort_ui;
static int compare(const void *aa,const void *bb){
 size_t a=*(const size_t *)aa,b=*(const size_t *)bb;UI *u=sort_ui;
 int af=u->catalog->items[a].is_nup!=0,bf=u->catalog->items[b].is_nup!=0;
 if(u->settings.sort>=2&&af!=bf)return u->settings.sort==2?af-bf:bf-af;
 const unsigned char *x=(const unsigned char *)u->titles[a],*y=(const unsigned char *)u->titles[b];int n=0;
 while(*x&&*y){n=tolower(*x)-tolower(*y);if(n)break;x++;y++;}if(!n)n=*x-*y;
 if(!n)n=strcmp(u->catalog->items[a].root,u->catalog->items[b].root);
 return u->settings.sort==1?-n:n;
}
static void view_build(UI *u){
 size_t wanted=u->count?u->view[u->selected]:SIZE_MAX;u->count=0;
 for(size_t i=0;i<u->catalog->count;i++){unsigned bit=u->catalog->items[i].is_nup?2:1;if(u->settings.filter&bit)u->view[u->count++]=i;}
 sort_ui=u;qsort(u->view,u->count,sizeof(*u->view),compare);sort_ui=NULL;
 u->selected=0;for(size_t i=0;i<u->count;i++)if(u->view[i]==wanted)u->selected=i;
 u->first=u->selected/5>2?u->selected/5-2:0;u->dirty=1;
}
static void status_read(UI *u){
 u->battery=-1;u->network=-1;u->free_bytes=-1;u->total_bytes=-1;
 FsFileSystem *sd=fsdevGetDeviceFileSystem("sdmc");
 if(sd){if(R_FAILED(fsFsGetFreeSpace(sd,"/",&u->free_bytes)))u->free_bytes=-1;if(R_FAILED(fsFsGetTotalSpace(sd,"/",&u->total_bytes)))u->total_bytes=-1;}
 if(u->psm_live){u32 charge;PsmChargerType charger;
  if(R_SUCCEEDED(psmGetBatteryChargePercentage(&charge))&&charge<=100)u->battery=(int)charge;
  u->charging=R_SUCCEEDED(psmGetChargerType(&charger))&&charger!=PsmChargerType_Unconnected;
 }
 if(u->nifm_live){NifmInternetConnectionType type;NifmInternetConnectionStatus status;u32 strength;
  if(R_SUCCEEDED(nifmGetInternetConnectionStatus(&type,&strength,&status))){u->network=status==NifmInternetConnectionStatus_Connected?(int)type:0;u->strength=(int)strength;}
 }
 u->status_tick=armGetSystemTick();u->dirty=1;
}
static Icon *icon_lookup(UI *u,size_t item){for(unsigned i=0;i<ICON_SLOTS;i++)if(u->icons[i].used&&u->icons[i].item==item){u->icons[i].stamp=++u->stamp;return &u->icons[i];}return NULL;}
static int icon_load(UI *u){
 /* One visible icon per iteration; no decoder/file/worker survives handoff. */
 for(size_t row=u->first;row<u->first+3;row++)for(size_t col=0;col<5;col++){
  size_t at=row*5+col;if(at>=u->count)return 0;size_t item=u->view[at];if(icon_lookup(u,item))continue;
  Icon *slot=&u->icons[0];for(unsigned k=0;k<ICON_SLOTS;k++)if(!u->icons[k].used||u->icons[k].stamp<slot->stamp)slot=&u->icons[k];
  free(slot->pixels);*slot=(Icon){.item=item,.used=1,.stamp=++u->stamp};
  unsigned char *encoded=NULL;size_t bytes=0;char title[256];nx2_ui_media(&u->catalog->items[item],title,&encoded,&bytes);
  if(encoded&&bytes<=(4U<<20)){int w,h,channels;
   unsigned char *decoded=stbi_load_from_memory(encoded,(int)bytes,&w,&h,&channels,4);
   if(decoded&&w>0&&h>0&&w<=2048&&h<=2048){
    slot->pixels=malloc(TILE_W*ART_H*4);
    if(slot->pixels){float scale=fmaxf((float)TILE_W/w,(float)ART_H/h);
     for(int y=0;y<ART_H;y++)for(int x=0;x<TILE_W;x++){
      int sx=(int)((x-TILE_W/2)/scale+w/2),sy=(int)((y-ART_H/2)/scale+h/2);
      if(sx<0)sx=0;if(sx>=w)sx=w-1;if(sy<0)sy=0;if(sy>=h)sy=h-1;
      memcpy(slot->pixels+4*(y*TILE_W+x),decoded+4*(sy*w+sx),4);
     }
    }
   }
   stbi_image_free(decoded);
  }
  free(encoded);u->dirty=1;return 1;
 }
 return 0;
}
static void art(UI *u,int x,int y,size_t item,Icon *icon){
 const Theme *t=theme(u);unsigned color=mix(t->accent,t->card,200);
 for(int row=0;row<ART_H;row++)for(int col=0;col<TILE_W;col++){
  unsigned c=mix(color,t->panel,(unsigned)(row*95/ART_H));
  if(icon&&icon->pixels){const unsigned char *p=icon->pixels+4*(row*TILE_W+col);c=(p[0]<<16)|(p[1]<<8)|p[2];}
  unsigned alpha=round_alpha(col,row,TILE_W,ART_H+12,12);
  if(alpha)pixel(u,x+col,y+row,c,alpha);
 }
 if(!icon||!icon->pixels){
  char initial[5]={0};const char *p=u->titles[item],*start=p;codepoint(&p);
  size_t n=(size_t)(p-start);if(n<=4)memcpy(initial,start,n);
  int w=text_width(u,initial,54);text(u,x+(TILE_W-w)/2,y+84,54,mix(t->text,color,90),initial,TILE_W);
 }
}
static int has_symbol(UI *u,unsigned cp){
 return (u->extension_live&&stbtt_FindGlyphIndex(&u->extension,(int)cp))||
        (u->font_live&&stbtt_FindGlyphIndex(&u->font,(int)cp));
}
static void symbol(UI *u,int x,int y,int w,int h,unsigned cp,int px,unsigned color){
 Glyph *g=glyph(u,cp,px);if(!g->pixels)return;
 int xx=x+(w-g->w)/2,yy=y+(h-g->h)/2;
 for(int j=0;j<g->h;j++)for(int i=0;i<g->w;i++)pixel(u,xx+i,yy+j,color,g->pixels[j*g->w+i]);
}
static void button(UI *u,int x,int y,const char *key,const char *label){
 const Theme *t=theme(u);unsigned filled=0,outline=0;
 if(*key=='A'){filled=0xe0a0;outline=0xe0e0;}
 else if(*key=='B'){filled=0xe0a1;outline=0xe0e1;}
 else if(*key=='X'){filled=0xe0a2;outline=0xe0e2;}
 else if(*key=='Y'){filled=0xe0a3;outline=0xe0e3;}
 else if(*key=='-'){filled=0xe0b4;outline=0xe0f0;}
 if(has_symbol(u,filled))symbol(u,x,y-27,30,30,filled,34,t->text);
 else if(has_symbol(u,outline))symbol(u,x,y-27,30,30,outline,34,t->text);
 else{
  rect(u,x,y-27,28,28,14,t->text);
  if(*key=='-')rect(u,x+7,y-14,14,2,1,t->panel);
  else if(u->font_live)symbol(u,x,y-27,28,28,(unsigned char)*key,23,t->panel);
  else text(u,x+7,y-7,21,t->panel,key,23);
 }
 text(u,x+39,y-3,23,t->text,label,113);
}
static void header(UI *u){
 const Theme *t=theme(u);char s[160];
 rect(u,0,0,WIDTH,59,0,t->panel);rect(u,0,58,WIDTH,1,0,mix(t->text,t->panel,205));
 /* Balanced Joy-Con silhouette, drawn with edge coverage rather than pixel blocks. */
 rect(u,31,14,23,34,10,t->text);rect(u,45,14,9,34,0,t->text);
 rect(u,34,17,17,28,7,t->panel);rect(u,45,17,6,28,0,t->panel);
 rect(u,38,22,7,7,4,t->text);
 rect(u,59,14,21,34,10,t->text);rect(u,59,14,8,34,0,t->text);
 rect(u,65,33,7,7,4,t->panel);text(u,84,45,38,t->text,"2",24);
 rect(u,108,13,1,32,0,t->muted);text(u,126,42,31,t->text,"BackupLoaderMKW",303);
 rect(u,444,13,1,32,0,t->muted);text(u,465,38,21,t->text,"Library:",76);
 snprintf(s,sizeof(s),"%zu Games",u->catalog->count);text(u,541,38,21,t->text,s,112);
 rect(u,662,13,1,32,0,t->muted);
 if(u->free_bytes>=0&&u->total_bytes>=0)snprintf(s,sizeof(s),"microSD: %.1f GB free / %.1f GB",u->free_bytes/1e9,u->total_bytes/1e9);
 else snprintf(s,sizeof(s),"microSD: unavailable");text(u,681,38,21,t->text,s,357);
 rect(u,1048,13,1,32,0,t->muted);
 if(u->network==2){rect(u,1067,24,26,18,3,t->text);rect(u,1071,27,18,9,0,t->panel);line(u,1080,42,1080,47,t->text,2);}
 else if(u->network==1){arc(u,1081,44,20,2.5f,t->text);arc(u,1081,44,13,2.5f,t->text);arc(u,1081,44,6,2.5f,t->text);rect(u,1078,42,6,6,3,t->text);}
 else{text(u,1065,40,21,t->muted,u->network<0?"?":"--",32);}
 if(u->battery>=0)snprintf(s,sizeof(s),"%d%%%s",u->battery,u->charging?"+":"");else snprintf(s,sizeof(s),"--%%");
 text(u,1110,38,22,t->text,s,80);rect(u,1201,21,39,21,4,t->text);rect(u,1204,24,33,15,1,t->panel);rect(u,1240,27,4,9,1,t->text);
 if(u->battery>=0)rect(u,1206,26,29*u->battery/100,11,1,t->text);
}
static void modal_draw(UI *u){
 if(!u->modal)return;const Theme *t=theme(u);
 for(int y=0;y<HEIGHT;y++)for(int x=0;x<WIDTH;x++)pixel(u,x,y,0,150);
 int height=u->modal==2?320:u->modal==1?230:435;
 rect(u,367,150,546,height,16,t->panel);text(u,397,196,30,t->text,u->modal==1?"Filter":u->modal==2?"Sort":"Options",480);
 unsigned count=u->modal==1?2:u->modal==2?2:5;
 for(unsigned i=0;i<count;i++){
  int y=219+(int)i*44;char label[160];
  if(u->modal==1)snprintf(label,sizeof(label),"[%s] %s",(u->settings.filter&(1U<<i))?"x":" ",i?"NUP":"Folder");
  else if(u->modal==2)snprintf(label,sizeof(label),"%s%s",sort_names[i],u->settings.sort==i?"  (selected)":"");
  else if(i==0)snprintf(label,sizeof(label),"Theme: < %s >",t->name);
  else if(i==1)snprintf(label,sizeof(label),"Diagnostics: %s",u->settings.detailed?"Verbose":"Minimal");
  else snprintf(label,sizeof(label),"%s",i==2?"Refresh library":i==3?"Export last system crash report":"Back");
  if((unsigned)u->choice==i)rect(u,387,y-23,506,39,8,mix(t->panel,t->accent,75));
  text(u,405,y+4,24,t->text,label,471);
 }
 if(u->modal==3){for(unsigned i=0;i<sizeof(themes)/sizeof(*themes);i++){rect(u,398+(int)i*48,481,34,18,6,themes[i].accent);if(u->settings.theme==i)rect(u,398+(int)i*48,503,34,2,0,t->text);}}
 text(u,398,150+height-18,19,t->muted,"A Select    B Back    Left / Right Change",490);
}
static void draw(UI *u){
 u32 stride;u->pixels=framebufferBegin(&u->fb,&stride);
 if(!u->pixels){framebufferEnd(&u->fb);return;}u->stride=stride/4;
 const Theme *t=theme(u);
 for(int y=0;y<HEIGHT;y++)rect(u,0,y,WIDTH,1,0,mix(t->bg,t->panel,(unsigned)(y*55/HEIGHT)));
 header(u);
 for(size_t r=0;r<3;r++)for(size_t c=0;c<5;c++){
  size_t at=(u->first+r)*5+c;if(at>=u->count)continue;size_t item=u->view[at];int x=48+(int)c*240,y=86+(int)r*190;
  if(at==u->selected){rect(u,x-7,y-7,TILE_W+14,TILE_H+14,19,mix(t->bg,t->accent,55));rect(u,x-4,y-4,TILE_W+8,TILE_H+8,16,t->accent);rect(u,x-1,y-1,TILE_W+2,TILE_H+2,13,t->text);}
  rect(u,x,y,TILE_W,TILE_H,12,t->card);art(u,x,y,item,icon_lookup(u,item));folder_icon(u,x+12,y+ART_H+10,25,u->catalog->items[item].is_nup);
  caption(u,x+44,y+ART_H,u->titles[item],t->text);
 }
 if(!u->count){text(u,205,284,30,t->text,"No NUP games found",870);text(u,205,329,22,t->muted,"Place NUP files in /games and press X to refresh.",900);}
 size_t rows=(u->count+4)/5;rect(u,1254,86,6,554,3,mix(t->bg,t->text,55));
 if(rows>3){int h=(int)(554*3/rows);if(h<16)h=16;int y=86+(int)((554-h)*u->first/(rows-3));rect(u,1254,y,6,h,3,t->muted);}
 rect(u,0,656,WIDTH,64,0,t->panel);rect(u,0,656,WIDTH,1,0,mix(t->panel,t->text,70));
 rect(u,32,680,35,21,8,t->text);rect(u,38,685,3,11,1,t->panel);rect(u,34,689,11,3,1,t->panel);
 rect(u,55,685,4,4,2,t->panel);rect(u,60,690,4,4,2,t->panel);text(u,82,696,24,t->text,"Browse",105);
 button(u,548,698,"-","Options");button(u,713,698,"Y","Sort");button(u,850,698,"X","Refresh");button(u,992,698,"B","Back");button(u,1122,698,"A","Launch");
 if(*u->notice){rect(u,150,599,980,39,9,t->panel);text(u,170,626,21,t->text,u->notice,938);}
 modal_draw(u);framebufferEnd(&u->fb);u->dirty=0;
}
static void destroy(UI *u){
 /* No deferred rendering, decoding or SD work remains on the launch boundary. */
 for(unsigned i=0;i<ICON_SLOTS;i++)free(u->icons[i].pixels);
 for(unsigned i=0;i<GLYPH_SLOTS;i++)free(u->glyphs[i].pixels);
 if(u->fb_live){framebufferClose(&u->fb);nwindowReleaseVi2Layer(nwindowGetDefault());}
 nx2_ui_media_exit();
 if(u->pl_live)plExit();if(u->nifm_live)nifmExit();if(u->psm_live)psmExit();
 free(u->titles);free(u->view);free(u);
}
static int failed(UI *u,const char *phase,Result result){
 FILE *f=fopen("sdmc:/BackupLoaderMKW-ui.log","w");
 if(f){fprintf(f,"GUI_FAILURE version=1 phase=%s result=%08x\n",phase,result);fclose(f);}
 if(u)destroy(u);return -1;
}
static int launch(UI *u){
 if(!u->count)return 0;const Nx2Game *g=&u->catalog->items[u->view[u->selected]];
 if(g->is_nup==2){snprintf(u->notice,sizeof(u->notice),"NUP unavailable: %.95s",g->nup_status);return 0;}
 if(!envHasNextLoad()){snprintf(u->notice,sizeof(u->notice),"This homebrew menu does not support loading the runtime.");return 0;}
 FILE *f=fopen(NX2_CORE_PATH,"rb");if(!f){snprintf(u->notice,sizeof(u->notice),"Runtime missing. Copy the entire release/switch folder to the SD card.");return 0;}fclose(f);
 AccountUid *selected=envGetUserIdStorage();
 unsigned char uid[sizeof(AccountUid)]={0};
 if(selected)memcpy(uid,selected,sizeof(uid));
 int have_uid=0;for(size_t k=0;k<sizeof(uid);k++)if(uid[k])have_uid=1;
 if(!have_uid){snprintf(u->notice,sizeof(u->notice),"No host profile available. Reopen homebrew from a selected user.");return 0;}
 if(!nx2_launch_request_write(g,(int)u->settings.detailed)){snprintf(u->notice,sizeof(u->notice),"Could not save launch request. Check free space and SD access.");return 0;}
 Result rc=envSetNextLoad(NX2_CORE_PATH,NX2_CORE_PATH " --ui-launch");
 if(R_FAILED(rc)){remove(NX2_REQUEST_PATH);snprintf(u->notice,sizeof(u->notice),"Runtime handoff failed: %08x",rc);return 0;}
 return 1;
}
static int export_report(UI *u){
 if(!envHasNextLoad()){snprintf(u->notice,sizeof(u->notice),"Runtime handoff is unavailable in this homebrew menu.");return 0;}
 FILE *f=fopen(NX2_CORE_PATH,"rb");if(!f){snprintf(u->notice,sizeof(u->notice),"Runtime missing. Copy the entire release/switch folder.");return 0;}fclose(f);
 remove(NX2_REQUEST_PATH);
 Result rc=envSetNextLoad(NX2_CORE_PATH,NX2_CORE_PATH " --ui-report");
 if(R_FAILED(rc)){snprintf(u->notice,sizeof(u->notice),"Report handoff failed: %08x",rc);return 0;}
 return 1;
}

int nx2_library_ui(Nx2Catalog *catalog,ConsoleFont fallback){
 UI *u=calloc(1,sizeof(*u));if(!u)return failed(NULL,"ui_allocation",UINT32_MAX);u->catalog=catalog;u->fallback=fallback;u->settings=settings_read();
 size_t count=catalog->count?catalog->count:1;
 u->titles=calloc(count,sizeof(*u->titles));u->view=calloc(count,sizeof(*u->view));
 if(!u->titles||!u->view)return failed(u,"catalog_allocation",UINT32_MAX);
 u->pl_live=R_SUCCEEDED(plInitialize(PlServiceType_User));
 if(u->pl_live){PlFontData data;if(R_SUCCEEDED(plGetSharedFontByType(&data,PlSharedFontType_Standard))&&data.address&&data.size>12){int offset=stbtt_GetFontOffsetForIndex(data.address,0);if(offset>=0&&(unsigned)offset<data.size)u->font_live=stbtt_InitFont(&u->font,data.address,offset);}}
 if(u->pl_live){PlFontData data;
  if(R_SUCCEEDED(plGetSharedFontByType(&data,PlSharedFontType_NintendoExt))&&data.address&&data.size>12){
   int offset=stbtt_GetFontOffsetForIndex(data.address,0);
   if(offset>=0&&(unsigned)offset<data.size)u->extension_live=stbtt_InitFont(&u->extension,data.address,offset);
  }
 }
 if(!u->font_live&&!fallback.gfx)return failed(u,"font_unavailable",UINT32_MAX);
 u->psm_live=R_SUCCEEDED(psmInitialize());u->nifm_live=R_SUCCEEDED(nifmInitialize(NifmServiceType_User));
 Result rc=framebufferCreate(&u->fb,nwindowGetDefault(),WIDTH,HEIGHT,PIXEL_FORMAT_RGBA_8888,2);
 if(R_FAILED(rc)){nwindowReleaseVi2Layer(nwindowGetDefault());return failed(u,"framebuffer_create",rc);}u->fb_live=1;
 rc=framebufferMakeLinear(&u->fb);if(R_FAILED(rc))return failed(u,"framebuffer_linear",rc);
 PadState pad;padConfigureInput(1,HidNpadStyleSet_NpadStandard);padInitializeDefault(&pad);
 status_read(u);int action=0;
 for(size_t i=0;i<catalog->count;i++){
  nx2_ui_media(&catalog->items[i],u->titles[i],NULL,NULL);
  if(!(i%8)){snprintf(u->notice,sizeof(u->notice),"Reading game names: %zu / %zu",i+1,catalog->count);draw(u);padUpdate(&pad);if((padGetButtonsDown(&pad)&HidNpadButton_B)||!appletMainLoop()){destroy(u);return 0;}}
 }
 *u->notice=0;view_build(u);u64 repeat=0;int direction=0;
 const u64 up=HidNpadButton_Up|HidNpadButton_StickLUp,dn=HidNpadButton_Down|HidNpadButton_StickLDown,
  left=HidNpadButton_Left|HidNpadButton_StickLLeft,right=HidNpadButton_Right|HidNpadButton_StickLRight;
 while(appletMainLoop()){
  padUpdate(&pad);u64 down=padGetButtonsDown(&pad),held=padGetButtons(&pad),tick=armGetSystemTick();
  int next=(held&up)?-5:(held&dn)?5:(held&left)?-1:(held&right)?1:0,move=0;
  if(next&&((down&(up|dn|left|right))||next!=direction)){move=next;repeat=tick+armNsToTicks(300000000);}
  else if(next&&tick>=repeat){move=next;repeat=tick+armNsToTicks(110000000);}direction=next;
  if(down||move)u->dirty=1;
  if(u->modal){
   int total=u->modal==1?2:u->modal==2?2:5;
   if(move==-5)u->choice=(u->choice+total-1)%total;else if(move==5)u->choice=(u->choice+1)%total;
   int changed=0;
   if(down&HidNpadButton_B)u->modal=0;
   else if(u->modal==3&&u->choice==0&&(move==1||move==-1)){int n=sizeof(themes)/sizeof(*themes);u->settings.theme=(u->settings.theme+n+(move==-1?-1:1))%n;changed=1;}
   else if((down&HidNpadButton_A)&&!(u->modal==3&&u->choice==0)){
    if(u->modal==1){u->settings.filter^=1U<<u->choice;view_build(u);changed=1;}
    else if(u->modal==2){u->settings.sort=(unsigned)u->choice;view_build(u);u->modal=0;changed=1;}
    else if(u->choice==1){u->settings.detailed^=1;changed=1;}
    else if(u->choice==2){action=2;break;}
    else if(u->choice==3){if(export_report(u)){action=1;break;}}
    else u->modal=0;
   }
   if(changed&&!settings_write(u))snprintf(u->notice,sizeof(u->notice),"Settings could not be saved; current choices still apply.");
  }else{
   if(down&HidNpadButton_B)break;
   if(down&HidNpadButton_X){action=2;break;}
   else if(down&HidNpadButton_Y){u->modal=2;u->choice=(int)u->settings.sort;}
   else if(down&HidNpadButton_Minus){u->modal=3;u->choice=0;}
   else if((down&HidNpadButton_A)&&launch(u)){action=1;break;}
   if(move&&u->count){
    long target=(long)u->selected+move;if(target<0)target=0;if((size_t)target>=u->count)target=(long)u->count-1;u->selected=(size_t)target;
    size_t row=u->selected/5;if(row<u->first)u->first=row;if(row>=u->first+3)u->first=row-2;
    *u->notice=0;
   }
  }
  if(armTicksToNs(tick-u->status_tick)>=5000000000ULL)status_read(u);
  if(!u->modal)icon_load(u);
  if(u->dirty)draw(u);svcSleepThread(16666667);
 }
 destroy(u);return action;
}
