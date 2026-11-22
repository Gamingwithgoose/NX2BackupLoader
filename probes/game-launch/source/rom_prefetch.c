/* SPDX-License-Identifier: GPL-2.0-only */
#include "rom_prefetch.h"

/* One MiB speculative reads for a 256 KiB request were multiplying SD work
 * when the next accesses jumped around a very large asset. Only prefetch
 * after two confirmed adjacent requests, and at most one extra request. */
uint64_t nx2_rom_prefetch_bytes(uint64_t requested,uint64_t remaining,
                                unsigned sequential_streak){
 if(sequential_streak<2 || requested<UINT64_C(0x10000) ||
    requested>UINT64_C(0x40000) || remaining<=requested)
  return 0;
 uint64_t amount=requested*2;
 if(amount>UINT64_C(0x80000))amount=UINT64_C(0x80000);
 if(amount>remaining)amount=remaining;
 return amount>requested?amount:0;
}
