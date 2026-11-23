/* SPDX-License-Identifier: MIT
 * NX2Trace: optional private kaiHEN plugin, independently hosted by HEN.
 * This deliberately NEVER probes services, debugs a process or changes state.
 */
#include <switch.h>
#include <kaihen/plugin.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

u32 __nx_applet_type = AppletType_None;
size_t __nx_heap_size = 0x200000;
void __appInit(void) {}
void __appExit(void) {}

typedef struct {
    Handle exit_write;
    u64 sequence;
    u32 write_failures;
    Mutex log_lock;
    Thread observe_thread;
    u8 observe_stack[0x8000] __attribute__((aligned(16)));
    u32 observer_started;
} TraceContext;

/* Fixed-size, scalar IPC record. Caller controls event code and numeric data.
 * Do not trust/parse arbitrary strings in a privileged plugin process. */
typedef struct {
    u32 magic;  /* 'NX2T' */
    u32 event;
    u64 guest_id;
    u64 value;
} TraceRecord;
#define TRACE_MAGIC 0x5432584eU

/* Only a small number of launch milestones are sent; SD I/O is intentionally
 * limited to those records. The plugin never opens an SM service itself. */
static void persist_record(TraceContext *ctx, const TraceRecord *rec) {
    mutexLock(&ctx->log_lock);
    FsFileSystem sd = {0};
    Result rc = fsInitialize();
    if (R_FAILED(rc)) { ctx->write_failures++; mutexUnlock(&ctx->log_lock); return; }
    rc = fsOpenSdCardFileSystem(&sd);
    if (R_FAILED(rc)) { ctx->write_failures++; fsExit(); mutexUnlock(&ctx->log_lock); return; }
    const char *path = "/BackupLoaderMKW-trace.log";
    FsFile file = {0};
    rc = fsFsOpenFile(&sd, path, FsOpenMode_Write, &file);
    if (R_FAILED(rc)) {
        rc = fsFsCreateFile(&sd, path, 0, 0);
        if (R_SUCCEEDED(rc))
            rc = fsFsOpenFile(&sd, path, FsOpenMode_Write, &file);
    }
    if (R_SUCCEEDED(rc)) {
        s64 size = 0;
        rc = fsFileGetSize(&file, &size);
        if (R_SUCCEEDED(rc) && size >= 0 && size < 1024 * 1024) {
            char line[160];
            int n = snprintf(line, sizeof(line),
                "seq=%llu event=%u guest=%016llx value=%016llx previous_write_failures=%u\n",
                (unsigned long long)++ctx->sequence, rec->event,
                (unsigned long long)rec->guest_id, (unsigned long long)rec->value,
                ctx->write_failures);
            if (n > 0 && n < (int)sizeof(line)) {
                rc = fsFileSetSize(&file, size + n);
                if (R_SUCCEEDED(rc))
                    rc = fsFileWrite(&file, size, line, (u64)n, FsWriteOption_Flush);
            }
        } else if (R_SUCCEEDED(rc)) {
            /* Intentionally bounded log, never overwrite a previous run. */
            rc = MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
        }
        fsFileClose(&file);
    }
    if (R_FAILED(rc)) ctx->write_failures++;
    fsFsClose(&sd);
    fsExit();
    mutexUnlock(&ctx->log_lock);
}
/* One dedicated, non-critical observer thread. Uses only pm:dmnt's
 * GetProcessId lookup for the *known* MKW host program. Never subscribes
 * to or consumes Horizon's system process-event queue, attaches a debugger,
 * manipulates a process or scans service names. It can detect disappearance,
 * NOT retrieve the exception/error that caused it.
 *
 * A service lookup may itself block; we isolate it from the plugin's
 * ready signal and main loop. On rejection this worker exits permanently.
 */
