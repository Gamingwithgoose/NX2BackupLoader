/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "rom_view.h"
#define NX2_RPC_SLOTS 16
#define NX2_PATH_MAX 1024
#define NX2_OBJECTS 96
#define NX2_FS_UNSUPPORTED 0x310002U
#define NX2_INVALID 0x2f5e02U
#define NX2_NOT_FOUND 0x202U
#define NX2_LIMIT 0x2f6a02U

enum {RPC_ROM_READ=1,RPC_SAVE_CREATE,RPC_SAVE_DELETE,RPC_SAVE_MKDIR,RPC_SAVE_RMDIR,RPC_SAVE_RMTREE,RPC_SAVE_CLEAN,RPC_SAVE_RENAME_FILE,RPC_SAVE_RENAME_DIR,RPC_SAVE_TYPE,RPC_SAVE_FREE,RPC_SAVE_TOTAL,RPC_SAVE_OPEN,RPC_SAVE_READ,RPC_SAVE_WRITE,RPC_SAVE_FLUSH,RPC_SAVE_RESIZE,RPC_SAVE_SIZE,RPC_SAVE_CLOSE,RPC_DIR_OPEN,RPC_DIR_READ,RPC_DIR_COUNT,RPC_DIR_CLOSE,RPC_COMMIT,RPC_LOG,RPC_ABORT,RPC_MAIN_ENTERED,RPC_ROM_MOUNT,RPC_SAVE_MOUNT,RPC_IPC,RPC_SAVE_TIMESTAMP,RPC_BREAK,RPC_PHYSICAL_MEMORY};
typedef struct {
 uint32_t state,op,result,reserved;
 uint64_t a,b,c,d,answer;
 void *buffer;
 char path[NX2_PATH_MAX],path2[NX2_PATH_MAX];
} Nx2Request;
typedef struct {uintptr_t vtable;uint64_t active,handle,mode;} Nx2Object;
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
 uint64_t (*original_present)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
 void (*rtld)(uint64_t,uint64_t);
 void (*original_os_init)(uint32_t,const void *);
} Nx2LaunchApi;
typedef struct {
 uint64_t begin,end;uint32_t type,permission;
} Nx2Region;
typedef struct {
 Nx2RomView rom;
 Nx2LaunchApi api;
 Nx2Request requests[NX2_RPC_SLOTS];
 Nx2Object objects[NX2_OBJECTS];
 uintptr_t storage_table[10],fs_table[24],file_table[10],dir_table[6];
 Nx2Object storage,save_fs;
 uint64_t heap_base,heap_capacity,heap_size;
 uint64_t arena_base,arena_capacity,arena_used;
 uint32_t worker_handle,stage,finished,abort_requested;
 uint64_t fsp_denied,rom_reads,save_calls,main_entered,frames;
 uint64_t handle_tags[128];
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
uint32_t nx2_ensure_save(uint64_t *);
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
uint32_t nx2_map_physical(uint64_t,uint64_t);
uint32_t nx2_unmap_physical(uint64_t,uint64_t);
