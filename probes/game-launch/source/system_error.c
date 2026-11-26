/* SPDX-License-Identifier: GPL-2.0-only */
#include "system_error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifndef NX2_SYSTEM_ERROR_PARSER_ONLY
#include <switch.h>
#endif

/* ERPT wire definitions verified against Atmosphere-NX/Atmosphere:
 * erpt_types.hpp, sf/erpt_sf_i_{manager,report}.hpp, and
 * srv/erpt_srv_formatter.hpp. Firmware may differ: fail explicitly rather
 * than scan arbitrary bytes for a code and attribute an unrelated report.
 * No SetFlags, Cleanup, Delete, or Pop commands are issued here. */
#define MARK_PATH "sdmc:/BackupLoaderMKW-last-launch.txt"
#define LOG_PATH "sdmc:/BackupLoaderMKW-system-error.log"
#define REPORT_LIMIT (1024u*1024u)
#define ATTACHMENT_RECORD_BYTES 0x58u
#define ATTACHMENT_CAPACITY_MAX 64u
#define REPORT_UUID_BYTES 16u
/* Public ERPT module147, description51. Keep the native result visible. */
#define ERPT_TOO_MANY_OUT_ATTACHMENTS 0x6693u
typedef struct {const unsigned char *p,*end; unsigned budget;} Reader;
typedef struct {
  uint64_t program;
  unsigned seen;
  char code[33];
  uint64_t scalar[6];
  uint64_t arrays[3][64];
  unsigned count[3];
  unsigned rejected_field,value_tag;
  size_t value_offset;
  int complete;
} Summary;
static const char *const fields[]={
  "ProgramId","ErrorCode","AbortType","ProgramMappedAddr64",
  "GpuErrorFaultAddress","GpuErrorFaultUnit","GpuErrorFaultType",
  "GpuErrorFaultInfo","GeneralRegisterAarch64","ExceptionInfoAarch64",
  "StackBacktrace64"};
