/* SPDX-License-Identifier: GPL-2.0-only */
#include <switch.h>
#include <kaihen/plugin.h>
#include <stdio.h>
#include "monitor_wire.h"
u32 __nx_applet_type=AppletType_None;
size_t __nx_heap_size=0x400000;
void __appInit(void){}
void __appExit(void){}
static FsFileSystem sd;
static void log_line(const char *kind,const Nx2MonitorSample *s,Result rc){
    char line[256];
    int n=snprintf(line,sizeof(line),"%s now=%llu pid=%llu title=%016llx phase=%u stage=%u frames=%llu sample=%llu rc=%08x\n",
        kind,(unsigned long long)armGetSystemTick(),(unsigned long long)s->pid,
        (unsigned long long)s->title,s->phase,s->stage,(unsigned long long)s->frames,
        (unsigned long long)s->tick,(unsigned)rc);
    if(n<=0 || n>=(int)sizeof(line))return;
    const char *path="/kaihen/nx2-crash-monitor.log";
    FsFile f={0};
    if(R_FAILED(fsFsOpenFile(&sd,path,FsOpenMode_Write|FsOpenMode_Append,&f))){
        fsFsCreateFile(&sd,path,0,0);
        if(R_FAILED(fsFsOpenFile(&sd,path,FsOpenMode_Write|FsOpenMode_Append,&f)))return;
    }
    s64 size=0;
    if(R_SUCCEEDED(fsFileGetSize(&f,&size)) && size<4194304){
        if(R_SUCCEEDED(fsFileSetSize(&f,size+n)))
            fsFileWrite(&f,size,line,n,FsWriteOption_Flush);
    }
    fsFileClose(&f);
}
static int read_sample(Nx2MonitorSample *s){
    FsFile f={0};u64 got=0;
    if(R_FAILED(fsFsOpenFile(&sd,"/kaihen/nx2-monitor.sample",FsOpenMode_Read,&f)))return 0;
    Result rc=fsFileRead(&f,0,s,sizeof(*s),FsReadOption_None,&got);
    fsFileClose(&f);
    return R_SUCCEEDED(rc)&&got==sizeof(*s)&&s->magic==NX2_MONITOR_MAGIC&&s->tick==s->end_tick&&s->phase<=3;
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
    log_line("PLUGIN_STARTED",&last,0);
    rc=svcSignalEvent(ready);
    if(R_SUCCEEDED(rc)){
        int warned=0;
        for(;;){
            if(read_sample(&next)&&next.tick>=boot&&next.tick<=armGetSystemTick()&&next.tick!=last.tick){
                if(warned)log_line("HEARTBEAT_RESUMED",&next,0);
                last=next;warned=0;
                log_line("HEARTBEAT",&last,0);
            }
            if(last.tick && (last.phase==1||last.phase==2) && !warned &&
               armTicksToNs(armGetSystemTick()-last.tick)> (last.phase==1?180000000000ULL:20000000000ULL)){
                warned=1;
                log_line("HEARTBEAT_TIMEOUT_NOT_PROOF_OF_CRASH",&last,0);
                Result p=pmdmntInitialize();
                if(R_SUCCEEDED(p)){
                    u64 program=0;
                    p=pmdmntGetProgramId(&program,last.pid);
                    log_line(R_SUCCEEDED(p)?"PROCESS_QUERY_OK":"PROCESS_QUERY_FAILED",&last,p);
                    pmdmntExit();
                }else log_line("PROCESS_QUERY_UNAVAILABLE",&last,p);
            }
            svcSleepThread(2000000000LL);
        }
    }
    fsFsClose(&sd);fsExit();pluginRuntimeExit();return rc;
}
