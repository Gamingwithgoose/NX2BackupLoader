/* SPDX-License-Identifier: GPL-2.0-only */
/* cc -std=c11 -Wall -Wextra -Werror -I../source test_profile_save.c -o test_profile_save */
#include "profile_save.h"
#include <stdio.h>
#include <string.h>
static int test(int v,const char*s){if(!v)fprintf(stderr,"FAIL %s\n",s);return !v;}
int main(void){
 int fails=0;char a[80],b[80];
 Nx2UserId one={0x1122334455667788ULL,0xaabbccddeeff0011ULL};
 Nx2UserId two={0x1122334455667788ULL,0xaabbccddeeff0012ULL};
 fails+=test(nx2_profile_relative(a,sizeof(a),one)&&
       !strcmp(a,"profiles/1122334455667788aabbccddeeff0011"),"deterministic UID");
 fails+=test(nx2_profile_relative(b,sizeof(b),two)&&strcmp(a,b),"profile separation");
 fails+=test(!nx2_profile_relative(a,20,one),"truncation refused");
 fails+=test(!nx2_profile_relative(a,sizeof(a),(Nx2UserId){0,0}),"no fake identity");
 fails+=test(NX2_PROFILE_FS_MODE>65536,"cache modes disjoint");
 puts(fails?"Profile namespace: FAIL":"Profile namespace: PASS");return !!fails;
}
