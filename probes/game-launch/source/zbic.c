/* SPDX-License-Identifier: GPL-2.0-only
 * Decoder: Atmosphere's Zstandard fork, retained with vendor/NOTICE and COPYING.
 */
#define ZSTD_ZBIC_SUPPORT 1
#define ZSTD_DISABLE_ASM 1
#define ZSTD_NO_INTRINSICS 1
#define XXH_VECTOR 0
#define NO_PREFETCH 1
#define __EMSCRIPTEN__ 1
/* The single decoder serves standard NUP Zstandard and Nintendo ZBIC.
 * NSO modules are prepared before native game execution; NUP file reads
 * happen outside that preparation phase, so these modes do not overlap. */
int nx2_zstd_zbic_mode = 0;
#include "../vendor/zstd.inc"
#include "nso.h"
int nx2_zbic_decode(void *output,size_t bytes,const void *input,size_t packed){
    nx2_zstd_zbic_mode = 1;
    size_t result=ZSTD_decompress(output,bytes,input,packed);
    nx2_zstd_zbic_mode = 0;
    return !ZSTD_isError(result)&&result==bytes;
}
