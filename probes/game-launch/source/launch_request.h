/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include "folder.h"
#define NX2_GUI_PATH "sdmc:/switch/BackupLoaderMKW/BackupLoaderMKW.nro"
#define NX2_CORE_PATH "sdmc:/switch/BackupLoaderMKW/runtime/BackupLoaderMKW-core.nro"
#define NX2_REQUEST_PATH "sdmc:/switch/BackupLoaderMKW/launch-request.bin"
/* Both programs are packaged together. No pointers cross the NRO boundary. */
typedef struct {
 uint64_t host,system_resource,pid;
 uint32_t query_result,version;
 uint64_t user_lo,user_hi;
} Nx2LaunchOrigin;
extern Nx2LaunchOrigin nx2_launch_origin;
int nx2_launch_request_write(const Nx2Game *,int detailed);
int nx2_launch_request_read(Nx2Game *,int *detailed);