static int take(Reader *r,size_t n,const unsigned char **p){
  if(n>(size_t)(r->end-r->p))return 0;
  if(p)*p=r->p;
  r->p+=n;return 1;
}
static int number(Reader *r,unsigned n,uint64_t *v){
  const unsigned char *p;
  if(!take(r,n,&p))return 0;
  *v=0;while(n--)*v=(*v<<8)|*p++;
  return 1;
}
static int byte(Reader *r,unsigned *v){
  const unsigned char *p;
  if(!r->budget||!take(r,1,&p))return 0;
  --r->budget;*v=*p;return 1;
}
static int string(Reader *r,const unsigned char **p,size_t *n){
  unsigned t;uint64_t size;
  if(!byte(r,&t))return 0;
  if((t&0xe0)==0xa0)size=t&31;
  else if(t>=0xd9&&t<=0xdb){if(!number(r,1u<<(t-0xd9),&size))return 0;}
  else return 0;
  if(size>(uint64_t)(r->end-r->p))return 0;
  *n=(size_t)size;return take(r,*n,p);
}
static int container(Reader *r,int map,uint64_t *n){
  unsigned t;if(!byte(r,&t))return 0;
  if((t&0xf0)==(map?0x80:0x90)){*n=t&15;return 1;}
  unsigned base=map?0xde:0xdc;
  if(t!=base&&t!=base+1)return 0;
  return number(r,t==base?2:4,n);
}
static int integer(Reader *r,uint64_t *v){
  unsigned t;if(!byte(r,&t))return 0;
  if(t<0x80){*v=t;return 1;}
  if(t>=0xcc&&t<=0xcf)return number(r,1u<<(t-0xcc),v);
  /* Positive signed values are valid too; reject negative identities. */
  if(t>=0xd0&&t<=0xd3){
    unsigned n=1u<<(t-0xd0);
    if(!number(r,n,v))return 0;
    return !(*v&(UINT64_C(1)<<(n*8-1)));
  }
  return 0;
}
static int skip(Reader *r,unsigned depth){
  unsigned t;uint64_t n=0;
  if(depth>16||!byte(r,&t))return 0;
  if(t<0x80||t>=0xe0||t==0xc0||t==0xc2||t==0xc3)return 1;
  if((t&0xe0)==0xa0)return take(r,t&31,NULL);
  if((t&0xf0)==0x80||(t&0xf0)==0x90){
    n=t&15;if((t&0xf0)==0x80)n*=2;
  }else if(t>=0xdc&&t<=0xdf){
    if(!number(r,(t&1)?4:2,&n))return 0;
    if(t>=0xde)n*=2;
  }else{
    if(t>=0xcc&&t<=0xcf)return take(r,1u<<(t-0xcc),NULL);
    if(t>=0xd0&&t<=0xd3)return take(r,1u<<(t-0xd0),NULL);
    if(t==0xca||t==0xcb)return take(r,t==0xca?4:8,NULL);
    if(t>=0xc4&&t<=0xc6){if(!number(r,1u<<(t-0xc4),&n))return 0;}
    else if(t>=0xd9&&t<=0xdb){if(!number(r,1u<<(t-0xd9),&n))return 0;}
    else return 0; /* No extension types in the known ERPT formatter. */
    return n<=(uint64_t)(r->end-r->p)&&take(r,(size_t)n,NULL);
  }
  if(n>r->budget)return 0;
  while(n--)if(!skip(r,depth+1))return 0;
  return 1;
}
static int parse_report(const unsigned char *data,size_t bytes,Summary *s){
  Reader r={data,data+bytes,100000};uint64_t count;
  memset(s,0,sizeof(*s));
  s->rejected_field=UINT32_MAX;
  if(!container(&r,1,&count)||count>4096)return 0;
  while(count--){
    const unsigned char *key,*value;size_t len,n;
    if(!string(&r,&key,&len))return 0;
    unsigned field;
    for(field=0;field<sizeof(fields)/sizeof(fields[0]);field++)
      if(len==strlen(fields[field])&&!memcmp(key,fields[field],len))break;
    s->rejected_field=field;
    s->value_offset=(size_t)(r.p-data);
    s->value_tag=r.p<r.end?*r.p:0x100;
    if(field==sizeof(fields)/sizeof(fields[0])){if(!skip(&r,0))return 0;continue;}
    if(s->seen&(1u<<field))return 0;
    s->seen|=1u<<field;
    if(field<2){
      if(!string(&r,&value,&n))return 0;
      if(!field){
        if(n==18&&value[0]=='0'&&(value[1]=='x'||value[1]=='X')){value+=2;n-=2;}
        if(n!=16)return 0;
        for(size_t i=0;i<n;i++){
          unsigned c=value[i],digit;
          if(c>='0'&&c<='9')digit=c-'0';
          else if(c>='a'&&c<='f')digit=c-'a'+10;
          else if(c>='A'&&c<='F')digit=c-'A'+10;
          else return 0;
          s->program=(s->program<<4)|digit;
        }
      }else{
        if(!n||n>=sizeof(s->code))return 0;
        for(size_t i=0;i<n;i++)if(value[i]<0x21||value[i]>0x7e)return 0;
        memcpy(s->code,value,n);s->code[n]=0;
      }
    }else if(field<8){if(!integer(&r,&s->scalar[field-2]))return 0;}
    else{
      uint64_t items;
      if(!container(&r,0,&items)||items>64)return 0;
      s->count[field-8]=(unsigned)items;
      for(unsigned i=0;i<items;i++)if(!integer(&r,&s->arrays[field-8][i]))return 0;
    }
  }
  s->complete=r.p==r.end;
  return s->complete&&(s->seen&3)==3;
}
static int technical_key(const unsigned char *key,size_t len){
  static const char *const names[]={
    "AbortFlag","ApplicationAbortFlag","CrashReportFlag","NxdmpIsAttached",
    "SyslogIsAttached","GpuCrashDumpIsAttached","EncryptedExceptionInfo",
    "EncryptedExceptionInfo1","EncryptedExceptionInfo2","EncryptedExceptionInfo3",
    "EncryptionKeyGeneration","EncryptionKeyV1",
    "SystemPhysicalMemoryLimit","SystemPhysicalMemoryPeak",
    "SystemThreadCountLimit","SystemThreadCountPeak",
    "SystemEventCountLimit","SystemEventCountPeak",
    "SystemTransferMemoryCountLimit","SystemTransferMemoryCountPeak",
    "SystemSessionCountLimit","SystemSessionCountPeak"};
  for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);i++)
    if(len==strlen(names[i])&&!memcmp(key,names[i],len))return 1;
  return 0;
}
/* Public ReportId/AttachmentId occupy20 wire bytes, but their documented
 * equality operators compare the16-byte UUID, not all padding/reserved
 * bytes. This legacy-layout helper requires a matching owner UUID for
 * EVERY record; it does not recognize ownerless records. */
