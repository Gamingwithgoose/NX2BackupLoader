/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
/* AArch64: a 16-byte UserId passed by value occupies two x registers. */
typedef struct {uint64_t lo,hi;} Nx2UserId;
#define NX2_PROFILE_FS_MODE UINT64_C(0x100000000)
#define NX2_PROFILE_PRIMARY_FLAG UINT64_C(1)
#define NX2_PROFILE_MARKER_MAGIC UINT64_C(0x3150525032584e)
static inline int nx2_profile_has_uid(Nx2UserId u){return u.lo||u.hi;}
static inline int nx2_profile_relative(char *dst,size_t cap,Nx2UserId u){
 if(!dst||cap<42||!nx2_profile_has_uid(u))return 0;
 int n=snprintf(dst,cap,"profiles/%016llx%016llx",
               (unsigned long long)u.lo,(unsigned long long)u.hi);
 return n>0&&(size_t)n<cap;
}

/* Native SDK current-application requests may carry the host identity.
 * This only authorizes routing into the selected guest's private SD tree. */
static inline int nx2_profile_application_allowed(uint64_t app,uint64_t guest,uint64_t host){
 return !app || app==guest || (host && app==host);
}
