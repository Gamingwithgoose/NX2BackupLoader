/* SPDX-License-Identifier: GPL-2.0-only */
#include "folder.h"
#include "nup.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#define STAT _stat64
#else
#include <dirent.h>
#define STAT stat
#endif
static void (*progress_callback)(uint32_t, void *);
static void *progress_context;
void nx2_folder_progress(void (*fn)(uint32_t, void *), void *ctx) {
  progress_callback = fn;
  progress_context = ctx;
}
typedef int (*Visit)(void *, const char *);
static int list(const char *root, Visit visit, void *ctx) {
#ifdef _WIN32
  char pattern[1600];
  if (snprintf(pattern, sizeof(pattern), "%s/*", root) >= (int)sizeof(pattern))
    return 0;
  WIN32_FIND_DATAA e;
  HANDLE h = FindFirstFileA(pattern, &e);
  if (h == INVALID_HANDLE_VALUE)
    return 0;
  int ok = 1;
  do {
    if (strcmp(e.cFileName, ".") && strcmp(e.cFileName, "..") &&
        !visit(ctx, e.cFileName)) {
      ok = 0;
      break;
    }
  } while (FindNextFileA(h, &e));
  FindClose(h);
  return ok;
#else
  DIR *d = opendir(root);
  if (!d)
    return 0;
  struct dirent *e;
  int ok = 1;
  while ((e = readdir(d)))
    if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..") &&
        !visit(ctx, e->d_name)) {
      ok = 0;
      break;
    }
  closedir(d);
  return ok;
#endif
}
static int type(const char *p, int dir) {
  struct STAT st;
  return !STAT(p, &st) && ((st.st_mode & S_IFMT) == (dir ? S_IFDIR : S_IFREG));
}
static uint32_t u32(const void *p) {
  const unsigned char *s = p;
  return (uint32_t)s[0] | (uint32_t)s[1] << 8 | (uint32_t)s[2] << 16 |
         (uint32_t)s[3] << 24;
}
static uint64_t u64(const void *p) {
  const unsigned char *s = p;
  return u32(s) | (uint64_t)u32(s + 4) << 32;
}
static void p32(void *p, uint32_t v) {
  unsigned char *s = p;
  for (unsigned i = 0; i < 4; i++)
    s[i] = (unsigned char)(v >> (8 * i));
}
static void p64(void *p, uint64_t v) {
  unsigned char *s = p;
  for (unsigned i = 0; i < 8; i++)
    s[i] = (unsigned char)(v >> (8 * i));
}
static int child(char *out, size_t cap, const char *root, const char *name) {
  int n = snprintf(out, cap, "%s/%s", root, name);
  return n >= 0 && (size_t)n < cap;
}
int nx2_folder_modules(const char *root, char names[13][16], unsigned *count) {
  char path[1400];
  unsigned n = 0;
  const char *first[] = {"rtld", "main"};
  for (unsigned i = 0; i < 2; i++) {
    if (!child(path, sizeof(path), root, "exefs") ||
        strlen(path) + 1 + strlen(first[i]) >= sizeof(path))
      return 0;
    strcat(path, "/");
    strcat(path, first[i]);
    if (!type(path, 0))
      return 0;
    strcpy(names[n++], first[i]);
  }
  for (unsigned i = 0; i < 10; i++) {
    char tail[40];
    snprintf(tail, sizeof(tail), "exefs/subsdk%u", i);
    if (!child(path, sizeof(path), root, tail))
      return 0;
    if (type(path, 0))
      snprintf(names[n++], 16, "subsdk%u", i);
  }
  if (!child(path, sizeof(path), root, "exefs/sdk") || !type(path, 0))
    return 0;
  strcpy(names[n++], "sdk");
  *count = n;
  return 1;
}
typedef struct {
  const char *root;
  Nx2Catalog *out;
} CatalogCtx;
static int catalog_entry(void *v, const char *name) {
  CatalogCtx *c = v;
  if (name[0] == '.' || strlen(name) >= 256 || strchr(name, '\\') ||
      strchr(name, ':'))
    return 1;
  char folder[1200], path[1400], modules[13][16];
  unsigned count;
  if (!child(folder, sizeof(folder), c->root, name))
    return 1;
  size_t len = strlen(name);
  if (type(folder, 0) && len > 4 &&
      !strcmp(name + len - 4, ".nup")) {
    uint64_t id = 0, files = 0;
    if (!nx2_nup_probe(folder, &id, &files)) return 1;
    Nx2Game archive = {0};
    memcpy(archive.name, name, len - 4);
    archive.name[len - 4] = 0;
    strcpy(archive.root, folder);
    archive.id = id;
    archive.is_nup = 1;
    if (c->out->count >= 4096) return 0;
    void *items = realloc(c->out->items,
                          (c->out->count + 1) * sizeof(archive));
    if (!items) return 0;
    c->out->items = items;
    c->out->items[c->out->count++] = archive;
    return 1;
  }
  if (!type(folder, 1))
    return 1;
  Nx2Game game = {0};
  strcpy(game.name, name);
  for (unsigned base = 0; base < 2; base++) {
    if (base) {
      if (!child(game.root, sizeof(game.root), folder, "base"))
        continue;
    } else
      strcpy(game.root, folder);
    if (!child(path, sizeof(path), game.root, "romfs") || !type(path, 1) ||
        !nx2_folder_modules(game.root, modules, &count))
      continue;
    if (!child(path, sizeof(path), game.root, "exefs/main.npdm"))
      return 0;
    FILE *f = fopen(path, "rb");
    if (f) {
      unsigned char h[128], aci[32];
      if (fread(h, 1, 128, f) == 128 && !memcmp(h, "META", 4)) {
        game.resource = u32(h + 0x14);
        uint32_t at = u32(h + 0x70);
        if (at >= 128 && at <= 0x100000 && !fseek(f, (long)at, SEEK_SET) &&
            fread(aci, 1, 32, f) == 32 && !memcmp(aci, "ACI0", 4))
          game.id = u64(aci + 16);
      }
      fclose(f);
    }
    child(path, sizeof(path), folder, "updates");
    game.updates = type(path, 1);
    if (c->out->count >= 4096)
      return 0;
    void *items = realloc(c->out->items, (c->out->count + 1) * sizeof(game));
    if (!items)
      return 0;
    c->out->items = items;
    c->out->items[c->out->count++] = game;
    return 1;
  }
  return 1;
}
static int game_compare(const void *a, const void *b) {
  return strcmp(((const Nx2Game *)a)->name, ((const Nx2Game *)b)->name);
}
int nx2_catalog(const char *root, Nx2Catalog *out) {
  *out = (Nx2Catalog){0};
  CatalogCtx c = {root, out};
  if (!list(root, catalog_entry, &c)) {
    nx2_catalog_free(out);
    return 0;
  }
  qsort(out->items, out->count, sizeof(*out->items), game_compare);
  return 1;
}
void nx2_catalog_free(Nx2Catalog *c) {
  free(c->items);
  *c = (Nx2Catalog){0};
}

