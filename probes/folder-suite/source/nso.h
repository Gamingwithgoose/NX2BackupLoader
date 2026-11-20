#pragma once
#include <stddef.h>
typedef void (*Nx2Sha256)(unsigned char output[32],const void *data,size_t bytes);
void *nx2_pages(size_t bytes);
void nx2_free_pages(void *memory);
int nx2_prepare_nso(const char *path,const char *expected_sha,Nx2Sha256 hash,void **image,size_t *image_size,size_t *bss_size,char error[160]);
int nx2_zbic_decode(void *output,size_t bytes,const void *input,size_t packed);
