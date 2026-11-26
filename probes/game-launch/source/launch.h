/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <stdint.h>
#include "profile_save.h"
#include <stddef.h>
#include "rom_view.h"
#define NX2_RPC_SLOTS 16
#define NX2_LOG_SLOTS 32
#define NX2_RPC_TRACE_SLOTS 32
#define NX2_PATH_MAX 1024
#define NX2_OBJECTS 512
#define NX2_FS_UNSUPPORTED 0x310002U
#define NX2_INVALID 0x2f5e02U
#define NX2_NOT_FOUND 0x202U
#define NX2_LIMIT 0x2f6a02U

enum {RPC_ROM_READ=1,RPC_SAVE_CREATE,RPC_SAVE_DELETE,RPC_SAVE_MKDIR,RPC_SAVE_RMDIR,RPC_SAVE_RMTREE,RPC_SAVE_CLEAN,RPC_SAVE_RENAME_FILE,RPC_SAVE_RENAME_DIR,RPC_SAVE_TYPE,RPC_SAVE_FREE,RPC_SAVE_TOTAL,RPC_SAVE_OPEN,RPC_SAVE_READ,RPC_SAVE_WRITE,RPC_SAVE_FLUSH,RPC_SAVE_RESIZE,RPC_SAVE_SIZE,RPC_SAVE_CLOSE,RPC_DIR_OPEN,RPC_DIR_READ,RPC_DIR_COUNT,RPC_DIR_CLOSE,RPC_COMMIT,RPC_LOG,RPC_ABORT,RPC_MAIN_ENTERED,RPC_ROM_MOUNT,RPC_SAVE_MOUNT,RPC_IPC,RPC_SAVE_TIMESTAMP,RPC_BREAK,RPC_PHYSICAL_MEMORY,RPC_CACHE_CREATE,RPC_CACHE_CHECK,RPC_VFS_PREPARE_ROOT,RPC_SAVE_ARCHIVE_EXISTS,RPC_SAVE_ARCHIVE_CREATE,RPC_CACHE_ENUM,RPC_SAVE_OPERATE_RANGE,RPC_CACHE_QUERY_SIZE,RPC_CACHE_PROVISION,RPC_SAVE_PROFILE_BIND,RPC_SAVE_PROFILE_EXISTS,RPC_SAVE_PROFILE_CREATE,RPC_SAVE_ATTRIBUTE,RPC_SAVE_PROFILE_ROOT};
typedef struct {
 uint32_t state,op,result,reserved;
 /* Published before state=2; observed by watchdog without altering RPC. */
 uint64_t diagnostic_published_tick,diagnostic_owner_tls;
 uint32_t diagnostic_ticket,diagnostic_reserved;
 uint64_t a,b,c,d,answer;
 void *buffer;
 char path[NX2_PATH_MAX],path2[NX2_PATH_MAX];
} Nx2Request;
/* Best-effort diagnostics and completed-mount notices never borrow an I/O slot.
 * Slots own their strings and values; no guest pointers outlive the call. */
typedef struct {
 uint32_t state,op; /* only RPC_LOG or RPC_SAVE_MOUNT notifications */
 uint64_t a,b,c,d;
 char path[NX2_PATH_MAX],path2[NX2_PATH_MAX];
} Nx2LogRecord;
/* Append-only ring. sequence is published last, so the host can reject
 * incomplete entries without relying on the faulted thread. */