static int attachment_list_layout(const unsigned char *data,size_t bytes,
                                  uint32_t count,const unsigned char *owner,
                                  size_t *prefix){
  if(!count||count>ATTACHMENT_CAPACITY_MAX)return 0;
  unsigned matches=0;
  for(unsigned candidate=0;candidate<2;candidate++){
    size_t start=candidate?8:0;
    if(start>bytes||(size_t)count>(bytes-start)/ATTACHMENT_RECORD_BYTES)continue;
    if(start){
      uint32_t listed=0,reserved=0;
      memcpy(&listed,data,4);memcpy(&reserved,data+4,4);
      if(listed!=count||reserved)continue;
    }
    int valid=1;
    for(uint32_t i=0;i<count;i++){
      const unsigned char *entry=data+start+(size_t)i*ATTACHMENT_RECORD_BYTES;
      int64_t size=0;unsigned nonzero=0;
      memcpy(&size,entry+0x30,8);
      for(unsigned j=0;j<16;j++)nonzero|=entry[20+j];
      if(memcmp(entry,owner,REPORT_UUID_BYTES)||size<0||!nonzero){valid=0;break;}
    }
    if(valid){++matches;*prefix=start;}
  }
  return matches==1;
}
/* Experimental ownerless compact profile. Observations: native list scoped
 * to one report returns2 records, contains no owner UUID anywhere, and its
 * minimum buffer requirement differs from the public88-byte structure.
 * This is a HYPOTHESIS, not a documented Switch2 ABI. Structural validation
 * must precede read-only native Open/GetSize validation for every record.
 * Nothing here authorizes reading or exporting an attachment body. */
static int attachment_compact_candidate(const unsigned char *data,size_t bytes,
                                        uint32_t count){
  if(!count||count>ATTACHMENT_CAPACITY_MAX||(size_t)count>bytes/64)return 0;
  for(uint32_t i=0;i<count;i++){
    const unsigned char *entry=data+(size_t)i*64;
    uint32_t flags=0;int64_t size=0;unsigned nonzero=0;
    memcpy(&flags,entry+20,4);memcpy(&size,entry+24,8);
    for(unsigned j=0;j<16;j++)nonzero|=entry[j];
    if(!nonzero||(flags&~3u)||size<=0)return 0;
    unsigned n=0;
    while(n<32&&entry[32+n]){
      unsigned c=entry[32+n++];
      if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||
           (c>='0'&&c<='9')||c=='.'||c=='_'||c=='-'))return 0;
    }
    if(!n||n==32)return 0;
    for(uint32_t j=0;j<i;j++)
      if(!memcmp(entry,data+(size_t)j*64,16))return 0;
  }
  return 1;
}
/* V7 console wire evidence: two60-byte records. ID20, flagsU32, sizeU32,
 * filename32. First size0x6713/name syslog.bin.lz4, second at60 has size
 * 0x3985/name nxdmp.gz. Native Open/GetSize must still confirm each record. */
static int attachment_wire60_candidate(const unsigned char *data,size_t bytes,
                                       uint32_t count){
  if(!count||count>ATTACHMENT_CAPACITY_MAX||(size_t)count>bytes/60)return 0;
  for(uint32_t i=0;i<count;i++){
    const unsigned char *entry=data+(size_t)i*60;
    uint32_t flags=0,size=0;unsigned nonzero=0;
    memcpy(&flags,entry+20,4);memcpy(&size,entry+24,4);
    for(unsigned j=0;j<16;j++)nonzero|=entry[j];
    if(!nonzero||(flags&~3u)||!size)return 0;
    unsigned n=0;
    while(n<32&&entry[28+n]){
      unsigned c=entry[28+n++];
      if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||
           (c>='0'&&c<='9')||c=='.'||c=='_'||c=='-'))return 0;
    }
    if(!n||n==32)return 0;
    for(uint32_t j=0;j<i;j++)if(!memcmp(entry,data+(size_t)j*60,16))return 0;
  }
  return 1;
}
/* For a completely parsed, host-matched report only, expose schema rather
 * than unknown values. This distinguishes missing/renamed/encrypted CPU
 * context without exporting serial numbers, account IDs or network data.
 * Root keys are bounded identifiers; unknown string/binary values stay out. */
