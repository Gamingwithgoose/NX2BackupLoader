#include <switch.h>
#include <kaihen.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <sys/types.h>
#include "expected.h"

_Static_assert(sizeof(off_t) >= 8, "64-bit SD seeks required");
static PadState pad;
static unsigned char buffer[65536];
static char game_root[1024];
static char outcome[128] = "A: run read test and write report. PLUS: exit.";
static int cancelled;
static int log_failed;

static void log_line(FILE *log, const char *format, ...) {
    va_list args;
    va_start(args, format);
    if (vfprintf(log, format, args) < 0) log_failed = 1;
    va_end(args);
    if (fflush(log) != 0) log_failed = 1;
}
static void hex(char out[65], const unsigned char bytes[32]) {
    static const char digits[] = "0123456789abcdef";
    for (unsigned i=0;i<32;i++) { out[2*i]=digits[bytes[i]>>4];out[2*i+1]=digits[bytes[i]&15]; }
    out[64]=0;
}
static int join(char *out, size_t size, const char *root, const char *leaf) {
    int written = snprintf(out,size,"%s/%s",root,leaf);
    return written >= 0 && (size_t)written < size;
}
static int matches(const char *root) {
    char path[1024], digest[65]; unsigned char bytes[32];
    if (!join(path,sizeof(path),root,"exefs/main")) return 0;
    FILE *file=fopen(path,"rb"); if (!file) return 0;
    int ok=fseeko(file,0x40,SEEK_SET)==0 && fread(bytes,1,32,file)==32;
    if (fclose(file)!=0) ok=0;
    if (!ok) return 0;
    hex(digest,bytes);
    return !strcmp(digest,expected_build_id);
}
static int find_yoshi(void) {
    DIR *dir=opendir("sdmc:/games"); if (!dir) return 0;
    struct dirent *entry; char root[1024], base[1024]; int found=0;
    while ((entry=readdir(dir))) {
        if (!strcmp(entry->d_name,".") || !strcmp(entry->d_name,"..")) continue;
        if (!join(root,sizeof(root),"sdmc:/games",entry->d_name)) continue;
        if (matches(root)) { strcpy(game_root,root);found=1;break; }
        if (join(base,sizeof(base),root,"base") && matches(base)) { strcpy(game_root,base);found=1;break; }
    }
    closedir(dir);return found;
}
static int keep_running(void) {
    if (!appletMainLoop()) { cancelled=1;return 0; }
    padUpdate(&pad);
    if (padGetButtonsDown(&pad)&HidNpadButton_Plus) {cancelled=1;return 0;}
    return 1;
}
static void progress(const Check *check, unsigned long long done) {
    consoleClear();
    printf("NX2 SD Read Probe 0.1\n\nReading: %s\n",check->path);
    printf("%llu / %llu bytes in this check\n",done,check->length);
    printf("Game files read only. PLUS cancels.\n");
    consoleUpdate(NULL);
}
static int check_file(FILE *log, const Check *check) {
    char path[1024], digest[65]; struct stat info;
    if (!join(path,sizeof(path),game_root,check->path)) {log_line(log,"FAIL path length\n");return 0;}
    if (stat(path,&info)!=0 || !S_ISREG(info.st_mode) || info.st_size<0 || (unsigned long long)info.st_size!=check->file_size) {
        log_line(log,"FAIL missing/wrong size: %s expected=%llu\n",check->path,check->file_size);return 0;
    }
    FILE *file=fopen(path,"rb"); if (!file) {log_line(log,"FAIL open: %s\n",check->path);return 0;}
    if (fseeko(file,(off_t)check->offset,SEEK_SET)!=0) {fclose(file);log_line(log,"FAIL seek: %s\n",check->path);return 0;}
    Sha256Context context;sha256ContextCreate(&context);
    unsigned long long done=0;int ok=1;progress(check,0);
    while (done<check->length) {
        if (!keep_running()) {ok=0;break;}
        size_t wanted=(size_t)((check->length-done)>sizeof(buffer)?sizeof(buffer):(check->length-done));
        size_t got=fread(buffer,1,wanted,file);
        if (got!=wanted) {ok=0;break;}
        sha256ContextUpdate(&context,buffer,got);done+=got;
        if ((done % (4*1024*1024))==0) progress(check,done);
    }
    if (ferror(file)) ok=0;
    if (fclose(file)!=0) ok=0;
    unsigned char bytes[32];sha256ContextGetHash(&context,bytes);hex(digest,bytes);
    if (cancelled) {log_line(log,"CANCELLED %s\n",check->path);return 0;}
    if (strcmp(digest,check->sha256)) ok=0;
    log_line(log,"%s %s offset=%llu bytes=%llu whole=%d\nexpected=%s\nactual=%s\n",
        ok?"PASS":"FAIL",check->path,check->offset,check->length,check->whole,check->sha256,digest);
    return ok;
}
static void runtime_info(FILE *log) {
    u64 value=0;Result rc=svcGetInfo(&value,InfoType_ProgramId,CUR_PROCESS_HANDLE,0);
    log_line(log,"Host program ID result=%08x value=%016llx\n",rc,(unsigned long long)value);
    value=0;rc=svcGetInfo(&value,InfoType_TotalMemorySize,CUR_PROCESS_HANDLE,0);
    log_line(log,"Process memory allowance result=%08x bytes=%llu\n",rc,(unsigned long long)value);
    value=0;rc=svcGetInfo(&value,InfoType_UsedMemorySize,CUR_PROCESS_HANDLE,0);
    log_line(log,"Process memory used result=%08x bytes=%llu\n",rc,(unsigned long long)value);
    Service ro,hen;rc=kaihenGetRoSession(&ro);
    log_line(log,"Borrowed RO session result=%08x\n",rc);
    if (R_FAILED(rc)) return;
    log_line(log,"Querying private homebrew session...\n");
    rc=kaihenOpenHomebrewSession(&ro,&hen);
    log_line(log,"Open private homebrew session result=%08x\n",rc);
    if (R_FAILED(rc)) return;
    log_line(log,"Querying HEN status...\n");
    HenStatus status={0};rc=henGetStatus(&hen,&status);
    log_line(log,"HEN status result=%08x protocol=%u startup=%08x pid=%llu\n",rc,status.protocol_version,status.initialization_result,(unsigned long long)status.process_id);
    serviceClose(&hen); /* RO session is borrowed and must remain open. */
}
static void run_test(void) {
    cancelled=0;log_failed=0;
    FILE *log=fopen("sdmc:/nx2-sdprobe-report.txt","w");
    if (!log) {snprintf(outcome,sizeof(outcome),"Cannot create SD report. No read tests ran.");return;}
    log_line(log,"NX2 SD Read Probe 0.1\nScope: file reads and private status queries; NO game execution.\n");
    runtime_info(log);
    unsigned passed=0;int found=find_yoshi();
    if (!found) log_line(log,"Yoshi build not found under sdmc:/games (direct or base/ layout).\n");
    else {
        log_line(log,"Backup folder: %s\n",game_root);
        for (unsigned i=0;i<CHECK_COUNT;i++) {if(check_file(log,&checks[i]))passed++;if(cancelled)break;}
    }
    log_line(log,"SUMMARY passed=%u total=%u cancelled=%d\n",passed,(unsigned)CHECK_COUNT,cancelled);
    log_line(log,"Passing establishes selected SD reads match the PC reference. It does NOT establish native game launch support.\n");
    if (ferror(log)) log_failed=1;
    if (fclose(log)!=0) log_failed=1;
    if (log_failed) snprintf(outcome,sizeof(outcome),"Report write failed; saved report may be incomplete.");
    else if (!found) snprintf(outcome,sizeof(outcome),"Yoshi not found. See nx2-sdprobe-report.txt.");
    else snprintf(outcome,sizeof(outcome),"%s: %u/%u checks passed. Report: /nx2-sdprobe-report.txt",cancelled?"Cancelled":"Finished",passed,(unsigned)CHECK_COUNT);
}
int main(int argc,char **argv) {
    (void)argc;(void)argv;
    consoleInit(NULL);padConfigureInput(1,HidNpadStyleSet_NpadStandard);padInitializeDefault(&pad);
    while (appletMainLoop()) {
        padUpdate(&pad);u64 down=padGetButtonsDown(&pad);
        if (down&HidNpadButton_Plus) break;
        if (down&HidNpadButton_A) run_test();
        consoleClear();
        printf("NX2 SD Read Probe 0.1\n\nYoshi demo PC-reference test\n");
        printf("7 full ExeFS hashes; 6 sample reads from large RomFS files.\n");
        printf("Finds Yoshi under /games/<folder>/ or /games/<folder>/base/.\n\n");
        printf("A: run test and overwrite /nx2-sdprobe-report.txt\nPLUS: exit/cancel\n\n");
        printf("%s\n\n",outcome);
        printf("Game files read only. This does NOT launch Yoshi.\n");
        consoleUpdate(NULL);
    }
    consoleExit(NULL);return 0;
}
