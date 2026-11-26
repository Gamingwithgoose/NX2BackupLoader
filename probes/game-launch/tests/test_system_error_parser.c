/* SPDX-License-Identifier: GPL-2.0-only */
/* Source fixture only: not built or executed as part of the source-only edit.
 * Host command: cc -Wall -Wextra -Werror test_system_error_parser.c -o test-erpt
 * No console services or report payloads are needed. */
#define NX2_SYSTEM_ERROR_PARSER_ONLY
#include "../source/system_error.c"
#include <assert.h>
static unsigned char data[4096];
static size_t used;
static void put(unsigned c){assert(used<sizeof(data));data[used++]=(unsigned char)c;}
static void str(const char *s){
  size_t n=strlen(s);assert(n<32);put(0xa0|(unsigned)n);
  for(size_t i=0;i<n;i++)put((unsigned char)s[i]);
}
static void identity(unsigned pairs){
  used=0;put(0x80|pairs);
  str("ProgramId");str("0400c3f00006e000");
  str("ErrorCode");str("2168-0002");
}
int main(void){
  Summary s;
  unsigned char owner[20]={1},records[8+2*ATTACHMENT_RECORD_BYTES]={0};
  size_t prefix=99;
  /* Reserved tail bytes differ: public UUID equality must still work. */
  memcpy(records,owner,16);records[16]=0xff;records[20]=2;
  memcpy(records+ATTACHMENT_RECORD_BYTES,owner,16);
  records[ATTACHMENT_RECORD_BYTES+16]=0xee;
  records[ATTACHMENT_RECORD_BYTES+20]=3;
  assert(attachment_list_layout(records,sizeof(records),2,owner,&prefix)&&prefix==0);
  records[ATTACHMENT_RECORD_BYTES]=4;
  assert(!attachment_list_layout(records,sizeof(records),2,owner,&prefix));
  records[ATTACHMENT_RECORD_BYTES]=1;
  memmove(records+8,records,2*ATTACHMENT_RECORD_BYTES);memset(records,0,8);
  records[0]=2;
  assert(attachment_list_layout(records,sizeof(records),2,owner,&prefix)&&prefix==8);
  assert(!attachment_list_layout(records,sizeof(records)-1,2,owner,&prefix));
  records[4]=1;
  assert(!attachment_list_layout(records,sizeof(records),2,owner,&prefix));
  unsigned char compact[128]={0};int64_t bytes=1234;
  compact[0]=1;compact[64]=2;
  memcpy(compact+24,&bytes,8);memcpy(compact+88,&bytes,8);
  memcpy(compact+32,"nxdmp.bin",10);memcpy(compact+96,"syslog.bin",11);
  assert(attachment_compact_candidate(compact,sizeof(compact),2));
  assert(!attachment_compact_candidate(compact,sizeof(compact)-1,2));
  compact[64]=1;
  assert(!attachment_compact_candidate(compact,sizeof(compact),2));
  compact[64]=2;compact[20]=0x80;
  assert(!attachment_compact_candidate(compact,sizeof(compact),2));
  compact[20]=0;compact[96]='\n';
  assert(!attachment_compact_candidate(compact,sizeof(compact),2));
  compact[96]='s';bytes=-1;memcpy(compact+24,&bytes,8);
  assert(!attachment_compact_candidate(compact,sizeof(compact),2));
  /* Console-observed V7 layout/sizes/names, with synthetic UUIDs. */
  unsigned char wire60[120]={0};uint32_t flags60=2,size60=26387;
  wire60[0]=1;wire60[60]=2;
  memcpy(wire60+20,&flags60,4);memcpy(wire60+80,&flags60,4);
  memcpy(wire60+24,&size60,4);size60=14725;memcpy(wire60+84,&size60,4);
  memcpy(wire60+28,"syslog.bin.lz4",15);memcpy(wire60+88,"nxdmp.gz",9);
  assert(attachment_wire60_candidate(wire60,sizeof(wire60),2));
  assert(!attachment_compact_candidate(wire60,sizeof(wire60),2));
  assert(!attachment_wire60_candidate(wire60,sizeof(wire60)-1,2));
  wire60[60]=1;assert(!attachment_wire60_candidate(wire60,sizeof(wire60),2));
  wire60[60]=2;wire60[20]=0x80;
  assert(!attachment_wire60_candidate(wire60,sizeof(wire60),2));
  wire60[20]=2;size60=0;memcpy(wire60+24,&size60,4);
  assert(!attachment_wire60_candidate(wire60,sizeof(wire60),2));
  identity(2);size_t basic=used;
  assert(parse_report(data,used,&s));
  assert(s.program==UINT64_C(0x0400c3f00006e000));
  assert(!strcmp(s.code,"2168-0002"));
  /* Every truncated prefix must fail, including empty and map header only. */
  for(size_t n=0;n<basic;n++)assert(!parse_report(data,n,&s));
  put(0);assert(!parse_report(data,used,&s)); /* Trailing data is not a report. */
  identity(3);str("ProgramId");str("0400486024dec000");
  assert(!parse_report(data,used,&s)); /* Duplicate identity: no attribution. */
  identity(3);str("StackBacktrace64");put(0x92);put(0xcf);
  for(unsigned i=0;i<8;i++)put(i);
  put(0xcc);put(0xff);
  assert(parse_report(data,used,&s));
  assert(s.count[2]==2&&s.arrays[2][0]==UINT64_C(0x0001020304050607));
  assert(s.arrays[2][1]==255);
  identity(3);str("AbortType");put(0xd0);put(0xff);
  assert(!parse_report(data,used,&s)); /* Negative scalar rejected. */
  assert(s.rejected_field==2&&s.value_tag==0xd0);
  identity(3);str("StackBacktrace64");put(0xdc);put(0);put(65);
  assert(!parse_report(data,used,&s)); /* Bound checked before traversal. */
  identity(3);str("SSID");put(0x81);str("private");put(0x92);str("secret");put(0xc3);
  assert(parse_report(data,used,&s));
  assert(s.seen==3); /* Unknown private fields skipped, not retained. */
  FILE *schema=tmpfile();assert(schema);
  write_report_shape(schema,data,used);
  rewind(schema);char text[2048]={0};size_t read=fread(text,1,sizeof(text)-1,schema);
  assert(read&&!ferror(schema));fclose(schema);
  assert(strstr(text,"key=SSID")&&strstr(text,"values=omitted"));
  assert(!strstr(text,"private")&&!strstr(text,"secret"));
  assert(!strstr(text,"0400c3f00006e000")&&!strstr(text,"2168-0002"));
  used=0;put(0x81);str("Telemetry");put(1);
  assert(!parse_report(data,used,&s)&&s.complete&&s.seen==0);
  assert(!parse_report(data,used-1,&s)&&!s.complete);
  identity(4);str("NxdmpIsAttached");put(0xc3);
  str("SystemThreadCountPeak");put(0xcd);put(1);put(0);
  assert(parse_report(data,used,&s));
  schema=tmpfile();assert(schema);write_report_shape(schema,data,used);
  rewind(schema);memset(text,0,sizeof(text));read=fread(text,1,sizeof(text)-1,schema);
  assert(read&&!ferror(schema));fclose(schema);
  assert(strstr(text,"key=NxdmpIsAttached boolean=1"));
  assert(strstr(text,"key=SystemThreadCountPeak unsigned=256"));
  identity(3);str("unknown");
  for(unsigned i=0;i<18;i++)put(0x91);
  put(0);assert(!parse_report(data,used,&s)); /* Nested container limit. */
  identity(3);str("unknown");put(0xc6);put(0xff);put(0xff);put(0xff);put(0xff);
  assert(!parse_report(data,used,&s)); /* Oversized binary is bounded. */
  used=0;put(0x82);str("ProgramId");str("not-a-program-id!");
  str("ErrorCode");str("2168-0002");
  assert(!parse_report(data,used,&s));
  identity(2);data[0]=0xde;memmove(data+3,data+1,used-1);
  data[1]=0;data[2]=2;used+=2;
  assert(parse_report(data,used,&s)); /* Map16 used by real formatter. */
  puts("ERPT parser fixtures passed");return 0;
}
