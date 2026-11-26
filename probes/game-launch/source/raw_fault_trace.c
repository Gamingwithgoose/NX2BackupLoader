/* SPDX-License-Identifier: GPL-2.0-only */
#include "raw_fault.h"
#include <switch.h>

/* Owned native IFile opened BEFORE guest execution. The fault path performs
 * one fixed512-byte Flush write: no fopen/printf/malloc, guest RPC, key
 * extraction or kernel-context mutation. A native IPC failure remains a
 * failure. This is experimental inside an exception path. */
static FsFile raw_file;
static uint32_t armed,busy;
uint32_t nx2_raw_fault_trace_result=UINT32_MAX;
static unsigned char block[512] __attribute__((aligned(16)));

static void put32(unsigned at,uint32_t value){
  for(unsigned i=0;i<4;i++)block[at+i]=(unsigned char)(value>>(i*8));
}
static void put64(unsigned at,uint64_t value){
  for(unsigned i=0;i<8;i++)block[at+i]=(unsigned char)(value>>(i*8));
}
uint32_t nx2_raw_fault_trace_open(uint64_t host,uint64_t guest){
  if(__atomic_exchange_n(&armed,0,__ATOMIC_ACQ_REL))fsFileClose(&raw_file);
  FsFileSystem *sd=fsdevGetDeviceFileSystem("sdmc");if(!sd)return 0x1f59;
  const char *path="/BackupLoaderMKW-raw-exception.bin";
  Result rc=fsFsOpenFile(sd,path,FsOpenMode_Write,&raw_file);
  if(R_FAILED(rc)){
    rc=fsFsCreateFile(sd,path,sizeof(block),0);if(R_FAILED(rc))return rc;
    rc=fsFsOpenFile(sd,path,FsOpenMode_Write,&raw_file);if(R_FAILED(rc))return rc;
  }
  rc=fsFileSetSize(&raw_file,sizeof(block));
  if(R_FAILED(rc)){fsFileClose(&raw_file);return rc;}
  for(unsigned i=0;i<sizeof(block);i++)block[i]=0;
  static const unsigned char magic[8]={'N','X','2','R','A','W','0','2'};
  static const unsigned char end[8]={'R','A','W','2','E','N','D','!'};
  for(unsigned i=0;i<8;i++){block[i]=magic[i];block[504+i]=end[i];}
  put32(8,2);put32(12,0);put64(16,host);put64(24,guest);
  put32(32,sizeof(Nx2RawFault));busy=0;
  __atomic_store_n(&nx2_raw_fault_trace_result,UINT32_MAX,__ATOMIC_RELEASE);
  rc=fsFileWrite(&raw_file,0,block,sizeof(block),FsWriteOption_Flush);
  if(R_FAILED(rc)){fsFileClose(&raw_file);return rc;}
  __atomic_store_n(&armed,1,__ATOMIC_RELEASE);return 0;
}
void nx2_raw_fault_trace_write(void){
  if(!__atomic_load_n(&armed,__ATOMIC_ACQUIRE))return;
  uint32_t expected=0;
  if(!__atomic_compare_exchange_n(&busy,&expected,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))return;
  if(__atomic_load_n(&nx2_raw_fault.state,__ATOMIC_ACQUIRE)>=2){
    const volatile unsigned char *source=(const volatile unsigned char *)&nx2_raw_fault;
    for(unsigned i=0;i<sizeof(Nx2RawFault);i++)block[40+i]=source[i];
    uint32_t checksum=2166136261u;
    for(unsigned i=0;i<sizeof(Nx2RawFault);i++)checksum=(checksum^block[40+i])*16777619u;
    put32(12,1);put32(36,checksum);
    /* Best effort; no synthesized success or retry loop in a faulted
     * process. Complete capture is checked offline using magic/checksum. */
    Result rc=fsFileWrite(&raw_file,0,block,sizeof(block),FsWriteOption_Flush);
    __atomic_store_n(&nx2_raw_fault_trace_result,rc,__ATOMIC_RELEASE);
  }
  __atomic_store_n(&busy,0,__ATOMIC_RELEASE);
}
