/* SPDX-License-Identifier: GPL-2.0-only */
#include "report_archive.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>

#define REPORT_ROOT "sdmc:/switch/BackupLoaderMKW/reports"
/* Bounded; moving/removing archived reports frees slots. Never prune evidence.
 * A bundle is a snapshot of root files, potentially from different launches. */
static const char *const names[]={
 "BackupLoaderMKW-gameplay.log","BackupLoaderMKW-nup.log",
 "BackupLoaderMKW-crash.log","BackupLoaderMKW-host-end.log",
 "BackupLoaderMKW-raw-exception.bin","BackupLoaderMKW-last-launch.txt",
 "BackupLoaderMKW-system-error.log"
};
int nx2_archive_previous_reports(void){
 unsigned existing=0;
 for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);i++){
  char src[128];snprintf(src,sizeof(src),"sdmc:/%s",names[i]);
  FILE *f=fopen(src,"rb");if(f){existing++;fclose(f);}
 }
 if(!existing)return 0;
 if(mkdir(REPORT_ROOT,0777)&&errno!=EEXIST)return -1;
 char dir[160];unsigned slot;
 for(slot=0;slot<16;slot++){
  snprintf(dir,sizeof(dir),REPORT_ROOT "/previous-%02u",slot);
  if(!mkdir(dir,0777))break;
  if(errno!=EEXIST)return -1;
 }
 if(slot==16)return -1;
 unsigned char *buf=malloc(32768);if(!buf)return -1;
 int copied=0,failed=0;
 for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);i++){
  char src[128],dst[256],tmp[264];
  snprintf(src,sizeof(src),"sdmc:/%s",names[i]);
  snprintf(dst,sizeof(dst),"%s/%s",dir,names[i]);
  snprintf(tmp,sizeof(tmp),"%s.part",dst);
  FILE *in=fopen(src,"rb");
  if(!in){if(errno!=ENOENT)failed=1;continue;}
  FILE *out=fopen(tmp,"wb");
  if(!out){fclose(in);failed=1;continue;}
  size_t total=0,n;int ok=1;
  while((n=fread(buf,1,32768,in))!=0){
   if(n>8U*1024*1024-total||fwrite(buf,1,n,out)!=n){ok=0;break;}
   total+=n;
  }
  if(ferror(in))ok=0;
  if(fclose(in))ok=0;
  if(fclose(out))ok=0;
  if(ok&&!rename(tmp,dst))copied++;
  else {remove(tmp);failed=1;}
 }
 free(buf);return failed?-1:copied;
}
