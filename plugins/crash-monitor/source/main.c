/* SPDX-License-Identifier: GPL-2.0-only */
#include <switch.h>
#include <kaihen/plugin.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include "monitor_wire.h"
u32 __nx_applet_type=AppletType_None;
size_t __nx_heap_size=0x400000;
void __appInit(void){}
void __appExit(void){}
static FsFileSystem sd;
static void append(const char *line,size_t n){
    const char *path="/kaihen/nx2-crash-monitor.log";
    FsFile f={0};
    if(R_FAILED(fsFsOpenFile(&sd,path,FsOpenMode_Write|FsOpenMode_Append,&f))){
        fsFsCreateFile(&sd,path,0,0);
        if(R_FAILED(fsFsOpenFile(&sd,path,FsOpenMode_Write|FsOpenMode_Append,&f)))return;
    }
    s64 size=0;
    if(R_SUCCEEDED(fsFileGetSize(&f,&size))){
        if(size+(s64)n>8388608){
            fsFileClose(&f);
            fsFsDeleteFile(&sd,"/kaihen/nx2-crash-monitor.previous.log");
            if(R_FAILED(fsFsRenameFile(&sd,path,"/kaihen/nx2-crash-monitor.previous.log")))return;
            if(R_FAILED(fsFsCreateFile(&sd,path,0,0)))return;
            if(R_FAILED(fsFsOpenFile(&sd,path,FsOpenMode_Write|FsOpenMode_Append,&f)))return;
            size=0;
        }
        if(R_SUCCEEDED(fsFileSetSize(&f,size+n)))
            fsFileWrite(&f,size,line,n,FsWriteOption_Flush);
    }
    fsFileClose(&f);
}
static void note(const char *fmt,...){
    char buf[512];int p=snprintf(buf,sizeof(buf),"tick=%llu ",(unsigned long long)armGetSystemTick());
    if(p<0||p>=(int)sizeof(buf))return;
    va_list ap;va_start(ap,fmt);
    int k=vsnprintf(buf+p,sizeof(buf)-p,fmt,ap);va_end(ap);
    if(k<0)return;
    size_t len=(size_t)p+(size_t)k;
    if(len>=sizeof(buf)-1)len=sizeof(buf)-2;
    buf[len++]='\n';append(buf,len);
}
static void log_line(const char *kind,const Nx2MonitorSample *s,Result rc){
    note("%s pid=%llu title=%016llx phase=%u stage=%u frames=%llu sample=%llu rc=%08x",
        kind,(unsigned long long)s->pid,(unsigned long long)s->title,s->phase,s->stage,
        (unsigned long long)s->frames,(unsigned long long)s->tick,(unsigned)rc);
}
typedef struct {unsigned poll, global, jit, processes, memory, loader, timeout, startup;} Config;
static Config cfg={2,1,1,1,1,1,20,180};
static void reload_config(void){
    FsFile f={0};char buf[1024];u64 got=0;
    if(R_FAILED(fsFsOpenFile(&sd,"/kaihen/nx2-crash.ini",FsOpenMode_Read,&f)))return;
    Result rc=fsFileRead(&f,0,buf,sizeof(buf)-1,FsReadOption_None,&got);
    fsFileClose(&f);if(R_FAILED(rc)||got>=sizeof(buf))return;
    buf[got]=0;
    Config c={2,1,1,1,1,1,20,180};
    char *line=buf;
    while(line && *line){
        char *next=strchr(line,'\n');if(next)*next++=0;
        char key[32];unsigned v;
        if(sscanf(line," %31[a-z_]=%u",key,&v)==2){
            if(!strcmp(key,"poll_seconds")&&v>=2&&v<=30)c.poll=v;
            else if(!strcmp(key,"global"))c.global=!!v;
            else if(!strcmp(key,"jit"))c.jit=!!v;
            else if(!strcmp(key,"processes"))c.processes=!!v;
            else if(!strcmp(key,"memory"))c.memory=!!v;
            else if(!strcmp(key,"loader"))c.loader=!!v;
            else if(!strcmp(key,"timeout_seconds")&&v>=10&&v<=600)c.timeout=v;
            else if(!strcmp(key,"startup_seconds")&&v>=30&&v<=900)c.startup=v;
        }
        line=next;
    }
    cfg=c;
}
#define MAX_PIDS 256
static u64 old_pids[MAX_PIDS],new_pids[MAX_PIDS],old_jit[64],new_jit[64];
static unsigned old_count,old_jit_count;
static int have_list,have_jit,pm_ready;
static u64 old_app,old_title;
static Result old_list_rc=~0u,old_jit_rc=~0u,old_app_rc=~0u;
static int contains(const u64 *list,unsigned n,u64 pid){for(unsigned i=0;i<n;i++)if(list[i]==pid)return 1;return 0;}
static u64 identify(u64 pid){u64 title=0;if(pm_ready)pmdmntGetProgramId(&title,pid);return title;}
static void global_poll(void){
    if(!pm_ready){
        Result rc=pmdmntInitialize();
        static Result previous=~0u;
        if(R_FAILED(rc)&&rc!=previous)note("MONITOR_PM_INIT_FAILED rc=%08x",(unsigned)rc);
        previous=rc;
        pm_ready=R_SUCCEEDED(rc);
    }
    if(cfg.processes){
        s32 count=0;Result rc=svcGetProcessList(&count,new_pids,MAX_PIDS);
        if(rc!=old_list_rc){if(R_FAILED(rc))note("MONITOR_PROCESS_LIST_FAILED rc=%08x",(unsigned)rc);old_list_rc=rc;}
        if(R_SUCCEEDED(rc)&&count>=0&&count<=MAX_PIDS){
            /* Process disappearance alone does not establish a crash. */
            memcpy(old_pids,new_pids,count*sizeof(u64));old_count=count;have_list=1;
        }
    }
    if(pm_ready){
        u64 app=0;Result rc=pmdmntGetApplicationProcessId(&app);
        if(rc!=old_app_rc||app!=old_app){

            old_app=R_SUCCEEDED(rc)?app:0;old_title=identify(old_app);
            old_app_rc=rc;
        }
        if(cfg.jit){
            u32 count=0;rc=pmdmntGetJitDebugProcessIdList(&count,new_jit,64);
            if(rc!=old_jit_rc){if(R_FAILED(rc))note("MONITOR_JIT_QUERY_FAILED rc=%08x",(unsigned)rc);old_jit_rc=rc;}
            if(R_SUCCEEDED(rc)&&count<=64){
                for(u32 i=0;i<count;i++)if(!have_jit||!contains(old_jit,old_jit_count,new_jit[i]))note("JIT_DEBUG_PENDING pid=%llu title=%016llx",(unsigned long long)new_jit[i],(unsigned long long)identify(new_jit[i]));
                memcpy(old_jit,new_jit,count*sizeof(u64));old_jit_count=count;have_jit=1;
            }
        }
    }
}
static void memory_poll(void){
    u64 total=0,used=0,self=0;Result a=svcGetSystemInfo(&total,SystemInfoType_TotalPhysicalMemorySize,0,0);
    Result b=svcGetSystemInfo(&used,SystemInfoType_UsedPhysicalMemorySize,0,0);
    Result c=svcGetInfo(&self,InfoType_UsedMemorySize,CUR_PROCESS_HANDLE,0);
    s64 free=0;Result d=fsFsGetFreeSpace(&sd,"/",&free);
    static Result last_a=0,last_b=0,last_c=0,last_d=0;
    if((a!=last_a||b!=last_b||c!=last_c||d!=last_d)&&(R_FAILED(a)||R_FAILED(b)||R_FAILED(c)||R_FAILED(d)))note("MONITOR_MEMORY_QUERY_FAILED results=%08x,%08x,%08x,%08x",(unsigned)a,(unsigned)b,(unsigned)c,(unsigned)d);
    last_a=a;last_b=b;last_c=c;last_d=d;
}
static int read_sample(Nx2MonitorSample *s){
    FsFile f={0};u64 got=0;
    if(R_FAILED(fsFsOpenFile(&sd,"/kaihen/nx2-monitor.sample",FsOpenMode_Read,&f)))return 0;
    Result rc=fsFileRead(&f,0,s,sizeof(*s),FsReadOption_None,&got);
    fsFileClose(&f);
    return R_SUCCEEDED(rc)&&got==sizeof(*s)&&s->magic==NX2_MONITOR_MAGIC&&s->tick==s->end_tick&&s->phase<=4;
}