static void write_report_shape(FILE *log,const unsigned char *data,size_t bytes){
  Reader r={data,data+bytes,100000};uint64_t count;
  if(!container(&r,1,&count)||count>4096)return;
  unsigned extra=0;
  for(uint64_t i=0;i<count;i++){
    const unsigned char *key;size_t len;
    if(!string(&r,&key,&len)||r.p==r.end)return;
    const unsigned char *begin=r.p;unsigned tag=*begin;
    if(!skip(&r,0))return;
    int technical=technical_key(key,len);
    if(i>=128&&(!technical||extra>=32))continue;
    if(i>=128)++extra;
    int identifier=len>0&&len<=63;
    for(size_t j=0;j<len&&identifier;j++){
      unsigned c=key[j];
      if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||
           (c>='0'&&c<='9')||c=='_'))identifier=0;
    }
    if(identifier)fprintf(log,"REPORT_FIELD_SHAPE key=%.*s tag=%02x encoded_bytes=%u values=omitted\n",
        (int)len,(const char *)key,tag,(unsigned)(r.p-begin));
    if(technical&&identifier){
      /* Only allowlisted numeric/boolean technical values. Encryption
       * blobs and every unrecognized string/binary value remain omitted. */
      Reader value={begin,r.p,100000};uint64_t numeric;
      if(tag==0xc2||tag==0xc3)
        fprintf(log,"REPORT_TECHNICAL key=%.*s boolean=%u\n",(int)len,(const char *)key,tag==0xc3);
      else if(integer(&value,&numeric)&&value.p==value.end)
        fprintf(log,"REPORT_TECHNICAL key=%.*s unsigned=%llu\n",(int)len,(const char *)key,
            (unsigned long long)numeric);
    }
  }
  if(count>128)fprintf(log,"REPORT_FIELD_SHAPE omitted_keys=%u additional_technical_keys=%u\n",
      (unsigned)(count-128-extra),extra);
}
#ifndef NX2_SYSTEM_ERROR_PARSER_ONLY
int nx2_system_error_mark(uint64_t host,uint64_t guest){
  /* Invalidate a stale marker even when the console clock is unusable. */
  FILE *f=fopen(MARK_PATH,"w");if(!f)return 0;
  time_t now=time(NULL);
  int ok=fprintf(f,"NX2_ERPT_RUN_V1 %016llx %016llx %lld\n",
      (unsigned long long)host,(unsigned long long)guest,(long long)now)>0;
  if(fclose(f))ok=0;
  return ok;
}
static int export_diagnostic_attachment(Service *attachment,s64 bytes,s64 when,
                                        const char *name,FILE *log){
  const char *role=NULL,*extension=NULL;
  if(!strcmp(name,"nxdmp.gz")){role="crash-dump";extension="gz";}
  else if(!strcmp(name,"syslog.bin.lz4")){role="syslog";extension="lz4";}
  else return 0;
  if(bytes<=0||bytes>REPORT_LIMIT){
    fprintf(log,"ATTACHMENT_EXPORT kind=%s skipped=size_bound bytes=%lld max=%u\n",role,(long long)bytes,REPORT_LIMIT);
    return 0;
  }
  unsigned char *buffer=malloc(16384);
  if(!buffer){fprintf(log,"ATTACHMENT_EXPORT kind=%s skipped=allocation_failed\n",role);return 0;}
  char path[160],part[168];
  snprintf(path,sizeof(path),"sdmc:/BackupLoaderMKW-%s-%lld.%s",role,(long long)when,extension);
  snprintf(part,sizeof(part),"%s.part",path);
  FILE *out=fopen(part,"wb");
  if(!out){fprintf(log,"ATTACHMENT_EXPORT kind=%s path=%s opened=0 complete=0\n",role,path);free(buffer);return 0;}
  size_t written=0;Result rc=0;int io_ok=1;
  while(written<(size_t)bytes){
    size_t request=(size_t)bytes-written;if(request>16384)request=16384;
    u32 got=0;
    rc=serviceDispatchOut(attachment,1,got,
        .buffer_attrs={SfBufferAttr_HipcMapAlias|SfBufferAttr_Out},
        .buffers={{buffer,request}});
    if(R_FAILED(rc)||!got||got>request){io_ok=0;break;}
    if(fwrite(buffer,1,got,out)!=got){io_ok=0;break;}
    written+=got;
  }
  int closed=fclose(out)==0;
  free(buffer);
  int complete=io_ok&&closed&&written==(size_t)bytes;
  int published=complete&&rename(part,path)==0;
  fprintf(log,"ATTACHMENT_EXPORT kind=%s path=%s native_read_result=%08x bytes=%u expected=%lld write_ok=%u close_ok=%u complete=%u staging_path=%s verbatim=1\n",
      role,path,rc,(unsigned)written,(long long)bytes,(unsigned)io_ok,(unsigned)closed,(unsigned)published,part);
  return published;
}
static unsigned attachment_capture(Service *root,Service *mgr,
                                    const unsigned char *report_id,s64 when,FILE *log){
  /* Firmware20+ GetAttachmentList returns count separately, then an array
   * of AttachmentInfo records, NOT the deprecated prefixed list. Public
   * types used five records, but actual Switch2 returned 0x6693 for that
   * capacity. Grow only for that explicit capacity error, with a hard cap.
   * Keep the known 0x58-byte layout provisional: owner checks below stop
   * incompatible records from opening unrelated attachment IDs. */
  unsigned char entries[ATTACHMENT_CAPACITY_MAX*ATTACHMENT_RECORD_BYTES]={0};
  /* The current public implementation requires space for at least10
   * records regardless of the actual count. Start at16, not the old5/8. */
  u32 count=0,capacity=16;Result rc;
  struct {u8 id[20];} owner;memcpy(owner.id,report_id,sizeof(owner.id));
  for(;;){
    count=0;memset(entries,0,sizeof(entries));
    rc=serviceDispatchInOut(mgr,6,owner,count,
        .buffer_attrs={SfBufferAttr_HipcMapAlias|SfBufferAttr_Out},
        .buffers={{entries,(size_t)capacity*ATTACHMENT_RECORD_BYTES}});
    fprintf(log,"ATTACHMENT_LIST result=%08x count=%u capacity=%u record_bytes=%u metadata_only=1\n",
        rc,count,capacity,ATTACHMENT_RECORD_BYTES);
    if(rc!=ERPT_TOO_MANY_OUT_ATTACHMENTS||capacity==ATTACHMENT_CAPACITY_MAX)break;
    capacity*=2;
  }
  if(R_FAILED(rc))return 0;
  if(count>capacity){fprintf(log,"ATTACHMENT_LAYOUT invalid_count=1 not_opened=1\n");return 0;}
  if(!count)return 0;
  size_t prefix=0,available=(size_t)capacity*ATTACHMENT_RECORD_BYTES;
  size_t stride=ATTACHMENT_RECORD_BYTES,id_offset=20,size_offset=0x30,name_offset=0x38;
  int compact=0;unsigned size_bytes=8;
  if(!attachment_list_layout(entries,available,count,report_id,&prefix)){
    fprintf(log,"ATTACHMENT_LAYOUT legacy_layout_unconfirmed=1 uuid_comparison_bytes=%u legacy_ids_not_opened=1\n",REPORT_UUID_BYTES);
    /* Only offsets of an already-known owner UUID, no raw ID/name/body
     * bytes. This can identify a changed envelope/stride without guessing
     * a new layout or opening any unvalidated handle. */
    unsigned found=0;
    for(size_t at=0;at+REPORT_UUID_BYTES<=available&&found<32;at++)
      if(!memcmp(entries+at,report_id,REPORT_UUID_BYTES)){
        fprintf(log,"ATTACHMENT_OWNER_UUID_OFFSET offset=%u\n",(unsigned)at);++found;
      }
    fprintf(log,"ATTACHMENT_OWNER_UUID_SCAN matches=%u capped_at=32 bytes=%u\n",found,(unsigned)available);
    unsigned nonzero=0;size_t last_nonzero=0;
    for(size_t at=0;at<available;at++)if(entries[at]){++nonzero;last_nonzero=at+1;}
    fprintf(log,"ATTACHMENT_BUFFER_SHAPE nonzero_bytes=%u last_nonzero_end=%u raw_values=omitted\n",
        nonzero,(unsigned)last_nonzero);
    /* This buffer is attachment-list METADATA from the report-scoped list
     * request, not report or dump contents. Capture its actual wire shape
     * instead of adding another guessed record stride. IDs/technical names
     * may appear here; no attachment Read command is issued. Hard cap256. */
    size_t wire_bytes=(last_nonzero+15u)&~(size_t)15u;
    if(wire_bytes<16)wire_bytes=16;
    if(wire_bytes>256)wire_bytes=256;
    if(wire_bytes>available)wire_bytes=available;
    fprintf(log,"ATTACHMENT_WIRE_METADATA captured_bytes=%u native_count=%u metadata_only=1 attachment_bodies=omitted\n",
        (unsigned)wire_bytes,count);
    for(size_t at=0;at<wire_bytes;at+=16){
      fprintf(log,"ATTACHMENT_WIRE offset=%04x hex=",(unsigned)at);
      for(size_t j=at;j<wire_bytes&&j<at+16;j++)fprintf(log,"%02x",entries[j]);
      fputc('\n',log);
    }
    if(!found&&attachment_wire60_candidate(entries,available,count)){
      compact=2;prefix=0;stride=60;id_offset=0;size_offset=24;size_bytes=4;name_offset=28;
      fprintf(log,"ATTACHMENT_WIRE60 structural_match=1 stride=60 size_bytes=4 report_filter_scoped=1 native_validation_required=1\n");
    }else if(!found&&attachment_compact_candidate(entries,available,count)){
      compact=1;prefix=0;stride=64;id_offset=0;size_offset=24;name_offset=32;
      fprintf(log,"ATTACHMENT_COMPACT_CANDIDATE structural_match=1 stride=64 documented_abi=0 report_filter_scoped=1 native_validation_required=1 body_read=0\n");
    }else{
      fprintf(log,"ATTACHMENT_COMPACT_CANDIDATE structural_match=0 not_opened=1\n");return 0;
    }
  }else{
    fprintf(log,"ATTACHMENT_LAYOUT verified_owner_uuids=1 prefix=%u stride=%u uuid_comparison_bytes=%u\n",
        (unsigned)prefix,ATTACHMENT_RECORD_BYTES,REPORT_UUID_BYTES);
  }
  unsigned validated=0,failed=0,exports=0;
  for(u32 i=0;i<count;i++){
    const unsigned char *entry=entries+prefix+i*stride;s64 listed_size=0;
    if(size_bytes==4){uint32_t small=0;memcpy(&small,entry+size_offset,4);listed_size=small;}
    else memcpy(&listed_size,entry+size_offset,sizeof(listed_size));
    char name[33]={0};memcpy(name,entry+name_offset,32);
    for(unsigned j=0;j<32&&name[j];j++)
      if(name[j]>='A'&&name[j]<='Z')name[j]+='a'-'A';
    const char *kind=strstr(name,"nxdmp")?"nxdmp":
        strstr(name,"syslog")?"syslog":strstr(name,"gpu")?"gpu":"other";
    Service attachment={0};s64 actual_size=0;
    rc=serviceDispatch(root,2,.out_num_objects=1,.out_objects=&attachment);
    if(R_FAILED(rc)){
      fprintf(log,"ATTACHMENT_SESSION index=%u result=%08x\n",i,rc);++failed;continue;
    }
    struct {u8 id[20];} in;memcpy(in.id,entry+id_offset,sizeof(in.id));
    rc=serviceDispatchIn(&attachment,0,in);
    int opened=R_SUCCEEDED(rc);
    if(opened)rc=serviceDispatchOut(&attachment,5,actual_size);
    int size_matches=R_SUCCEEDED(rc)&&actual_size==listed_size;
    if(size_matches)++validated;else ++failed;
    fprintf(log,"ATTACHMENT_METADATA index=%u kind=%s listed_bytes=%lld size_result=%08x actual_bytes=%lld size_matches=%u compact_candidate=%u body_read=0\n",
        i,kind,(long long)listed_size,rc,(long long)actual_size,(unsigned)size_matches,(unsigned)compact);
    /* Export only the two exact diagnostic types observed on hardware,
     * after the native service has confirmed the proposed ID and size.
     * An unverified experimental64-byte profile remains metadata-only. */
    if(size_matches&&compact!=1)
      exports+=(unsigned)export_diagnostic_attachment(&attachment,actual_size,when,name,log);
    if(opened)serviceDispatch(&attachment,4);
    serviceClose(&attachment);
  }
  fprintf(log,"ATTACHMENT_NATIVE_VALIDATION stride=%u native_size_matches=%u failures=%u all_records_validated=%u diagnostic_exports=%u\n",
      (unsigned)stride,validated,failed,(unsigned)(validated==count&&!failed),exports);
  return exports;
}
static int read_report(Service *root,const unsigned char *id,
                       unsigned char *data,size_t *bytes,FILE *log){
  Service rep={0};s64 size=0;
  Result rc=serviceDispatch(root,0,.out_num_objects=1,.out_objects=&rep);
  if(R_FAILED(rc)){fprintf(log,"REPORT_SESSION result=%08x\n",rc);return 0;}
  struct {u8 id[20];} in;
  memcpy(in.id,id,sizeof(in.id));
  rc=serviceDispatchIn(&rep,0,in);
  int opened=R_SUCCEEDED(rc),ok=0;
  if(opened)rc=serviceDispatchOut(&rep,5,size);
  if(R_SUCCEEDED(rc)&&size>0&&size<=REPORT_LIMIT){
    size_t offset=0;
    while(offset<(size_t)size){
      u32 got=0;size_t request=(size_t)size-offset;
      if(request>16384)request=16384;
      rc=serviceDispatchOut(&rep,1,got,
          .buffer_attrs={SfBufferAttr_HipcMapAlias|SfBufferAttr_Out},
          .buffers={{data+offset,request}});
      if(R_FAILED(rc)||!got||got>request)break;
      offset+=got;
    }
    if(offset==(size_t)size){*bytes=offset;ok=1;}
  }
  fprintf(log,"REPORT_READ result=%08x size=%lld complete=%u\n",rc,(long long)size,ok);
  if(opened)serviceDispatch(&rep,4);
  serviceClose(&rep);return ok;
}
void nx2_system_error_read(char *outcome,size_t capacity){
  FILE *log=fopen(LOG_PATH,"w");
  if(!log){snprintf(outcome,capacity,"Y: cannot write system-error.log");return;}
  fprintf(log,"SYSTEM_ERROR_READ version=8 menu_only=1 system_reports_read_only=1 report_body_export=0 matched_diagnostic_exports=1 per_attachment_max=%u wire60_console_observed=1\n",REPORT_LIMIT);
  unsigned long long host=0,guest=0;long long start=0;
  FILE *mark=fopen(MARK_PATH,"r");
  int valid=mark&&fscanf(mark,"NX2_ERPT_RUN_V1 %llx %llx %lld",&host,&guest,&start)==3;
  if(mark)fclose(mark);
  long long now=(long long)time(NULL);
  if(!valid||!host||start<1577836800||start>now+5){
    fprintf(log,"RESULT unavailable=missing_or_invalid_launch_marker no_attribution=1\n");
    snprintf(outcome,capacity,"Y: no valid launch marker; test a game with this source first");
    fclose(log);return;
  }
  fprintf(log,"RUN host=%016llx guest=%016llx start=%lld now=%lld window_seconds=600\n",
      host,guest,start,now);
  Service root={0},mgr={0};unsigned char *data=NULL;
  Result rc=smGetService(&root,"erpt:r");
  fprintf(log,"ERPT_SERVICE result=%08x\n",rc);
  if(R_FAILED(rc))goto unavailable;
  rc=serviceDispatch(&root,1,.out_num_objects=1,.out_objects=&mgr);
  fprintf(log,"ERPT_MANAGER result=%08x\n",rc);
  if(R_FAILED(rc))goto unavailable;
  unsigned char list[0x15e8]={0};const u32 type=2;
  rc=serviceDispatchIn(&mgr,0,type,
      .buffer_attrs={SfBufferAttr_HipcMapAlias|SfBufferAttr_Out},
      .buffers={{list,sizeof(list)}});
  fprintf(log,"ERPT_LIST result=%08x\n",rc);
  if(R_FAILED(rc))goto unavailable;
  u32 count=0;memcpy(&count,list,sizeof(count));
  if(count>50){fprintf(log,"RESULT unsupported_list count=%u\n",count);goto unsupported;}
  data=malloc(REPORT_LIMIT);
  if(!data){fprintf(log,"RESULT allocation_failed\n");goto unsupported;}
  Summary best={0};s64 best_time=0;unsigned char best_id[20]={0};
  unsigned matched=0,unreadable=0,unidentified=0;
  for(u32 i=0;i<count;i++){
    const unsigned char *entry=list+8+i*0x70;s64 when=0;
    memcpy(&when,entry+0x40,sizeof(when));
    /* Do not assume report ordering. Both program ID and clock must match.
     * A same-host fault in this window is a candidate, not proof of cause. */
    if(when<start-5||when>start+600||when>now+5)continue;
    size_t bytes=0;Summary summary;
    if(!read_report(&root,entry+4,data,&bytes,log)){++unreadable;continue;}
    if(!parse_report(data,bytes,&summary)){
      if(summary.complete){
        fprintf(log,"REPORT_IGNORED reason=missing_crash_identity fields_present=%u no_attribution=1\n",summary.seen&3u);
        ++unidentified;continue;
      }
      const char *field=summary.rejected_field<sizeof(fields)/sizeof(fields[0])?
          fields[summary.rejected_field]:"unknown_or_header";
      fprintf(log,"REPORT_PARSE supported=0 bytes=%u field=%s value_tag=%03x value_offset=%u no_attribution=1\n",
          (unsigned)bytes,field,summary.value_tag,(unsigned)summary.value_offset);
      ++unreadable;continue;
    }
    if(summary.program!=host)continue;
    fprintf(log,"REPORT_MATCH timestamp=%lld program=%016llx error=%s plain_cpu_fields=%u\n",
        (long long)when,(unsigned long long)summary.program,summary.code,
        (summary.seen>>8)&7u);
    write_report_shape(log,data,bytes);
    ++matched;
    if(when>=best_time){best_time=when;best=summary;memcpy(best_id,entry+4,sizeof(best_id));}
  }
  fprintf(log,"RESULT host_time_matches=%u unreadable_candidates=%u missing_identity=%u absence_is_not_proof_of_no_fault=1\n",
      matched,unreadable,unidentified);
  if(matched){
    fprintf(log,"HOST_TIME_CANDIDATE timestamp=%lld program=%016llx guest=%016llx error=%s attribution=host_process_only\n",
        (long long)best_time,(unsigned long long)best.program,guest,best.code);
    for(unsigned i=2;i<8;i++)if(best.seen&(1u<<i))
      fprintf(log,"FIELD %s=%016llx\n",fields[i],(unsigned long long)best.scalar[i-2]);
    for(unsigned i=8;i<11;i++)if(best.seen&(1u<<i))
      for(unsigned j=0;j<best.count[i-8];j++)
        fprintf(log,"FIELD %s[%u]=%016llx\n",fields[i],j,(unsigned long long)best.arrays[i-8][j]);
    unsigned exports=attachment_capture(&root,&mgr,best_id,best_time,log);
    snprintf(outcome,capacity,"Y: host report %s, %u diagnostic files saved; see system-error.log",best.code,exports);
  }else snprintf(outcome,capacity,"Y: no matching readable report; see system-error.log");
  goto done;
unsupported:
  snprintf(outcome,capacity,"Y: report format or memory unavailable; see system-error.log");
  goto done;
unavailable:
  snprintf(outcome,capacity,"Y: ERPT unavailable (%08x); see system-error.log",rc);
done:
  free(data);serviceClose(&mgr);serviceClose(&root);fclose(log);
}
#endif