typedef struct {
 uint64_t caller,thread;
 uint32_t op,phase,result,sequence;
} Nx2RpcTrace;
typedef struct {uintptr_t vtable;uint64_t active,handle,mode,profile_high;} Nx2Object;
typedef struct {
 void (*os_init)(uint32_t,const void *);
 void (*set_fs_allocator)(void *(*)(uint64_t),void (*)(void *,uint64_t));
 void (*fs_init)(void);
 uint32_t (*mount_rom)(const char *,void *,void *,uint64_t);
 uint32_t (*query_cache)(uint64_t *,void *);
 uint32_t (*register_fs)(const char *,void *);
 uint32_t (*open_file)(uint64_t *,const char *,int);
 uint32_t (*read_file)(uint64_t *,uint64_t,int64_t,void *,uint64_t,const void *);
 uint32_t (*write_file)(uint64_t,int64_t,const void *,uint64_t,const void *);
 uint32_t (*get_size)(int64_t *,uint64_t);
 void (*close_file)(uint64_t);
 void (*unmount)(const char *);
 uint32_t (*create_file)(const char *,int64_t);
 uint32_t (*delete_file)(const char *);
 const char *(*path_string)(const void *);
 void (*set_fs_result_handler)(uint32_t (*)(uint32_t));
 uintptr_t storage_rtti,fs_rtti,file_rtti,dir_rtti;
 void (*original_main)(void);
 void (*original_nninit)(void);
 void (*original_sdk_init)(void);
 uintptr_t (*original_bootstrap)(const char *);
 uintptr_t (*original_getproc)(void *,const char *);
 /* NVNqueuePresentTexture has a void return and three arguments. */
 void (*original_present)(void *queue,void *window,int texture_index);
 void (*rtld)(uint64_t,uint64_t);
 void (*original_os_init)(uint32_t,const void *);
 /* Optional SDK service resolver observer; always forwards the real result. */
 uint32_t (*original_connect_hipc)(uint32_t *,const char *);
 uint32_t (*original_device_initialize)(void *,const void *);
 uint32_t (*original_queue_initialize)(void *,const void *);
 uint32_t (*original_window_initialize)(void *,const void *);
 void (*original_queue_submit)(void *,int,const uint64_t *);
 void (*original_queue_flush)(void *);
 void (*original_queue_finish)(void *);
 uint32_t (*original_window_acquire)(void *,void *,int *);
 uint32_t (*original_queue_acquire)(void *,void *,int *);
 void (*original_queue_fence)(void *,void *,int,int);
 void (*original_queue_wait)(void *,const void *);
 int (*original_sync_wait)(const void *,uint64_t);
 int (*original_queue_error)(void *,void *);
 uint8_t (*original_pool_init)(void *,const void *);
 void (*original_pool_finalize)(void *);
 void (*original_pool_storage)(void *,void *,size_t);
 void (*original_pool_flags)(void *,int);
 uint64_t (*original_pool_address)(const void *);
 void (*original_cmd_begin)(void *);
 uint64_t (*original_cmd_end)(void *);
 void (*original_cmd_memory)(void *,const void *,ptrdiff_t,size_t);
 void (*original_control_memory)(void *,void *,size_t);
 uint8_t (*original_texture_init)(void *,const void *);
 void (*original_texture_finalize)(void *);
 uint8_t (*original_sync_init)(void *,void *);
 void (*original_sync_finalize)(void *);
 uint8_t (*original_pool_map_virtual)(void *,int,const void *);
} Nx2LaunchApi;
typedef struct {
 uint64_t begin,end;uint32_t type,permission;
} Nx2Region;
/* Guest IPC SVC breadcrumbs: written without RPC, read by host watchdog.
 * A bounded ring is diagnostic only; never changes IPC routing/results. */
#define NX2_IPC_TRACE_SLOTS 32
#define NX2_GRAPHICS_TRACE_SLOTS 32
/* Experimental all-service IPC metadata trace; 0 bytes of guest payload. */
#define NX2_IPC_EVENT_SLOTS 16384U
#define NX2_NVN_STAGE_COUNT 5
#define NX2_GPU_BOUNDARY_RING_SLOTS 2048U
/* A snapshot is a last-N timeline, not a GPU/CPU stack trace. */
typedef struct {
 uint32_t sequence,kind;
 uint64_t tick,thread_tls,caller;
 uint64_t queue,window;
 int32_t texture_index;
 uint32_t reserved;
 uint64_t detail;
} Nx2GpuBoundary;
#define NX2_SHARED_TRACE_SLOTS 4096U
typedef struct {
 uint32_t sequence,operation,phase,handle,permission,result,id;
 uint64_t tick,tls,caller,address,bytes,owner;
} Nx2SharedEvent;
#define NX2_SYNC_FLIGHT_SLOTS 128U
/* Observe only the outer native sync IPC call. Entry/return snapshots
 * cannot identify kernel wait reason without additional OS support. */
typedef struct {
 uint64_t thread_tls,caller,started_tick;
 uint32_t handle,request_type,header0,header1;
 uint32_t call_id,phase,result,collision;
} Nx2SyncFlight;