#define NX2_HOST_MKW_ID UINT64_C(0x0400c3f00006e000)
static void observe_host(void *arg) {
    TraceContext *ctx = (TraceContext*)arg;
    Service pm = {0};
    Result rc = smGetService(&pm, "pm:dmnt");
    TraceRecord record={ TRACE_MAGIC, 20, NX2_HOST_MKW_ID, rc };
    persist_record(ctx,&record);
    if (R_FAILED(rc)) return;
    u64 last_pid=0;
    u32 failures=0;
    for(;;) {
        /* Horizon pm:dmnt GetProcessId = command 2 on current HOS.
         * No process enumeration or lifecycle event queue consumption. */
        const u64 target=NX2_HOST_MKW_ID;
        u64 pid=0;
        rc=serviceDispatchInOut(&pm,2,target,pid);
        if(R_SUCCEEDED(rc)) {
            failures=0;
            if(pid && last_pid!=pid) {
                record=(TraceRecord){TRACE_MAGIC,21,NX2_HOST_MKW_ID,pid};
                persist_record(ctx,&record);
            }
            last_pid=pid;
        } else {
            if(last_pid) {
                record=(TraceRecord){TRACE_MAGIC,22,NX2_HOST_MKW_ID,last_pid};
                persist_record(ctx,&record);
                record=(TraceRecord){TRACE_MAGIC,23,NX2_HOST_MKW_ID,rc};
                persist_record(ctx,&record);
                last_pid=0;
            }
            /* Never repeatedly hammer an unrecognized/denied service. */
            if(++failures>=3) {
                record=(TraceRecord){TRACE_MAGIC,24,NX2_HOST_MKW_ID,rc};
                persist_record(ctx,&record);
                break;
            }
        }
        svcSleepThread(1000000000LL);
    }
    serviceClose(&pm);
}
static Result trace_command(u32 cmd, const void *input, size_t input_size,
                            void *output, size_t *output_size, void *context) {
    (void)output;
    *output_size = 0;
    TraceContext *ctx = (TraceContext*)context;
    if (cmd == 0 && input_size == sizeof(TraceRecord)) {
        TraceRecord rec;
        memcpy(&rec, input, sizeof(rec));
        if (rec.magic != TRACE_MAGIC || rec.event == 0 || rec.event > 64)
            return MAKERESULT(Module_Libnx, LibnxError_BadInput);
        persist_record(ctx, &rec);
        return 0; /* Avoid propagating SD errors into game launch. */
    }
    if (cmd == 1) return svcSignalEvent(ctx->exit_write);
    return MAKERESULT(Module_Libnx, LibnxError_BadInput);
}
int main(void) {
    Handle ready = INVALID_HANDLE, control = INVALID_HANDLE;
    Result rc = pluginRuntimeInitializeWithService(&ready, &control);
    if (R_FAILED(rc)) return rc;
    TraceContext ctx = {0};
    Handle exit_read = INVALID_HANDLE;
    PluginService service = {0};
    rc = svcCreateEvent(&ctx.exit_write, &exit_read);
    if (R_SUCCEEDED(rc)) rc = pluginServiceStart(&service, control, trace_command, &ctx);
    if (R_SUCCEEDED(rc)) rc = svcSignalEvent(ready);
    /* Start external observer ONLY after plugin signaled ready to HEN.
     * Any unavailable or hanging PM service affects this worker alone. */
    if (R_SUCCEEDED(rc)) {
        Result obs=threadCreate(&ctx.observe_thread,observe_host,&ctx,
                                ctx.observe_stack,sizeof(ctx.observe_stack),
                                0x2c,-2);
        if (R_SUCCEEDED(obs)) {
            obs=threadStart(&ctx.observe_thread);
            if (R_SUCCEEDED(obs)) ctx.observer_started=1;
            else threadClose(&ctx.observe_thread);
        }
    }
    if (R_SUCCEEDED(rc)) rc = svcWaitSynchronizationSingle(exit_read, UINT64_MAX);
    if (service.started) pluginServiceStop(&service);
    if (exit_read != INVALID_HANDLE) svcCloseHandle(exit_read);
    if (ctx.exit_write != INVALID_HANDLE) svcCloseHandle(ctx.exit_write);
    pluginRuntimeExit();
    return rc;
}
