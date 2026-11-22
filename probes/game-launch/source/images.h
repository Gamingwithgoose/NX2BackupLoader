/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <switch.h>
#include <kaihen.h>
#include <stdio.h>
#include "module.h"
#include "launch.h"
typedef struct {void *backing;size_t image,bss;uint64_t mapped;} Nx2Loaded;
typedef struct {Nx2Loaded images[13];Nx2Module modules[13];Service ro;unsigned count;unsigned ro_rollovers;struct {uint64_t field,module,offset;} deferred[16];unsigned deferred_count;} Nx2Images;
int nx2_images_load(FILE *,const char *,Nx2Images *,Nx2Launch *,int);
void nx2_images_free(FILE *,Nx2Images *);
