/* SPDX-License-Identifier: GPL-2.0-only */
#include <switch.h>
#include <kaihen.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <stdarg.h>
#include <dirent.h>
#include "module.h"
#include "nso.h"
#include "fixture_hash.h"
#include "yoshi_hashes.h"
extern void nx2_tlsdesc_resolver(void);
typedef struct {void *backing;size_t image,bss;u64 mapped;} Loaded;
typedef struct {void *blocks[16];Nx2Group *group;} TlsContext;
static PadState pad;
static s32 tls_slot=-1;
static volatile unsigned tls_errors;
static uint64_t tls_guard;
static unsigned checks,failures;
static int log_error;
static char outcome[160]="A: full suite | Y: fixture suite only | PLUS: exit";
static void hash_bytes(unsigned char out[32],const void *data,size_t bytes){sha256CalculateHash(out,data,bytes);}
static void record(FILE *f,const char *format,...){va_list args;va_start(args,format);if(vfprintf(f,format,args)<0)log_error=1;va_end(args);if(fflush(f))log_error=1;}
static void test(FILE *f,const char *name,int passed){checks++;if(!passed)failures++;record(f,"%s %s\n",passed?"PASS":"FAIL",name);}
static void screen(const char *stage){consoleClear();printf("NX2 Folder Execution Suite 0.3\n\n%s\n\nReport updates after every stage.\n",stage);consoleUpdate(NULL);}
uint64_t nx2_tls_offset(const Nx2TlsArg *arg){
    TlsContext *context=tls_slot>=0?threadTlsGet(tls_slot):NULL;uintptr_t address=(uintptr_t)&tls_guard,tp;
    if(context&&arg&&arg->module<context->group->count&&context->blocks[arg->module]&&arg->offset<context->group->modules[arg->module].tls_memory_size)address=(uintptr_t)context->blocks[arg->module]+arg->offset;
    else __atomic_fetch_add(&tls_errors,1,__ATOMIC_RELAXED);
    __asm__ volatile("mrs %0,tpidr_el0":"=r"(tp));return address-tp;
}
static int tls_prepare(TlsContext *context,Nx2Group *group){
    memset(context,0,sizeof(*context));context->group=group;
    for(size_t i=0;i<group->count;i++)if(group->modules[i].tls_memory_size){
        Nx2Module *m=&group->modules[i];size_t alignment=m->tls_alignment<16?16:m->tls_alignment;
        context->blocks[i]=memalign(alignment,m->tls_memory_size);if(!context->blocks[i])return 0;
        memset(context->blocks[i],0,m->tls_memory_size);memcpy(context->blocks[i],m->guest+m->tls_address,m->tls_file_size);
    }return 1;
}
static void tls_release(TlsContext *context){for(unsigned i=0;i<16;i++){free(context->blocks[i]);context->blocks[i]=NULL;}}
static Result map(Service *ro,Loaded *l){const struct{u64 pid,source,size,bss,bss_size;}in={0,(u64)l->backing,l->image,l->bss?(u64)l->backing+l->image:0,l->bss};return serviceDispatchInOut(ro,0,in,l->mapped,.in_send_pid=true);}
static Result unmap(Service *ro,u64 address){const struct{u64 pid,address;}in={0,address};return serviceDispatchIn(ro,1,in,.in_send_pid=true);}
static void __attribute__((noreturn)) fatal(FILE *log,const char *why){record(log,"STOP_PROCESS: %s\n",why);fclose(log);svcExitProcess();}
static void unload_all(FILE *log,Service *ro,Loaded *loaded,size_t count){
    for(size_t reverse=count;reverse;reverse--){Loaded *l=&loaded[reverse-1];if(l->mapped){Result rc=unmap(ro,l->mapped);record(log,"UNMAP index=%u result=%08x\n",(unsigned)(reverse-1),rc);if(R_FAILED(rc))fatal(log,"module could not be unmapped; backing retained");l->mapped=0;}nx2_free_pages(l->backing);l->backing=NULL;}
}
static int permissions(FILE *log,Nx2Module *m){
    const unsigned expected[]={Perm_Rx,Perm_R,Perm_Rw};int ok=1;
    for(unsigned i=0;i<3;i++){
        MemoryInfo info;u32 page;Result rc=svcQueryMemory(&info,&page,(uintptr_t)m->guest+m->address[i]);
        record(log,"PERMISSION segment=%u result=%08x permission=%u expected=%u\n",i,rc,R_SUCCEEDED(rc)?info.perm:0,expected[i]);
        if(R_FAILED(rc)||info.perm!=expected[i])ok=0;
    }return ok;
}
static int fixture_read(const char *name,const char *wanted,Loaded *l){
    char path[200];snprintf(path,sizeof(path),"sdmc:/games/nx2-exec-fixture/%s.nxm",name);FILE *f=fopen(path,"rb");if(!f)return 0;
    unsigned char header[128];size_t n=fread(header,1,128,f);u32 size=0,bss=0;
    if(n==128){memcpy(&size,header+0x18,4);memcpy(&bss,header+0x38,4);}
    if(n!=128||memcmp(header+16,"NRO0",4)||!size||size>1024*1024||bss>1024*1024||((size|bss)&4095)){fclose(f);return 0;}
    l->backing=nx2_pages((size_t)size+bss);if(!l->backing){fclose(f);return 0;}l->image=size;l->bss=bss;memset(l->backing,0,(size_t)size+bss);
    rewind(f);n=fread(l->backing,1,size,f);int extra=fgetc(f),bad=ferror(f);if(fclose(f))bad=1;
    unsigned char hash[32];char text[65];sha256CalculateHash(hash,l->backing,size);for(unsigned i=0;i<32;i++)snprintf(text+2*i,3,"%02x",hash[i]);
    if(n!=size||extra!=EOF||bad||strcmp(text,wanted)){nx2_free_pages(l->backing);l->backing=NULL;return 0;}return 1;
}
static uintptr_t function(FILE *f,Nx2Module *m,const char *name){uintptr_t address=0;int rc=nx2_module_function(m,name,&address);record(f,"FUNCTION %s result=%s address=%016llx\n",name,nx2_module_error(rc),(unsigned long long)address);return rc?0:address;}
typedef struct {TlsContext context;uintptr_t entry;uint64_t input,first,second;} Worker;
static void worker(void *opaque){Worker *w=opaque;threadTlsSet(tls_slot,&w->context);uint64_t(*call)(uint64_t)=(void *)w->entry;w->first=call(w->input);w->second=call(w->input);threadTlsSet(tls_slot,NULL);}
static int wait_worker(FILE *f,Thread *thread){
    s32 index=-1;Result rc=svcWaitSynchronization(&index,&thread->handle,1,10000000000ULL);record(f,"WORKER_WAIT result=%08x\n",rc);
    if(R_FAILED(rc))fatal(f,"worker did not finish; preventing live-code unmap");
    rc=threadClose(thread);record(f,"WORKER_CLOSE result=%08x\n",rc);return R_SUCCEEDED(rc);
}
static int run_callbacks(FILE *f,Nx2Group *g,size_t index,int final){
    uintptr_t callbacks[64];size_t count=0;int rc=nx2_module_callbacks(g,index,final,callbacks,64,&count);
    record(f,"CALLBACKS module=%u final=%d count=%u status=%s\n",(unsigned)index,final,(unsigned)count,nx2_module_error(rc));if(rc)return 0;
    for(size_t i=0;i<count;i++)((void(*)(void))callbacks[i])();
    return 1;
}
static int fixture_cycle(FILE *f,Service *ro,unsigned cycle){
    screen("Running multi-module execution and TLS tests...");record(f,"\nFIXTURE_CYCLE %u BEGIN\n",cycle);
    Loaded l[2]={{0}};Nx2Module modules[2];Nx2Group g={0};g.modules=modules;g.count=2;g.tls_resolver=(uintptr_t)nx2_tlsdesc_resolver;
    TlsContext main_tls={0};Worker workers[2]={0};int main_bound=0,ok=0;unsigned before=failures;
    const char *names[]={"provider","client"},*hashes[]={PROVIDER_SHA256,CLIENT_SHA256};
    for(unsigned i=0;i<2;i++){
        int good=fixture_read(names[i],hashes[i],&l[i]);test(f,i?"client file/hash":"provider file/hash",good);if(!good)goto done;
        int rc=nx2_module_inspect(l[i].backing,l[i].image+l[i].bss,&modules[i]);test(f,"fixture metadata",rc==0);if(rc)goto done;
        Result mapped=map(ro,&l[i]);record(f,"MAP %s result=%08x address=%016llx\n",names[i],mapped,(unsigned long long)l[i].mapped);if(R_FAILED(mapped))fatal(f,"fixture map failed; retaining backing");
        rc=nx2_module_inspect((void *)l[i].mapped,l[i].image+l[i].bss,&modules[i]);test(f,"mapped fixture metadata",rc==0);if(rc)goto done;
        good=permissions(f,&modules[i]);test(f,"fixture RX/R/RW permissions",good);if(!good)goto done;
    }
    Nx2LinkStats stats;int rc=nx2_group_relocate(&g,&stats);record(f,"LINK status=%s global=%llu jump=%llu relr=%llu tls=%llu unknown=%u missing=%s\n",nx2_module_error(rc),(unsigned long long)stats.global,(unsigned long long)stats.jump,(unsigned long long)stats.relr,(unsigned long long)stats.tls,stats.tls_unknown,stats.missing);
    test(f,"cross-module linking",rc==0);if(rc)goto done;test(f,"RELR exercised",stats.relr>0);test(f,"TLS descriptors exercised",stats.tls>=2);
    if(!tls_prepare(&main_tls,&g)){test(f,"main TLS allocation",0);goto done;}threadTlsSet(tls_slot,&main_tls);main_bound=1;
    uintptr_t entry=function(f,&modules[1],"nx2_suite_entry"),ctor=function(f,&modules[1],"nx2_ctor_check"),weak=function(f,&modules[1],"nx2_weak_check"),bss=function(f,&modules[1],"nx2_bss_check"),tls=function(f,&modules[0],"nx2_provider_tls"),simd=function(f,&modules[0],"nx2_simd_tls");
    test(f,"all fixture functions resolved",entry&&ctor&&weak&&bss&&tls&&simd);if(!entry||!ctor||!weak||!bss||!tls||!simd)goto done;
    uint64_t(*call)(uint64_t)=(void *)entry;uint64_t(*get_ctor)(void)=(void *)ctor;uint64_t(*get_tls)(void)=(void *)tls;
    test(f,"constructor initially zero",get_ctor()==0);
    int initialized=run_callbacks(f,&g,0,0)&&run_callbacks(f,&g,1,0);test(f,"constructors called",initialized);if(!initialized)goto done;
    test(f,"constructor state",get_ctor()==0xcafe);test(f,"undefined weak symbol is null",((uint64_t(*)(void))weak)()==0);
    test(f,"BSS and relocated writable data",((uint64_t(*)(void))bss)()==1);
    test(f,"TLS template initialized",get_tls()==7);
    uint64_t first=call(3),second=call(3);record(f,"MAIN_CALL first=%llu second=%llu\n",(unsigned long long)first,(unsigned long long)second);
    test(f,"cross-module calls and TLS updates",first==4680&&second==4686&&get_tls()==13);
    double result=((double(*)(double))simd)(2.5);record(f,"SIMD_TLS result=%.3f expected=15.5\n",result);test(f,"TLS preserves floating argument",result==15.5);
    Thread threads[2];int created[2]={0},started[2]={0};
    for(unsigned i=0;i<2;i++){
        workers[i].entry=entry;workers[i].input=i?5:3;
        if(!tls_prepare(&workers[i].context,&g)){test(f,"worker TLS allocation",0);goto workers_done;}
        Result trc=threadCreate(&threads[i],worker,&workers[i],NULL,0x10000,0x2c,-2);record(f,"WORKER_CREATE %u result=%08x\n",i,trc);test(f,"worker thread create",R_SUCCEEDED(trc));if(R_FAILED(trc))goto workers_done;created[i]=1;
        trc=threadStart(&threads[i]);record(f,"WORKER_START %u result=%08x\n",i,trc);test(f,"worker thread start",R_SUCCEEDED(trc));if(R_FAILED(trc))goto workers_done;started[i]=1;
    }
workers_done:
    for(unsigned i=0;i<2;i++)if(created[i]){if(started[i]){test(f,"worker joined and closed",wait_worker(f,&threads[i]));record(f,"WORKER_VALUES %u first=%llu second=%llu\n",i,(unsigned long long)workers[i].first,(unsigned long long)workers[i].second);test(f,"worker TLS values",workers[i].first==(i?4684:4680)&&workers[i].second==(i?4694:4686));}else{Result trc=threadClose(&threads[i]);if(R_FAILED(trc))fatal(f,"unstarted thread close failed");}}
    test(f,"main TLS isolated from workers",get_tls()==13);test(f,"TLS resolver had no fallback",tls_errors==0);
    test(f,"destructors called",run_callbacks(f,&g,1,1)&&run_callbacks(f,&g,0,1));test(f,"destructor state reset",get_ctor()==0);ok=failures==before;
done:
    if(main_bound)threadTlsSet(tls_slot,NULL);
    tls_release(&main_tls);for(unsigned i=0;i<2;i++)tls_release(&workers[i].context);
    unload_all(f,ro,l,2);test(f,"fixture cleanup completed",1);record(f,"FIXTURE_CYCLE %u END status=%s\n",cycle,ok?"PASS":"FAIL");return ok;
}
static int find_yoshi(char root[1024]){
    DIR *directory=opendir("sdmc:/games");if(!directory)return 0;struct dirent *entry;int found=0;
    while((entry=readdir(directory))&&!found){
        if(!strcmp(entry->d_name,".")||!strcmp(entry->d_name,".."))continue;
        for(unsigned nested=0;nested<2&&!found;nested++){
            char path[1200];int n=snprintf(root,1024,"sdmc:/games/%s%s",entry->d_name,nested?"/base":"");if(n<0||n>=1024)continue;
            n=snprintf(path,sizeof(path),"%s/exefs/main",root);if(n<0||(size_t)n>=sizeof(path))continue;FILE *file=fopen(path,"rb");if(!file)continue;
            unsigned char header[96];size_t got=fread(header,1,sizeof(header),file);fclose(file);
            static const unsigned char id[16]={0x65,0x2b,0xfd,0xc0,0x37,0x5c,0x2f,0x66,0x3e,0x56,0x78,0x9d,0xa6,0x4c,0x7b,0xde};
            if(got==sizeof(header)&&!memcmp(header,"NSO0",4)&&!memcmp(header+0x40,id,16))found=1;
        }
    }closedir(directory);return found;
}
static void yoshi_test(FILE *f,Service *ro){
    char root[1024];int found=find_yoshi(root);test(f,"Yoshi folder found",found);if(!found){record(f,"YOSHI_BLOCKED folder not found\n");return;}
    record(f,"YOSHI_ROOT %s\n",root);Loaded loaded[6]={{0}};Nx2Module modules[6];Nx2Group g={0};g.modules=modules;g.count=6;g.tls_resolver=(uintptr_t)nx2_tlsdesc_resolver;size_t count=0;
    for(unsigned i=0;i<6;i++){
        char message[160],path[1200],stage[200];snprintf(stage,sizeof(stage),"Yoshi: decompressing and mapping %s (%u/6)...",yoshi_files[i].name,i+1);screen(stage);record(f,"YOSHI_PREPARE %s\n",yoshi_files[i].name);
        if(!appletMainLoop()){record(f,"YOSHI_CANCELLED app exit requested\n");goto done;}
        snprintf(path,sizeof(path),"%s/exefs/%s",root,yoshi_files[i].name);int good=nx2_prepare_nso(path,yoshi_files[i].sha,hash_bytes,&loaded[i].backing,&loaded[i].image,&loaded[i].bss,message);record(f,"NSO %s status=%s image=%llu bss=%llu\n",yoshi_files[i].name,good?"OK":message,(unsigned long long)loaded[i].image,(unsigned long long)loaded[i].bss);count=i+1;test(f,"Yoshi NSO full hash/decompression/segment hashes",good);if(!good)goto done;
        int rc=nx2_module_inspect(loaded[i].backing,loaded[i].image+loaded[i].bss,&modules[i]);test(f,"Yoshi dynamic metadata",rc==0);if(rc){record(f,"YOSHI_PARSE %s\n",nx2_module_error(rc));goto done;}
        Result mapped=map(ro,&loaded[i]);record(f,"YOSHI_MAP %s result=%08x address=%016llx\n",yoshi_files[i].name,mapped,(unsigned long long)loaded[i].mapped);if(R_FAILED(mapped))fatal(f,"Yoshi map failed; retaining backing");
        rc=nx2_module_inspect((void *)loaded[i].mapped,loaded[i].image+loaded[i].bss,&modules[i]);test(f,"Yoshi mapped metadata",rc==0);if(rc)goto done;
        good=permissions(f,&modules[i]);test(f,"Yoshi RX/R/RW permissions",good);if(!good)goto done;
    }
    screen("Validating Yoshi cross-module relocations (no guest entrypoint)...");
    Nx2LinkStats stats;int rc=nx2_group_validate(&g,&stats);record(f,"YOSHI_LINK_VALIDATION status=%s abs64=%llu global=%llu jump=%llu relative=%llu relr=%llu tls=%llu unknown_tls=%u missing=%s\n",nx2_module_error(rc),(unsigned long long)stats.abs64,(unsigned long long)stats.global,(unsigned long long)stats.jump,(unsigned long long)stats.relative,(unsigned long long)stats.relr,(unsigned long long)stats.tls,stats.tls_unknown,stats.missing);
    test(f,"Yoshi non-TLS linking validation",rc==NX2_TLS_LAYOUT&&stats.tls_unknown==1&&stats.relr>0&&!stats.missing[0]);
    if(rc!=NX2_TLS_LAYOUT||stats.tls_unknown!=1||stats.missing[0])goto done;
    /* All six files passed exact compressed and decompressed hashes above.
     * Offline comparison of the original rtld established this one zero pair
     * is preserved; this does not make the callback safe to invoke. */
    const Nx2PreservedTls preserved={2,1152944};g.preserved_tls=&preserved;g.preserved_tls_count=1;
    screen("Applying Yoshi relocations (no game entrypoint)...");
    rc=nx2_group_relocate(&g,&stats);
    record(f,"YOSHI_RELOCATE status=%s abs64=%llu global=%llu jump=%llu relative=%llu relr=%llu tls=%llu preserved=%u unknown=%u missing=%s\n",nx2_module_error(rc),(unsigned long long)stats.abs64,(unsigned long long)stats.global,(unsigned long long)stats.jump,(unsigned long long)stats.relative,(unsigned long long)stats.relr,(unsigned long long)stats.tls,stats.tls_preserved,stats.tls_unknown,stats.missing);
    test(f,"Yoshi complete relocation applied",rc==0&&stats.tls_preserved==1&&!stats.tls_unknown);if(rc)goto done;
    u64 pair[2];memcpy(pair,modules[2].guest+1152944,sizeof(pair));test(f,"Yoshi anonymous TLS matches original rtld",!pair[0]&&!pair[1]);
    record(f,"YOSHI_NATIVE_STARTUP NOT_CALLED: relocation applied. Anonymous TLS pair preserved exactly as observed in original rtld. Native SDK environment, RomFS routing and save isolation remain unimplemented.\n");
done:screen("Releasing Yoshi test mappings...");unload_all(f,ro,loaded,count);test(f,"Yoshi mapping cleanup completed",1);
}
static void run(int full){
    checks=failures=log_error=0;tls_errors=0;FILE *f=fopen("sdmc:/nx2-folder-exec-report.txt","w");if(!f){snprintf(outcome,sizeof(outcome),"Cannot write report; test did not run.");return;}
    record(f,"NX2 Folder Execution Suite 0.3\nScope: owned multi-module execution; Yoshi full relocation and startup capability survey. No Yoshi entrypoint.\n");
    u64 title=0;Result rc=svcGetInfo(&title,InfoType_ProgramId,CUR_PROCESS_HANDLE,0);record(f,"HOST result=%08x title=%016llx\n",rc,(unsigned long long)title);test(f,"Pokopia host",R_SUCCEEDED(rc)&&title==0x0400a3a01ee4c000ULL);if(failures)goto finish;
    /* Read-only process capabilities needed by the original SDK startup.
     * Unsupported info types are observations, not failed tests. */
    record(f,"STARTUP_CAPABILITIES_BEGIN\n");
    for(u32 type=0;type<=38;type++){
        u64 value=0;Result info=svcGetInfo(&value,type,CUR_PROCESS_HANDLE,0);
        record(f,"GET_INFO type=%u handle=current-process sub=0 result=%08x value=%016llx\n",type,info,(unsigned long long)value);
    }
    for(u64 sub=2;sub<=3;sub++){
        u64 value=0;Result info=svcGetInfo(&value,InfoType_RandomEntropy,0,sub);
        record(f,"SDK_GET_INFO type=11 handle=0 sub=%llu result=%08x value=%016llx\n",(unsigned long long)sub,info,(unsigned long long)value);
    }
    record(f,"STARTUP_CAPABILITIES_END\n");
    Service ro;rc=kaihenGetRoSession(&ro);record(f,"RO result=%08x\n",rc);test(f,"borrowed RO available",R_SUCCEEDED(rc));if(R_FAILED(rc))goto finish;
    tls_slot=threadTlsAlloc(NULL);test(f,"TLS slot allocated",tls_slot>=0);if(tls_slot<0)goto finish;
    for(unsigned cycle=1;cycle<=3;cycle++){if(!fixture_cycle(f,&ro,cycle))break;}
    if(full&&!failures)yoshi_test(f,&ro);else if(full)record(f,"Yoshi phase skipped after fixture failure.\n");
    threadTlsFree(tls_slot);tls_slot=-1;
finish:
    record(f,"SUMMARY checks=%u passed=%u failures=%u full=%d\n",checks,checks-failures,failures,full);
    record(f,"Yoshi was not executed. Report records the remaining startup blockers explicitly.\n");
    if(ferror(f))log_error=1;
    if(fclose(f))log_error=1;
    snprintf(outcome,sizeof(outcome),"%u/%u checks passed. %s",checks-failures,checks,log_error?"Report write error.":"Report: /nx2-folder-exec-report.txt");
}
int main(int argc,char **argv){
    (void)argc;(void)argv;consoleInit(NULL);padConfigureInput(1,HidNpadStyleSet_NpadStandard);padInitializeDefault(&pad);
    while(appletMainLoop()){
        padUpdate(&pad);u64 down=padGetButtonsDown(&pad);if(down&HidNpadButton_Plus)break;if(down&HidNpadButton_A)run(1);if(down&HidNpadButton_Y)run(0);
        consoleClear();printf("NX2 Folder Execution Suite 0.3\n\n");printf("Three cycles: cross-module calls, RELR, constructors,\nBSS, weak imports, TLS, worker threads, SIMD and cleanup.\n");
        printf("Full test applies relocations to all six Yoshi modules.\nYoshi native startup remains blocked; no game entrypoint called.\n\n");printf("A: full suite | Y: fixture only | PLUS: exit\n\n%s\n",outcome);consoleUpdate(NULL);
    }consoleExit(NULL);return 0;
}