typedef struct {
  char *path;
  uint64_t size, data;
  uint32_t at, parent, next, dir, file, hash, po;
} Entry;
typedef struct {
  Entry *files, *dirs;
  uint32_t nf, nd, fc, dc;
  const char *root;
  char relative[1024];
  unsigned depth;
  int failed;
} Tree;
static uint64_t align64(uint64_t n, uint64_t a) {
  return (n + a - 1) & ~(a - 1);
}
static int add(Entry **items, uint32_t *count, uint32_t *capacity,
               const char *path, uint64_t bytes) {
  if (*count >= 262144)
    return 0;
  if (*count == *capacity) {
    uint32_t cap = *capacity ? *capacity * 2 : 64;
    void *p = realloc(*items, (size_t)cap * sizeof(Entry));
    if (!p)
      return 0;
    *items = p;
    *capacity = cap;
  }
  char *s = malloc(strlen(path) + 1);
  if (!s)
    return 0;
  strcpy(s, path);
  (*items)[(*count)++] = (Entry){.path = s,
                                 .size = bytes,
                                 .next = UINT32_MAX,
                                 .dir = UINT32_MAX,
                                 .file = UINT32_MAX};
  return 1;
}
static int tree_entry(void *v, const char *name) {
  Tree *t = v;
  if (!strcmp(name, ".listing"))
    return 1;
  if (strchr(name, ':') || strchr(name, '\\'))
    return 0;
  size_t old = strlen(t->relative);
  char rel[1024], full[2400];
  int n =
      snprintf(rel, sizeof(rel), "%s%s%s", t->relative, old ? "/" : "", name);
  if (n < 0 || (size_t)n >= sizeof(rel) ||
      !child(full, sizeof(full), t->root, rel))
    return 0;
  struct STAT st;
  if (STAT(full, &st))
    return 0;
  if ((st.st_mode & S_IFMT) == S_IFDIR) {
    if (t->depth >= 64 || !add(&t->dirs, &t->nd, &t->dc, rel, 0))
      return 0;
    strcpy(t->relative, rel);
    t->depth++;
    int ok = list(full, tree_entry, t);
    t->depth--;
    t->relative[old] = 0;
    return ok;
  }
  if ((st.st_mode & S_IFMT) != S_IFREG || st.st_size < 0)
    return 0;
  int ok = add(&t->files, &t->nf, &t->fc, rel, (uint64_t)st.st_size);
  if (ok && progress_callback && !(t->nf % 1024))
    progress_callback(t->nf, progress_context);
  return ok;
}
static int entry_compare(const void *a, const void *b) {
  return strcmp(((const Entry *)a)->path, ((const Entry *)b)->path);
}
static unsigned depth(const char *s) {
  unsigned n = !!*s;
  for (; *s; s++)
    n += *s == '/';
  return n;
}
static int dir_compare(const void *a, const void *b) {
  const Entry *x = a, *y = b;
  unsigned dx = depth(x->path), dy = depth(y->path);
  return dx < dy ? -1 : dx > dy ? 1 : strcmp(x->path, y->path);
}
static int pointer_compare(const void *a, const void *b) {
  return strcmp((*(Entry *const *)a)->path, (*(Entry *const *)b)->path);
}
static Entry *parent(Entry **index, uint32_t n, const char *path) {
  char s[1024];
  strcpy(s, path);
  char *slash = strrchr(s, '/');
  if (slash)
    *slash = 0;
  else
    s[0] = 0;
  Entry key = {.path = s};
  Entry *p = &key;
  Entry **found = bsearch(&p, index, n, sizeof(*index), pointer_compare);
  return found ? *found : NULL;
}
static uint32_t prime(uint32_t n) {
  if (n < 3)
    n = 3;
  n |= 1;
  for (;; n += 2) {
    uint32_t d;
    for (d = 3; (uint64_t)d * d <= n; d += 2)
      if (n % d == 0)
        break;
    if ((uint64_t)d * d > n)
      return n;
  }
}
static const char *leaf(const char *s) {
  const char *p = strrchr(s, '/');
  return p ? p + 1 : s;
}
static uint32_t name_hash(uint32_t parent, const char *s) {
  uint32_t h = parent ^ 123456789;
  for (; *s; s++) {
    h = (h >> 5) | (h << 27);
    h ^= (unsigned char)*s;
  }
  return h;
}
void *nx2_folder_rom(const char *root, size_t *out_size, char error[160]) {
  Tree t = {.root = root};
  Entry **index = NULL;
  unsigned char *blob = NULL;
  uint32_t *dh = NULL, *fh = NULL;
  *out_size = 0;
  snprintf(error, 160, "Could not scan or allocate the RomFS directory index");
  if (!add(&t.dirs, &t.nd, &t.dc, "", 0) || !list(root, tree_entry, &t) ||
      !t.nf)
    goto done;
  qsort(t.files, t.nf, sizeof(Entry), entry_compare);
  qsort(t.dirs, t.nd, sizeof(Entry), dir_compare);
  index = malloc((size_t)t.nd * sizeof(*index));
  if (!index)
    goto done;
  for (uint32_t i = 0; i < t.nd; i++)
    index[i] = &t.dirs[i];
  qsort(index, t.nd, sizeof(*index), pointer_compare);
  uint64_t dm = 0, fm = 0, strings = 0, data = 0;
  for (uint32_t i = 0; i < t.nd; i++) {
    t.dirs[i].at = (uint32_t)dm;
    dm = align64(dm + 24 + strlen(leaf(t.dirs[i].path)), 4);
  }
  for (uint32_t i = 0; i < t.nf; i++) {
    Entry *e = &t.files[i];
    e->at = (uint32_t)fm;
    fm = align64(fm + 32 + strlen(leaf(e->path)), 4);
    e->po = (uint32_t)strings;
    strings += strlen(e->path) + 1;
    data = align64(data, 16);
    e->data = data;
    if (e->size > INT64_MAX - data)
      goto done;
    data += e->size;
  }
  if (dm > UINT32_MAX || fm > UINT32_MAX || strings > UINT32_MAX)
    goto done;
  uint32_t db = prime(t.nd), fb = prime(t.nf);
  uint64_t dho = 80, dmo = dho + db * 4ULL, fho = dmo + dm,
           fmo = fho + fb * 4ULL, ms = fmo + fm, base = align64(ms, 4096);
  uint64_t so = 80 + t.nf * 32ULL, mo = align64(so + strings, 16),
           bytes = mo + ms;
  if (bytes > 128 * 1024 * 1024 || base > INT64_MAX - data - 4095)
    goto done;
  blob = calloc(1, (size_t)bytes);
  dh = malloc(db * 4ULL);
  fh = malloc(fb * 4ULL);
  if (!blob || !dh || !fh) {
    free(blob);
    blob = NULL;
    goto done;
  }
  memset(dh, 255, db * 4ULL);
  memset(fh, 255, fb * 4ULL);
  for (uint32_t i = t.nd; i > 1; i--) {
    Entry *e = &t.dirs[i - 1], *p = parent(index, t.nd, e->path);
    if (!p) {
      free(blob);
      blob = NULL;
      goto done;
    }
    e->parent = p->at;
    e->next = p->dir;
    p->dir = e->at;
  }
  for (uint32_t i = t.nf; i; i--) {
    Entry *e = &t.files[i - 1], *p = parent(index, t.nd, e->path);
    if (!p) {
      free(blob);
      blob = NULL;
      goto done;
    }
    e->parent = p->at;
    e->next = p->file;
    p->file = e->at;
  }
  for (uint32_t i = 0; i < t.nd; i++) {
    Entry *e = &t.dirs[i];
    const char *name = leaf(e->path);
    uint32_t bucket = name_hash(e->parent, name) % db;
    unsigned char *p = blob + mo + dmo + e->at;
    p32(p, e->parent);
    p32(p + 4, e->next);
    p32(p + 8, e->dir);
    p32(p + 12, e->file);
    p32(p + 16, dh[bucket]);
    p32(p + 20, (uint32_t)strlen(name));
    memcpy(p + 24, name, strlen(name));
    dh[bucket] = e->at;
  }
  for (uint32_t i = 0; i < t.nf; i++) {
    Entry *e = &t.files[i];
    const char *name = leaf(e->path);
    uint32_t bucket = name_hash(e->parent, name) % fb;
    unsigned char *p = blob + mo + fmo + e->at;
    p32(p, e->parent);
    p32(p + 4, e->next);
    p64(p + 8, e->data);
    p64(p + 16, e->size);
    p32(p + 24, fh[bucket]);
    p32(p + 28, (uint32_t)strlen(name));
    memcpy(p + 32, name, strlen(name));
    fh[bucket] = e->at;
    p = blob + 80 + i * 32ULL;
    p64(p, base + e->data);
    p64(p + 8, e->size);
    p32(p + 16, e->po);
    p32(p + 20, (uint32_t)strlen(e->path));
    memcpy(blob + so + e->po, e->path, strlen(e->path));
  }
  for (uint32_t i = 0; i < db; i++)
    p32(blob + mo + dho + i * 4ULL, dh[i]);
  for (uint32_t i = 0; i < fb; i++)
    p32(blob + mo + fho + i * 4ULL, fh[i]);
  uint64_t meta[] = {80,  dho,       db * 4ULL, dmo, dm,
                     fho, fb * 4ULL, fmo,       fm,  base};
  for (unsigned i = 0; i < 10; i++)
    p64(blob + mo + i * 8, meta[i]);
  memcpy(blob, "NX2ROM01", 8);
  p32(blob + 8, 1);
  p32(blob + 12, 80);
  p64(blob + 16, ms);
  p64(blob + 24, align64(base + data, 4096));
  p32(blob + 32, t.nf);
  p32(blob + 36, 32);
  p64(blob + 40, 80);
  p64(blob + 48, so);
  p64(blob + 56, strings);
  p64(blob + 64, mo);
  p64(blob + 72, base);
  *out_size = (size_t)bytes;
  error[0] = 0;
done:
  for (uint32_t i = 0; i < t.nf; i++)
    free(t.files[i].path);
  for (uint32_t i = 0; i < t.nd; i++)
    free(t.dirs[i].path);
  free(t.files);
  free(t.dirs);
  free(index);
  free(dh);
  free(fh);
  return blob;
}
