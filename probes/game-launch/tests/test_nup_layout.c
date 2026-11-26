/* SPDX-License-Identifier: GPL-2.0-only */
#include "../source/nup_layout.h"
#include <assert.h>
#include <stddef.h>
int main(void){
 uint64_t dirs=2,files=300000,f=112+24*dirs,c=f+80*files;
 uint64_t pool=c+4*(dirs+files),names=9*(dirs+files),bytes=pool+names,fa=0,ca=0;
 assert(nx2_nup_layout(bytes,1,dirs,files,pool,names,&fa,&ca));
 assert(fa==f&&ca==c); /* A legitimate >262144-file archive. */
 assert(!nx2_nup_layout(111,1,dirs,files,pool,names,NULL,NULL));
 assert(!nx2_nup_layout(bytes,1,UINT64_MAX,files,pool,names,NULL,NULL));
 assert(!nx2_nup_layout(bytes,1,dirs,UINT64_MAX,pool,names,NULL,NULL));
 assert(!nx2_nup_layout(bytes,1,dirs,files,c-1,names,NULL,NULL));
 assert(!nx2_nup_layout(bytes,1,dirs,files,pool+1,names-1,NULL,NULL));
 assert(!nx2_nup_layout(bytes,1,dirs,files,pool,names+1,NULL,NULL));
 assert(!nx2_nup_layout(128U*1024*1024+1,1,dirs,files,pool,names,NULL,NULL));
 assert(!nx2_nup_layout(bytes,0,dirs,files,pool,names,NULL,NULL));
 return 0;
}
