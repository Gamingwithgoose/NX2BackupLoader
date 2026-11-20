#include <switch.h>
#include <kaihen.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <stdarg.h>
#include "module.h"
#include "fixture_hash.h"
static char outcome[160]="A runs the supplied test module. PLUS exits.";
static int log_error;
static void record(FILE *f,const char *format,...) {
    va_list args;va_start(args,format);if(vfprintf(f,format,args)<0)log_error=1;va_end(args);
    if(fflush(f)!=0)log_error=1;
}
static Result map_module(Service *ro,void *memory,size_t image,size_t bss,u64 *base) {
    const struct {u64 pid,source,size,bss_source,bss_size;} in={0,(u64)memory,image,bss?(u64)memory+image:0,bss};
    return serviceDispatchInOut(ro,0,in,*base,.in_send_pid=true);
}
static Result unmap_module(Service *ro,u64 base) {
    const struct {u64 pid,base;} in={0,base};
    return serviceDispatchIn(ro,1,in,.in_send_pid=true);
}
static void digest_hex(char text[65],const unsigned char hash[32]) {
    static const char digits[]="0123456789abcdef";
    for(unsigned i=0;i<32;i++){text[2*i]=digits[hash[i]>>4];text[2*i+1]=digits[hash[i]&15];}text[64]=0;
}
static void run(void) {
    log_error=0;
    snprintf(outcome,sizeof(outcome),"Test stopped. See /nx2-folder-exec-report.txt.");
    FILE *report=fopen("sdmc:/nx2-folder-exec-report.txt","w");
    if(!report){snprintf(outcome,sizeof(outcome),"Cannot write report; test did not run.");return;}
    record(report,"NX2 Folder Execution Test 0.1\nOwn fixture only; no Yoshi entrypoint or save access.\n");
    u64 title=0;Result rc=svcGetInfo(&title,InfoType_ProgramId,CUR_PROCESS_HANDLE,0);
    record(report,"Host title result=%08x id=%016llx\n",rc,(unsigned long long)title);
    if(R_FAILED(rc)||title!=0x0400a3a01ee4c000ULL){record(report,"STOP: expected Pokopia host.\n");goto finish;}
    FILE *file=fopen("sdmc:/games/nx2-exec-fixture/module.nxm","rb");
    if(!file){record(report,"STOP: fixture missing at /games/nx2-exec-fixture/module.nxm\n");goto finish;}
    unsigned char header[128];size_t got=fread(header,1,sizeof(header),file);
    if(got!=sizeof(header)||memcmp(header+16,"NRO0",4)){fclose(file);record(report,"STOP: invalid fixture header\n");goto finish;}
    u32 size,bss;memcpy(&size,header+0x18,4);memcpy(&bss,header+0x38,4);
    if(!size||size>1024*1024||bss>1024*1024||((size|bss)&4095)){fclose(file);record(report,"STOP: fixture size limit\n");goto finish;}
    void *memory=memalign(4096,(size_t)size+bss);
    if(!memory){fclose(file);record(report,"STOP: allocation failure\n");goto finish;}
    memset(memory,0,(size_t)size+bss);rewind(file);got=fread(memory,1,size,file);
    int extra=fgetc(file),bad=ferror(file);if(fclose(file)!=0)bad=1;
    if(got!=size||extra!=EOF||bad){free(memory);record(report,"STOP: fixture read error/size mismatch\n");goto finish;}
    unsigned char hash[32];char digest[65];sha256CalculateHash(hash,memory,size);digest_hex(digest,hash);
    record(report,"Fixture SHA256=%s\n",digest);
    if(strcmp(digest,FIXTURE_SHA256)){free(memory);record(report,"STOP: fixture is not this build's owned test module\n");goto finish;}
    Nx2Module module;int status=nx2_module_inspect(memory,(size_t)size+bss,&module);
    record(report,"Pre-map metadata: %s\n",nx2_module_error(status));
    if(status){free(memory);goto finish;}
    Service ro;rc=kaihenGetRoSession(&ro);record(report,"Borrow RO result=%08x\n",rc);
    if(R_FAILED(rc)){free(memory);goto finish;}
    u64 mapped=0;record(report,"Mapping executable/read-only/writable segments...\n");
    rc=map_module(&ro,memory,size,bss,&mapped);record(report,"Map result=%08x address=%016llx\n",rc,(unsigned long long)mapped);
    if(R_FAILED(rc)){
        /* Retain backing on a failed map: no assumption about rollback state. */
        record(report,"STOP: map failed; ending host process after report flush.\n");fclose(report);svcExitProcess();
    }
    status=nx2_module_inspect((void *)mapped,(size_t)size+bss,&module);
    if(!status)status=nx2_module_relocate(&module);
    record(report,"Relocate mapped module: %s\n",nx2_module_error(status));
    uintptr_t entry=0;if(!status)status=nx2_module_function(&module,"nx2_module_entry",&entry);
    record(report,"Entry lookup: %s address=%016llx\n",nx2_module_error(status),(unsigned long long)entry);
    if(!status){
        MemoryInfo info;u32 page;
        rc=svcQueryMemory(&info,&page,entry);
        record(report,"Entry memory query result=%08x permission=%u\n",rc,R_SUCCEEDED(rc)?info.perm:0);
        if(R_FAILED(rc)||(info.perm&Perm_Rx)!=Perm_Rx)status=NX2_BAD_IMAGE;
        rc=svcQueryMemory(&info,&page,(uintptr_t)module.guest+module.address[2]);
        record(report,"Data memory query result=%08x permission=%u\n",rc,R_SUCCEEDED(rc)?info.perm:0);
        if(R_FAILED(rc)||(info.perm&Perm_Rw)!=Perm_Rw)status=NX2_BAD_IMAGE;
    }
    int passed=0;
    if(!status){
        record(report,"Entering supplied owned module with input=41...\n");
        uint64_t (*call)(uint64_t)=(uint64_t (*)(uint64_t))entry;
        uint64_t first=call(41),second=call(41);
        passed=first==4702&&second==4703;
        record(report,"EXECUTION %s first=%llu expected=4702 second=%llu expected=4703\n",passed?"PASS":"FAIL",(unsigned long long)first,(unsigned long long)second);
    }
    record(report,"Unloading module...\n");rc=unmap_module(&ro,mapped);record(report,"Unload result=%08x\n",rc);
    if(R_FAILED(rc)){record(report,"STOP: unload failed; ending host process without freeing mapped backing.\n");fclose(report);svcExitProcess();}
    free(memory);
    record(report,"SUMMARY fixture_execution=%s mapping_released=1\n",passed?"PASS":"FAIL");
    record(report,"This proves only the fixture mapping/relocation/call path. Yoshi native startup, TLS and filesystem integration remain unimplemented.\n");
    snprintf(outcome,sizeof(outcome),"%s. Report: /nx2-folder-exec-report.txt",passed?"Fixture execution passed":"Fixture execution did not pass");
finish:
    if(ferror(report))log_error=1;
    if(fclose(report)!=0)log_error=1;
    if(log_error)snprintf(outcome,sizeof(outcome),"Report write failed; log may be incomplete.");
}
int main(int argc,char **argv) {
    (void)argc;(void)argv;consoleInit(NULL);PadState pad;
    padConfigureInput(1,HidNpadStyleSet_NpadStandard);padInitializeDefault(&pad);
    while(appletMainLoop()){
        padUpdate(&pad);u64 down=padGetButtonsDown(&pad);if(down&HidNpadButton_Plus)break;
        if(down&HidNpadButton_A)run();
        consoleClear();printf("NX2 Folder Execution Test 0.1\n\n");
        printf("Runs a supplied test module from SD under Pokopia.\n");
        printf("Tests mapping, relocation, code execution and writable data.\n\n");
        printf("A: run fixture | PLUS: exit\n%s\n\n",outcome);
        printf("Does not launch Yoshi or access game saves.\n");consoleUpdate(NULL);
    }
    consoleExit(NULL);return 0;
}