/* NVN stages: 0 submit, 1 flush, 2 finish, 3 window acquire, 4 queue acquire. */
typedef struct {
 uint64_t handle,caller;
 uint32_t request_type,command,result,phase;
 uint32_t service_result;
 char service[9];
} Nx2IpcBreadcrumb;
typedef struct {
 uint64_t caller,tick;
 uint32_t sequence; /* monotonic index+1, published last */
 uint32_t call_id,phase,handle,kind,request_type;
 uint32_t command,object_id,flags,hdr0,hdr1,special;
 uint32_t kernel_result,cmif_result;
 char service[9];
} Nx2IpcEvent;
typedef struct {
 Nx2RomView rom;
 Nx2LaunchApi api;
 Nx2Request requests[NX2_RPC_SLOTS];
 Nx2LogRecord logs[NX2_LOG_SLOTS];
 uint32_t log_enqueued,log_drained,log_dropped,log_next;
 uint32_t rpc_trace_next;
 Nx2RpcTrace rpc_trace[NX2_RPC_TRACE_SLOTS];
 Nx2Object objects[NX2_OBJECTS];
 uintptr_t storage_table[10],fs_table[24],file_table[10],dir_table[6];
 Nx2Object storage,save_fs;
 uint64_t heap_base,heap_capacity,heap_size;
 /* Logical heap layout can include an unmapped virtual reservation.
  * Never used as permission to fabricate physical allocation success. */
 uint64_t guest_virtual_reserve,guest_logical_budget,guest_program_bytes,physical_floor;
 uint64_t guest_program_id,host_program_id;
 uint64_t arena_base,arena_capacity,arena_used;
 uint32_t worker_handle,stage,finished,abort_requested;
 uint32_t late_trace_active; /* diagnostic only; armed after real SDK registration */
 uint32_t sdk_save_open_count; /* native IPC open, NOT proof of SDK registration */
 /* Guest-side C++ filesystem callback entry sites (before RPC helpers). */
 uint32_t fs_guest_read_entries,fs_guest_read_returns;
 uint64_t fs_guest_read_caller,fs_guest_read_offset,fs_guest_read_size;
 uint32_t fs_guest_read_result;
 uint32_t save_gate_phase,save_gate_result;
 uint32_t save_notice_enqueued,save_notice_served,save_notice_dropped;
 uint64_t save_gate_caller;
 uint32_t guest_main_returned;
 uint64_t fsp_denied,rom_reads,save_calls,main_entered,frames;
 uint64_t handle_tags[2048];
 /* NVN mode: 0=diagnostic; 1=native pointers except PresentTexture gate;
  * 2=all native pointers, per-function/frame counters unobserved. */
 uint32_t nvn_driver_passthrough;
 uint32_t nvn_bootstrap_calls,nvn_getproc_calls,nvn_present_lookups;
 uint32_t nvn_present_entered,nvn_present_returned;
 uint32_t nvn_device_entered,nvn_device_returned,nvn_device_result;
 uint32_t nvn_queue_entered,nvn_queue_returned,nvn_queue_result;
 uint32_t nvn_window_entered,nvn_window_returned,nvn_window_result;
 uint32_t nvn_stage_entered[NX2_NVN_STAGE_COUNT];
 uint32_t nvn_stage_returned[NX2_NVN_STAGE_COUNT];
 uint32_t nvn_stage_result[NX2_NVN_STAGE_COUNT];
 /* Optional gameplay GPU tracing, sampled by host watchdog. */
 uint32_t nvn_gameplay_trace;
 uint64_t nvn_stage_last_caller[NX2_NVN_STAGE_COUNT];
 uint64_t nvn_stage_last_tls[NX2_NVN_STAGE_COUNT];
 uint64_t nvn_stage_last_tick[NX2_NVN_STAGE_COUNT];
 uint64_t nvn_stage_return_tick[NX2_NVN_STAGE_COUNT];
 uint32_t shared_trace_next,shared_trace_id;
 Nx2SharedEvent shared_trace[NX2_SHARED_TRACE_SLOTS];
 uint32_t gpu_boundary_next;
 Nx2GpuBoundary gpu_boundary[NX2_GPU_BOUNDARY_RING_SLOTS];
 uint32_t ipc_trace_next,ipc_trace_begins,ipc_trace_ends;
 Nx2IpcBreadcrumb ipc_breadcrumbs[NX2_IPC_TRACE_SLOTS];
 uint32_t ipc_call_next,ipc_event_next;
 uint32_t sync_flight_collisions;
 Nx2SyncFlight sync_flights[NX2_SYNC_FLIGHT_SLOTS];
 Nx2IpcEvent ipc_events[NX2_IPC_EVENT_SLOTS];
 /* Native FSP router stage published in shared RAM without a blocking
  * guest-to-host logging RPC from intercepted synchronous IPC.
  * Stage: 1 entered, 2 decoded, 3 namespace, 4 virtual object,
  * 5 response serialized, 6 unsupported/rejected. */
 uint32_t fsp_route_stage,fsp_route_command,fsp_route_object;
 uint32_t fsp_route_result,fsp_route_child;
 uint32_t graphics_ipc_next;
 Nx2IpcBreadcrumb graphics_ipc[NX2_GRAPHICS_TRACE_SLOTS];
 Nx2Region regions[39];uint32_t region_count;
 uint32_t preflight,preflight_result;
 uint64_t cache_size;
 uint32_t sample_results[3];uint64_t sample_reads[3];unsigned char samples[3][4096];
 uint32_t save_result;uint64_t save_cookie;
} Nx2Launch;
extern Nx2Launch *nx2_launch;
void nx2_bridge_init(Nx2Launch *);
void nx2_preflight_worker(Nx2Launch *);
void nx2_game_worker(Nx2Launch *);
uint32_t nx2_rpc(uint32_t,uint64_t,uint64_t,uint64_t,uint64_t,void *,const char *,const char *,uint64_t *);
uint32_t nx2_virtual_info(uint64_t *,uint32_t,uint32_t,uint64_t);
uint32_t nx2_virtual_heap(void **,uint64_t);
uint32_t nx2_rtld_query(void *,uint32_t *,uint64_t);
uint32_t nx2_connect(uint32_t *,const char *);
uint32_t nx2_sync(uint32_t);
uint32_t nx2_sync_buffer(void *,uint64_t,uint32_t);
uint32_t nx2_close_handle(uint32_t);
uint32_t nx2_map_shared(uint32_t,uint64_t,uint64_t,uint32_t);
uint32_t nx2_unmap_shared(uint32_t,uint64_t,uint64_t);
void nx2_break(uint32_t,uint64_t,uint64_t);
void nx2_debug(const char *,uint64_t);
void nx2_main_gate(void);
uint32_t nx2_mount_rom(const char *,void *,uint64_t);
uint32_t nx2_mount_rom_default(const char *);
uint32_t nx2_query_rom(uint64_t *);
uint32_t nx2_mount_rom_app(const char *,uint64_t,void *,uint64_t);
uint32_t nx2_mount_rom_index(const char *,int,void *,uint64_t);
uint32_t nx2_query_rom_app(uint64_t *,uint64_t);
uint32_t nx2_query_rom_index(uint64_t *,int);
uint32_t nx2_mount_save(const char *);
uint32_t nx2_mount_save_user_ref(const char *,const Nx2UserId *);
uint32_t nx2_mount_save_user_value(const char *,Nx2UserId);
uint32_t nx2_mount_save_app_user_ref(const char *,uint64_t,const Nx2UserId *);
uint32_t nx2_mount_save_app_user_value(const char *,uint64_t,Nx2UserId);
uint32_t nx2_mount_save_detail_user(const char *,uint32_t,uint64_t,Nx2UserId,uint32_t,uint32_t,uint32_t);
uint32_t nx2_save_exists_user_ref(const Nx2UserId *);
uint32_t nx2_provision_save_user_ref(const Nx2UserId *);
uint32_t nx2_provision_save_user_value(uint64_t,Nx2UserId);
uint32_t nx2_ensure_save_user(uint64_t *,uint64_t,const void *,const Nx2UserId *);
/* Per-title cache storage is backed by isolated SD directories, not by
 * the host application's native cache quotas. No fake success. */
