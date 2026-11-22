/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <stdint.h>
/* close_kind: 0=method, 1=domain-object release, 2=HIPC session close. */
typedef struct {uint32_t command,valid,control,want_fsp,domain,object_id,close_kind;} Nx2IpcDecision;
Nx2IpcDecision nx2_ipc_decide(const void *,uint64_t,uint32_t);
int nx2_ipc_fsp_allowed(Nx2IpcDecision);
/* kind=4 native device; 5 native event; 6 read-only native IStorage. */
int nx2_ipc_fsp_native_child_allowed(Nx2IpcDecision,uint32_t);
int nx2_ipc_save_reader_allowed(Nx2IpcDecision);
int nx2_ipc_service_name(const void *,uint64_t,char out[9]);
int nx2_ipc_reply_result(const void *,uint64_t,uint32_t,uint32_t *);
/* Inspect only bounded HIPC CMIF domain metadata; never use title IDs. */
int nx2_ipc_domain_response_first_object(const void *,uint64_t,uint32_t *);

/* Service-domain lifetime helpers for clone and copy controls. */
int nx2_ipc_control_copy_domain_object(const void *,uint64_t,uint32_t *);
int nx2_ipc_reply_first_moved_handle(const void *,uint64_t,uint32_t *);

/* HIPC type-2 session close completion, including ConnectionClosed. */
int nx2_ipc_session_closed(Nx2IpcDecision,uint32_t);
