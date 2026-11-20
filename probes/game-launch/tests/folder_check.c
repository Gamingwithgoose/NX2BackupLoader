/* SPDX-License-Identifier: GPL-2.0-only */
#include "../source/folder.h"
#include "../source/rom_view.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv) {
  if (argc == 3 && !strcmp(argv[1], "--catalog")) {
    Nx2Catalog c;
    if (!nx2_catalog(argv[2], &c))
      return 1;
    for (size_t i = 0; i < c.count; i++)
      printf("%s\t%s\t%016llx\n", c.items[i].name, c.items[i].root,
             (unsigned long long)c.items[i].id);
    nx2_catalog_free(&c);
    return 0;
  }
  if (argc != 3)
    return 2;
  char error[160];
  size_t size;
  void *map = nx2_folder_rom(argv[1], &size, error);
  if (!map) {
    fprintf(stderr, "%s\n", error);
    return 1;
  }
  Nx2RomView v;
  if (!nx2_rom_view(map, size, &v)) {
    free(map);
    return 1;
  }
  FILE *out = fopen(argv[2], "wb");
  if (!out) {
    free(map);
    return 1;
  }
  int ok = fwrite(map, 1, size, out) == size;
  fclose(out);
  printf("files=%u metadata=%llu bytes=%llu\n", v.count,
         (unsigned long long)v.metadata_size, (unsigned long long)size);
  free(map);
  return ok ? 0 : 1;
}