uint32_t nx2_create_cache_storage(int,int64_t,int64_t);
uint32_t nx2_ensure_guest_cache_storage(int);
uint32_t nx2_mount_cache_storage(const char *);
uint32_t nx2_mount_cache_storage_index(const char *,int);
uint32_t nx2_mount_cache_storage_app(const char *,uint64_t);
uint32_t nx2_mount_cache_storage_app_index(const char *,uint64_t,int);
/* Kirby's native ldr:ro session cannot be assumed to belong to the guest. */
uint32_t nx2_ro_initialize_compat(void);
uint32_t nx2_ensure_save(uint64_t *);
uint32_t nx2_provision_save(void);
uint32_t nx2_save_exists(void);
uint32_t nx2_save_ok(void);

void nx2_nninit_gate(void);
void nx2_sdk_init_gate(void);
uintptr_t nx2_bootstrap_gate(const char *);
uint32_t nx2_async(uint32_t *,void *,uint64_t,uint32_t);
void nx2_exit_gate(void);
uint32_t nx2_false(void);

uint32_t nx2_save_sizes(int64_t *,int64_t *);
uint32_t nx2_save_available(int64_t *);
uint32_t nx2_save_journal(int64_t *);
uint32_t nx2_save_flags(uint32_t *);

uint32_t nx2_true(void);

void nx2_os_init_gate(uint32_t,const void *);
uint32_t nx2_create_thread(uint32_t *,uintptr_t,uintptr_t,uintptr_t,int32_t,int32_t);
uint32_t nx2_start_thread(uint32_t);
uint32_t nx2_wait_sync(int32_t *,const uint32_t *,int32_t,int64_t);
uint32_t nx2_arbitrate_lock(uint32_t,uintptr_t,uint32_t);
uint32_t nx2_arbitrate_unlock(uintptr_t);
uint32_t nx2_wait_key(uintptr_t,uintptr_t,uint32_t,int64_t);
void nx2_signal_key(uintptr_t,int32_t);
uint32_t nx2_set_memory_permission(uintptr_t,uint64_t,uint32_t);
void nx2_exit_thread_trace(void);
uint32_t nx2_map_physical(uint64_t,uint64_t);
uint32_t nx2_unmap_physical(uint64_t,uint64_t);