/* Read append-only launcher error records, preserving partial lines until
 * the next poll. Cursor is persisted only after a complete error is copied.
 * Replaying a line after a power loss is preferable to dropping a failure. */
static s64 pending_cursor;
static void load_cursor(void){
    FsFile f={0};u64 got=0;s64 v=0;
    if(R_SUCCEEDED(fsFsOpenFile(&sd,"/kaihen/nx2-errors.cursor",FsOpenMode_Read,&f))){
        Result rc=fsFileRead(&f,0,&v,sizeof(v),FsReadOption_None,&got);fsFileClose(&f);
        if(R_SUCCEEDED(rc)&&got==sizeof(v)&&v>=0)pending_cursor=v;
    }
}
static void save_cursor(void){
    const char *path="/kaihen/nx2-errors.cursor";FsFile f={0};
    if(R_FAILED(fsFsOpenFile(&sd,path,FsOpenMode_Write,&f))){
        fsFsCreateFile(&sd,path,sizeof(pending_cursor),0);
        if(R_FAILED(fsFsOpenFile(&sd,path,FsOpenMode_Write,&f)))return;
    }
    fsFileWrite(&f,0,&pending_cursor,sizeof(pending_cursor),FsWriteOption_Flush);fsFileClose(&f);
}
static void drain_errors(void){
    FsFile f={0};s64 size=0;
    if(R_FAILED(fsFsOpenFile(&sd,"/kaihen/nx2-errors.pending",FsOpenMode_Read,&f)))return;
    if(R_FAILED(fsFileGetSize(&f,&size))){fsFileClose(&f);return;}
    if(size<pending_cursor)pending_cursor=0;
    char buf[16384];u64 got=0;
    if(size>pending_cursor && R_SUCCEEDED(fsFileRead(&f,pending_cursor,buf,sizeof(buf),FsReadOption_None,&got)) && got){
        size_t complete=0;
        for(size_t i=0;i<got;i++)if(buf[i]=='\n')complete=i+1;
        if(complete){append(buf,complete);pending_cursor+=complete;save_cursor();}
    }
    fsFileClose(&f);
}
int main(void){
    Handle ready=INVALID_HANDLE;
    Result rc=pluginRuntimeInitialize(&ready);
    if(R_FAILED(rc))return rc;
    rc=fsInitialize();
    if(R_FAILED(rc)){pluginRuntimeExit();return rc;}
    rc=fsOpenSdCardFileSystem(&sd);
    if(R_FAILED(rc)){fsExit();pluginRuntimeExit();return rc;}
    Nx2MonitorSample last={0},next={0};
    u64 boot=armGetSystemTick();
    load_cursor();
    rc=svcSignalEvent(ready);
    if(R_SUCCEEDED(rc)){
        int warned=0;
        u64 last_memory=0,last_config=0;
        for(;;){
            u64 now=armGetSystemTick();
            if(!last_config||armTicksToNs(now-last_config)>=5000000000ULL){reload_config();last_config=now;}
            drain_errors();
            if(cfg.global)global_poll();
            if(cfg.memory&&(!last_memory||armTicksToNs(now-last_memory)>=30000000000ULL)){memory_poll();last_memory=now;}
            if(cfg.loader&&read_sample(&next)&&next.tick>=boot&&next.tick<=armGetSystemTick()&&next.tick!=last.tick){
                last=next;warned=0;
            }
            if(cfg.loader&&last.tick && (last.phase==1||last.phase==2) && !warned &&
               armTicksToNs(armGetSystemTick()-last.tick)> ((u64)(last.phase==1?cfg.startup:cfg.timeout)*1000000000ULL)){
                warned=1;
                /* A stalled heartbeat is not proof of a fault; query only. */
                Result p=pmdmntInitialize();
                if(R_SUCCEEDED(p)){
                    u64 program=0;
                    p=pmdmntGetProgramId(&program,last.pid);
                    if(R_FAILED(p))log_line("MONITOR_PROCESS_QUERY_FAILED",&last,p);
                    pmdmntExit();
                }else log_line("MONITOR_PROCESS_QUERY_UNAVAILABLE",&last,p);
            }
            svcSleepThread((s64)cfg.poll*1000000000LL);
        }
    }
    fsFsClose(&sd);fsExit();pluginRuntimeExit();return rc;
}
