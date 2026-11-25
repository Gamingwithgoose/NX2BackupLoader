/* SPDX-License-Identifier: GPL-2.0-only */
#include "termination_trace.h"
#include <switch.h>
#include <stdio.h>
#include <string.h>

/* Inspired by nx2-relink's hostend.c boundary observations. Independent
 * implementation: own pre-opened native IFile, no guest RPC or stdio stream
 * in the fatal path, no unsafe pointer/stack dereferences. Best effort;
 * kernel kills, GPU faults and raw inlined SVCs may bypass these wrappers. */
static FsFile trace_file;
static uint32_t armed,busy,next_record;
#define TRACE_RECORD_BYTES 512
#define TRACE_RECORD_LIMIT 32
static Result write_record(uint32_t record,const char *text){
 char block[TRACE_RECORD_BYTES];memset(block,' ',sizeof(block));
 size_t n=strlen(text);if(n>sizeof(block)-1)n=sizeof(block)-1;
 memcpy(block,text,n);block[sizeof(block)-1]='\n';
 return fsFileWrite(&trace_file,(int64_t)record*TRACE_RECORD_BYTES,
                    block,sizeof(block),FsWriteOption_Flush);
}
void nx2_termination_trace_close(void){
 if(__atomic_exchange_n(&armed,0,__ATOMIC_ACQ_REL))fsFileClose(&trace_file);
}
uint32_t nx2_termination_trace_open(uint64_t host,uint64_t guest){
 nx2_termination_trace_close();
 FsFileSystem *sd=fsdevGetDeviceFileSystem("sdmc");
 if(!sd)return 0x1f59;
 const char *path="/BackupLoaderMKW-host-end.log";
 Result rc=fsFsOpenFile(sd,path,FsOpenMode_Write|FsOpenMode_Append,&trace_file);
 if(R_FAILED(rc)){
  rc=fsFsCreateFile(sd,path,0,0);if(R_FAILED(rc))return rc;
  rc=fsFsOpenFile(sd,path,FsOpenMode_Write|FsOpenMode_Append,&trace_file);
  if(R_FAILED(rc))return rc;
 }
 rc=fsFileSetSize(&trace_file,0);
 if(R_FAILED(rc)){fsFileClose(&trace_file);return rc;}
 char text[256];
 snprintf(text,sizeof(text),"HOST_TERMINATION_TRACE version=1 host=%016llx guest=%016llx best_effort=1 original_calls_preserved=1 records_max=%u",
          (unsigned long long)host,(unsigned long long)guest,TRACE_RECORD_LIMIT);
 rc=write_record(0,text);
 if(R_FAILED(rc)){fsFileClose(&trace_file);return rc;}
 next_record=1;busy=0;
 __atomic_store_n(&armed,1,__ATOMIC_RELEASE);return 0;
}
static void ended(const char *kind,uint64_t a,uint64_t b,uint64_t c,uintptr_t caller){
 if(!__atomic_load_n(&armed,__ATOMIC_ACQUIRE))return;
 uint32_t expected=0;
 if(!__atomic_compare_exchange_n(&busy,&expected,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))return;
 uint32_t record=next_record++;
 if(record<=TRACE_RECORD_LIMIT){
  char text[384];
  snprintf(text,sizeof(text),"HOST_END_EVENT seq=%u kind=%s tick=%llu caller=%016llx a=%016llx b=%016llx c=%016llx args_not_dereferenced=1",
           record,kind,(unsigned long long)armGetSystemTick(),(unsigned long long)caller,
           (unsigned long long)a,(unsigned long long)b,(unsigned long long)c);
  (void)write_record(record,text);
 }
 __atomic_store_n(&busy,0,__ATOMIC_RELEASE);
}
Result __real_svcBreak(u32 reason,uintptr_t address,uintptr_t size);
Result __wrap_svcBreak(u32 reason,uintptr_t address,uintptr_t size){
 ended((reason&0x80000000U)?"svcBreak_notification":"svcBreak",reason,address,size,(uintptr_t)__builtin_return_address(0));
 return __real_svcBreak(reason,address,size);
}
void __real_svcExitProcess(void) __attribute__((noreturn));
void __wrap_svcExitProcess(void){
 ended("svcExitProcess",0,0,0,(uintptr_t)__builtin_return_address(0));
 __real_svcExitProcess();
}
void __real_svcReturnFromException(Result rc) __attribute__((noreturn));
void __wrap_svcReturnFromException(Result rc){
 ended("svcReturnFromException",rc,0,0,(uintptr_t)__builtin_return_address(0));
 __real_svcReturnFromException(rc);
}
void __real_abort(void) __attribute__((noreturn));
void __wrap_abort(void){
 ended("abort",0,0,0,(uintptr_t)__builtin_return_address(0));
 __real_abort();
}
void __real_svcExitThread(void) __attribute__((noreturn));
void __wrap_svcExitThread(void){
 ended("svcExitThread",0,0,0,(uintptr_t)__builtin_return_address(0));
 __real_svcExitThread();
}
