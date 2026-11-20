#pragma once
#include <stddef.h>
#include <stdint.h>
enum { NX2_OK, NX2_BAD_IMAGE, NX2_BAD_DYNAMIC, NX2_BAD_SYMBOL, NX2_BAD_RELOCATION, NX2_UNSUPPORTED, NX2_MISSING_SYMBOL, NX2_TLS_LAYOUT, NX2_ALREADY_LINKED };
typedef struct {
    unsigned char *image, *guest;
    size_t total;
    uint32_t address[3], file_size[3], memory_size[3];
    uint64_t strings, string_size, symbols, symbol_count;
    uint64_t rela, rela_size, plt, plt_size;
    uint64_t relr, relr_size, hash, gnu_hash;
    uint64_t init, fini, init_array, init_size, fini_array, fini_size;
    uint32_t tls_address, tls_file_size, tls_memory_size, tls_alignment;
    int linked;
} Nx2Module;
typedef struct {const char *name;uintptr_t address;} Nx2External;
typedef struct {uint32_t module, reserved;uint64_t offset;} Nx2TlsArg;
/* Explicit caller policy for a verified image, never a general TLS fallback. */
typedef struct {size_t module;uint64_t offset;} Nx2PreservedTls;
typedef struct {
    Nx2Module *modules;size_t count;
    const Nx2External *external;size_t external_count;
    uintptr_t tls_resolver;
    Nx2TlsArg tls_args[128];size_t tls_used;
    const Nx2PreservedTls *preserved_tls;size_t preserved_tls_count;
} Nx2Group;
typedef struct {uint64_t abs64,global,jump,relative,relr,tls;unsigned tls_unknown,tls_preserved;char missing[160];} Nx2LinkStats;
int nx2_module_inspect(void *image, size_t bytes, Nx2Module *out);
int nx2_module_relocate(Nx2Module *module);
int nx2_module_function(Nx2Module *module, const char *name, uintptr_t *address);
int nx2_group_validate(Nx2Group *group,Nx2LinkStats *stats);
int nx2_group_relocate(Nx2Group *group,Nx2LinkStats *stats);
int nx2_module_callbacks(Nx2Group *group,size_t module,int final,uintptr_t *callbacks,size_t capacity,size_t *count);
const char *nx2_module_error(int code);
