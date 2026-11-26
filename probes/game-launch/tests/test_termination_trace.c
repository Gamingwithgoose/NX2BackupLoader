/* SPDX-License-Identifier: GPL-2.0-only */
#include "termination_trace.h"
#include <switch.h>
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
static FsFileSystem filesystem={1};
static unsigned mode,writes,opens,closes,created,real_breaks,last_real;
static int fail_write,recurse;
static char bytes[33*512+1];
static jmp_buf finish;
Result __wrap_svcBreak(u32,uintptr_t,uintptr_t);
void __wrap_svcExitProcess(void),__wrap_svcExitThread(void),__wrap_abort(void);
void __wrap_svcReturnFromException(Result);
FsFileSystem *fsdevGetDeviceFileSystem(const char *s){assert(!strcmp(s,"sdmc"));return mode==1?NULL:&filesystem;}
Result fsFsOpenFile(FsFileSystem *fs,const char *s,u32 flags,FsFile *f){
 assert(fs==&filesystem && !strcmp(s,"/BackupLoaderMKW-host-end.log") && flags==6);opens++;
 if(mode==2&&opens==1)return 0x402;
 f->handle=5;return 0;
}
Result fsFsCreateFile(FsFileSystem *fs,const char *s,int64_t size,u32 option){assert(fs==&filesystem && !strcmp(s,"/BackupLoaderMKW-host-end.log")&&!size&&!option);created++;return 0;}
Result fsFileSetSize(FsFile *f,int64_t size){assert(f->handle==5&&!size);return mode==3?0x202:0;}
void fsFileClose(FsFile *f){assert(f->handle==5);closes++;}
uint64_t armGetSystemTick(void){return 98765;}
Result fsFileWrite(FsFile *f,int64_t offset,const void *p,uint64_t size,u32 option){
 assert(f->handle==5 && offset>=0 && offset+size<=sizeof(bytes)-1 && size==512 && option==1);
 writes++;
 assert(!memchr(p,0,(size_t)size));
 if(recurse){recurse=0;assert(__wrap_svcBreak(0x80000003,1,4)==0x55);}
 if(mode==4||fail_write)return 0x202;
 memcpy(bytes+offset,p,(size_t)size);return 0;
}
Result __real_svcBreak(u32 reason,uintptr_t address,uintptr_t size){assert((reason==0 || reason==0x80000003)&&address==1&&size==4);real_breaks++;return 0x55;}
void __real_svcExitProcess(void){last_real=1;longjmp(finish,1);}
void __real_svcExitThread(void){last_real=2;longjmp(finish,1);}
void __real_svcReturnFromException(Result r){assert(r==0xe401);last_real=3;longjmp(finish,1);}
void __real_abort(void){last_real=4;longjmp(finish,1);}
static void reset(unsigned next){nx2_termination_trace_close();mode=next;writes=opens=closes=created=real_breaks=last_real=0;fail_write=recurse=0;memset(bytes,0,sizeof(bytes));}
int main(void){
 reset(0);assert(__wrap_svcBreak(0,1,4)==0x55 && !writes);
 for(unsigned i=1;i<=4;i++){
  reset(i);uint32_t rc=nx2_termination_trace_open(10,20);
  if(i==2){assert(!rc && created==1 && opens==2);nx2_termination_trace_close();}
  else {assert(rc);unsigned before=writes;assert(__wrap_svcBreak(0,1,4)==0x55 && writes==before);if(i!=1)assert(closes==1);}
 }
 reset(0);assert(!nx2_termination_trace_open(10,20));assert(writes==1);
 recurse=1;assert(__wrap_svcBreak(0,1,4)==0x55);assert(writes==2 && real_breaks==2);
 assert(strstr(bytes,"kind=svcBreak") && strstr(bytes,"args_not_dereferenced=1"));
 if(!setjmp(finish))__wrap_svcExitProcess();assert(last_real==1 && strstr(bytes,"kind=svcExitProcess"));
 if(!setjmp(finish))__wrap_svcExitThread();assert(last_real==2 && strstr(bytes,"kind=svcExitThread"));
 if(!setjmp(finish))__wrap_svcReturnFromException(0xe401);assert(last_real==3 && strstr(bytes,"kind=svcReturnFromException"));
 fail_write=1;if(!setjmp(finish))__wrap_abort();assert(last_real==4);
 reset(0);assert(!nx2_termination_trace_open(10,20));
 for(unsigned i=0;i<100;i++)assert(__wrap_svcBreak(0,1,4)==0x55);
 assert(writes==33 && real_breaks==100);
 nx2_termination_trace_close();unsigned before=writes;assert(__wrap_svcBreak(0,1,4)==0x55 && writes==before);
 puts("Host termination: original calls, unsafe argument avoidance, native writes, recursion, failures and bound PASS");return 0;
}
