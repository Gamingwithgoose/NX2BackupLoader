/* SPDX-License-Identifier: GPL-2.0-only */
#include "launch_request.h"
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#ifdef __SWITCH__
#include <switch.h>
#endif
Nx2LaunchOrigin nx2_launch_origin;
typedef struct {
 unsigned char magic[8];
 uint32_t version,bytes,checksum,detailed;
 Nx2Game game;
 Nx2LaunchOrigin origin;
} Request;
_Static_assert(offsetof(Request,game)==24,"launch request ABI");
_Static_assert(offsetof(Request,origin)==24+sizeof(Nx2Game),"launch origin ABI");
static uint32_t checksum(const void *p,size_t n){
 const unsigned char *b=p;uint32_t h=2166136261U;
 for(size_t i=0;i<n;i++)h=(h^b[i])*16777619U;
 return h;
}
static int text_ok(const char *s,size_t n,int path){
 const char *end=memchr(s,0,n);if(!end)return 0;
 if(path && *s && strncmp(s,"sdmc:/",6))return 0;
 for(const char *p=s;p<end;p++)if((unsigned char)*p<32)return 0;
 return 1;
}
static int valid(const Nx2Game *g){
 return g && *g->root && *g->name && g->is_nup>=0 && g->is_nup<=2 &&
   text_ok(g->name,sizeof(g->name),0) &&
   text_ok(g->root,sizeof(g->root),1) &&
   text_ok(g->library_root,sizeof(g->library_root),1) &&
   text_ok(g->base_root,sizeof(g->base_root),1) &&
   text_ok(g->nup_status,sizeof(g->nup_status),0);
}
int nx2_launch_request_write(const Nx2Game *game,int detailed){
 if(!valid(game)||game->is_nup==2)return 0;
 mkdir("sdmc:/switch/BackupLoaderMKW",0777);
 Request r={.magic={'N','X','2','G','U','I','0','2'},.version=2,
   .bytes=sizeof(Nx2Game),.detailed=detailed?1:0,.game=*game};
 r.origin.version=1;
#ifdef __SWITCH__
 svcGetInfo(&r.origin.host,InfoType_ProgramId,CUR_PROCESS_HANDLE,0);
 svcGetProcessId(&r.origin.pid,CUR_PROCESS_HANDLE);
 r.origin.query_result=svcGetInfo(&r.origin.system_resource,InfoType_SystemResourceSizeTotal,CUR_PROCESS_HANDLE,0);
#else
 r.origin.query_result=UINT32_MAX;
#endif
 r.checksum=checksum((const unsigned char *)&r+offsetof(Request,game),sizeof(r)-offsetof(Request,game));
 FILE *f=fopen(NX2_REQUEST_PATH ".tmp","wb");if(!f)return 0;
 int ok=fwrite(&r,1,sizeof(r),f)==sizeof(r);
 if(fflush(f))ok=0;
 if(fclose(f))ok=0;
 if(!ok){remove(NX2_REQUEST_PATH ".tmp");return 0;}
 remove(NX2_REQUEST_PATH);
 if(rename(NX2_REQUEST_PATH ".tmp",NX2_REQUEST_PATH)){
  remove(NX2_REQUEST_PATH ".tmp");return 0;
 }
 return 1;
}
int nx2_launch_request_read(Nx2Game *out,int *detailed){
 if(!out||!detailed)return 0;
 Request r;
 FILE *f=fopen(NX2_REQUEST_PATH,"rb");if(!f)return 0;
 size_t n=fread(&r,1,sizeof(r),f);int extra=fgetc(f),closed=fclose(f);
 if(n!=sizeof(r)||extra!=EOF||closed||memcmp(r.magic,"NX2GUI02",8)||
    r.version!=2||r.bytes!=sizeof(Nx2Game)||r.detailed>1||r.origin.version!=1||
    r.checksum!=checksum((const unsigned char *)&r+offsetof(Request,game),sizeof(r)-offsetof(Request,game))||!valid(&r.game))return 0;
 *out=r.game;*detailed=(int)r.detailed;nx2_launch_origin=r.origin;return 1;
}
