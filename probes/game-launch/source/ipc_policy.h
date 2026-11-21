/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <stdint.h>
typedef struct {uint32_t command,valid,control,want_fsp,domain,object_id;} Nx2IpcDecision;
Nx2IpcDecision nx2_ipc_decide(const void *,uint64_t,uint32_t);
int nx2_ipc_fsp_allowed(Nx2IpcDecision);
int nx2_ipc_save_reader_allowed(Nx2IpcDecision);
int nx2_ipc_service_name(const void *,uint64_t,char out[9]);
int nx2_ipc_reply_result(const void *,uint64_t,uint32_t,uint32_t *);
/* Inspect only bounded HIPC CMIF domain metadata; never use title IDs. */
int nx2_ipc_domain_response_first_object(const void *,uint64_t,uint32_t *);
