#pragma once
#include <stddef.h>
#include <stdint.h>
enum { NX2_OK, NX2_BAD_IMAGE, NX2_BAD_DYNAMIC, NX2_BAD_SYMBOL, NX2_BAD_RELOCATION, NX2_UNSUPPORTED, NX2_MISSING_SYMBOL };
typedef struct {
    unsigned char *image, *guest;
    size_t total;
    uint32_t address[3], file_size[3], memory_size[3];
    uint64_t strings, string_size, symbols, symbol_count;
    uint64_t rela, rela_size, plt, plt_size;
} Nx2Module;
int nx2_module_inspect(void *image, size_t bytes, Nx2Module *out);
int nx2_module_relocate(Nx2Module *module);
int nx2_module_function(Nx2Module *module, const char *name, uintptr_t *address);
const char *nx2_module_error(int code);
