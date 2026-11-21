/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <stdint.h>
typedef struct {uint32_t command,valid,control,want_fsp;} Nx2IpcDecision;
Nx2IpcDecision nx2_ipc_decide(const void *,uint64_t,uint32_t);
int nx2_ipc_fsp_allowed(Nx2IpcDecision);
int nx2_ipc_save_reader_allowed(Nx2IpcDecision);
int nx2_ipc_service_name(const void *,uint64_t,char out[9]);
int nx2_ipc_reply_result(const void *,uint64_t,uint32_t,uint32_t *);
