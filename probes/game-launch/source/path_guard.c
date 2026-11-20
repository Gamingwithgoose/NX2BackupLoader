/* SPDX-License-Identifier: GPL-2.0-only */
#include "path_guard.h"
#include <string.h>
int nx2_save_path(char *out,size_t cap,const char *root,const char *input){
 if(!out||!root||!input)return 0;
 size_t n=strlen(root);if(n+2>cap||input[0]=='\\')return 0;memcpy(out,root,n);const char *p=input;if(*p=='/')p++;if(*p){out[n++]='/';size_t start=n;for(;*p;p++){
  if(n+1>=cap||*p=='\\'||*p==':'||(unsigned char)*p<32)return 0;
  if(*p=='/'){size_t len=n-start;if(!len||(len==1&&out[start]=='.')||(len==2&&out[start]=='.'&&out[start+1]=='.'))return 0;start=n+1;}
  out[n++]=*p;
 }size_t len=n-start;if(!len||(len==1&&out[start]=='.')||(len==2&&out[start]=='.'&&out[start+1]=='.'))return 0;}
 out[n]=0;return n<0x301;
}
