#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include "host/dirent.h"
#ifndef S_ISDIR
#ifdef _WIN32
#define S_ISDIR(m) (((m)&_S_IFMT)==_S_IFDIR)
#define S_ISREG(m) (((m)&_S_IFMT)==_S_IFREG)
#endif
#endif
#ifndef _WIN32
#define _S_IFDIR S_IFDIR
#define _S_IFREG S_IFREG
#endif
static int folder_total=1, index_next, missing_root, missing_romfs, malformed, write_fault, close_fault, listing_fault, nested_base;
static FILE *report_stream;
static int fake_stat(const char *p, struct stat *s) {
    memset(s,0,sizeof(*s));
    if(nested_base && strstr(p,"/exefs") && !strstr(p,"/base/exefs"))return -1;
    if (strstr(p,"/romfs")) {if(missing_romfs)return -1;s->st_mode=_S_IFDIR;return 0;}
    if (!strstr(p,"/exefs/") || !strcmp(p+strlen(p)-6,"/exefs")) {s->st_mode=_S_IFDIR;return 0;}
    if(strstr(p,"/rtld") || strstr(p,"/sdk")) return -1;
    s->st_mode=_S_IFREG; s->st_size=4; return 0;
}
static FILE *fake_fopen(const char *p,const char *m) {
    (void)m;
    FILE *f=tmpfile();
    if (!f) abort();
    if(strstr(p,"report.txt")) {report_stream=f;return f;}
    fputs(malformed?"BAD!":strstr(p,"main.npdm")?"META":"NSO0",f);
    rewind(f); return f;
}
static int fake_fprintf(FILE *f,const char *fmt,...) {
    if(f==report_stream && write_fault)return -1;
    va_list ap; va_start(ap,fmt);int r=vfprintf(f,fmt,ap);va_end(ap);return r;
}
static int fake_fclose(FILE *f) {
    int fail=f==report_stream && close_fault;
    if(f==report_stream)report_stream=NULL;
    int r=fclose(f);return fail?EOF:r;
}
DIR *opendir(const char *p) {(void)p;index_next=0;static DIR d;return missing_root?NULL:&d;}
struct dirent *readdir(DIR *d) {(void)d;static struct dirent e;if(listing_fault && index_next==1){errno=EIO;return NULL;}if(index_next>=folder_total)return NULL;snprintf(e.d_name,sizeof(e.d_name),"Game%03d",folder_total-index_next++);return &e;}
int closedir(DIR *d) {(void)d;return 0;}
#define stat(p,s) fake_stat(p,s)
#define fopen fake_fopen
#define fclose fake_fclose
#define fprintf fake_fprintf
#define main application_main
#include "../app/source/main.c"
#undef main
#undef fprintf
#undef fclose
#undef fopen
#undef stat
static int tests=0,failures=0;
#define CHECK(label,expr) do{tests++;if(!(expr)){failures++;printf("FAIL: %s\n",label);}else printf("PASS: %s\n",label);}while(0)
int main(void) {
    scan();CHECK("scanner recognizes folder",count==1);
    CHECK("signature-only flags are explicitly named",games[0].magic_main && games[0].magic_npdm && games[0].main_size==4);
    CHECK("normal report succeeds",report(&games[0])==1);
    nested_base=1;inspect(&games[0]);CHECK("base layout detected",games[0].uses_base && games[0].has_exefs && games[0].magic_main);nested_base=0;
    malformed=1;inspect(&games[0]);CHECK("wrong magic rejected",!games[0].magic_main && !games[0].magic_npdm);
    malformed=0;missing_romfs=1;inspect(&games[0]);CHECK("missing RomFS recorded",!games[0].has_romfs);
    missing_romfs=0;write_fault=1;CHECK("write failure returns error",report(&games[0])==0 && strstr(status,"Report write failed")!=NULL);
    write_fault=0;close_fault=1;CHECK("close failure returns error",report(&games[0])==0);
    close_fault=0;folder_total=129;scan();CHECK("folder cap is disclosed",count==128 && strstr(status,"omitted 1")!=NULL);
    CHECK("folders sorted",strcmp(games[0].name,games[count-1].name)<0);
    listing_fault=1;scan();CHECK("listing failure disclosed",strstr(status,"results incomplete")!=NULL);
    listing_fault=0;
    missing_root=1;scan();CHECK("missing root resets selection and reports error",count==0 && selection==0 && strstr(status,"Cannot read")!=NULL);
    char short_path[4];CHECK("oversized path rejected",!path_join(short_path,sizeof(short_path),"abcd","ef") && short_path[0]=='\0');
    printf("%d tests, %d failed\n",tests,failures);return failures?1:0;
}
