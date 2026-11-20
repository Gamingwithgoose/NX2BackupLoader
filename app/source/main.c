#include <switch.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>

#define APP_VERSION "0.3.0"
#define GAME_ROOT "sdmc:/games"
#define MAX_GAMES 128
#define NAME_SIZE 256
#define PATH_SIZE 512

typedef struct {
    char name[NAME_SIZE];
    unsigned has_exefs, has_romfs, has_npdm;
    unsigned has_main, has_rtld, has_sdk;
    unsigned magic_npdm, magic_main, magic_rtld, magic_sdk;
    unsigned path_error;
    unsigned uses_base;
    unsigned long long main_size;
} Game;

static Game games[MAX_GAMES];
static int count = 0;
static int selection = 0;
static char status[180] = "Press A to inspect a folder (read only).";

static int is_directory(const char *p) {
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}
static int is_file(const char *p) {
    struct stat st;
    return stat(p, &st) == 0 && S_ISREG(st.st_mode);
}
static int path_join(char *dst, size_t cap, const char *parent, const char *leaf) {
    size_t a = strlen(parent), b = strlen(leaf);
    if (a >= cap || cap - a < 2 || b > cap - a - 2) {
        if (cap) dst[0] = '\0';
        return 0;
    }
    memcpy(dst, parent, a);
    dst[a] = '/';
    memcpy(dst + a + 1, leaf, b + 1);
    return 1;
}
static int has_magic(const char *path, const char *magic) {
    unsigned char header[4];
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    size_t n = fread(header, 1, sizeof(header), f);
    fclose(f);
    return n == sizeof(header) && memcmp(header, magic, 4) == 0;
}
static void inspect(Game *g) {
    char dir[PATH_SIZE], p[PATH_SIZE], exefs[PATH_SIZE];
    struct stat st;
    g->path_error = 0;
    g->uses_base = 0;
#define JOIN(dst, parent, leaf) do { \
    if (!path_join(dst, sizeof(dst), parent, leaf)) { g->path_error = 1; return; } \
} while (0)
    JOIN(dir, GAME_ROOT, g->name);
    JOIN(exefs, dir, "exefs");
    g->has_exefs = is_directory(exefs);
    if (!g->has_exefs) {
        char base[PATH_SIZE];
        JOIN(base, dir, "base");
        JOIN(exefs, base, "exefs");
        if (is_directory(exefs)) {
            memcpy(dir, base, strlen(base) + 1);
            g->uses_base = 1;
            g->has_exefs = 1;
        } else {
            JOIN(exefs, dir, "exefs");
        }
    }
    JOIN(p, dir, "romfs");
    g->has_romfs = is_directory(p);
    JOIN(p, exefs, "main.npdm");
    g->has_npdm = is_file(p);
    g->magic_npdm = g->has_npdm && has_magic(p, "META");
    JOIN(p, exefs, "main");
    g->has_main = is_file(p);
    g->magic_main = g->has_main && has_magic(p, "NSO0");
    g->main_size = (g->has_main && stat(p, &st) == 0) ? (unsigned long long)st.st_size : 0;
    JOIN(p, exefs, "rtld");
    g->has_rtld = is_file(p);
    g->magic_rtld = g->has_rtld && has_magic(p, "NSO0");
    JOIN(p, exefs, "sdk");
    g->has_sdk = is_file(p);
    g->magic_sdk = g->has_sdk && has_magic(p, "NSO0");
#undef JOIN
}
static int compare_games(const void *a, const void *b) {
    return strcmp(((const Game *)a)->name, ((const Game *)b)->name);
}
static void scan(void) {
    count = 0;
    selection = 0;
    DIR *root = opendir(GAME_ROOT);
    if (!root) {
        snprintf(status, sizeof(status), "Cannot read %s (create this folder).", GAME_ROOT);
        return;
    }
    struct dirent *entry;
    unsigned omitted = 0;
    int read_error = 0;
    for (;;) {
        errno = 0;
        entry = readdir(root);
        if (!entry) { read_error = errno; break; }
        const char *name = entry->d_name;
        if (!strcmp(name, ".") || !strcmp(name, "..")) continue;
        if (strlen(name) >= NAME_SIZE) { ++omitted; continue; }
        char p[PATH_SIZE];
        if (!path_join(p, sizeof(p), GAME_ROOT, name)) { ++omitted; continue; }
        if (!is_directory(p)) continue;
        if (count == MAX_GAMES) { ++omitted; continue; }
        Game *g = &games[count++];
        memset(g, 0, sizeof(*g));
        snprintf(g->name, sizeof(g->name), "%s", name);
        inspect(g);
    }
    if (closedir(root) != 0) read_error = errno ? errno : EIO;
    qsort(games, count, sizeof(games[0]), compare_games);
    snprintf(status, sizeof(status), "Found %d folder(s); omitted %u.%s A writes report.",
             count, omitted, read_error ? " SD listing error; results incomplete." : "");
}
static int report(const Game *g) {
    FILE *f = fopen("sdmc:/nx2backuploader-report.txt", "a");
    if (!f) {
        snprintf(status, sizeof(status), "Could not write SD report; check storage.");
        return 0;
    }
    int failed = 0;
#define WRITE(...) do { if (fprintf(f, __VA_ARGS__) < 0) failed = 1; } while (0)
    WRITE("\n=== NX2BackupLoader diagnostic " APP_VERSION " ===\n");
    WRITE("Folder: %s/%s%s\n", GAME_ROOT, g->name, g->uses_base ? "/base" : "");
    WRITE("ExeFS folder: %u | RomFS folder: %u\n", g->has_exefs, g->has_romfs);
    WRITE("main.npdm: present=%u META=%u\n", g->has_npdm, g->magic_npdm);
    WRITE("main: present=%u NSO0=%u size=%llu\n", g->has_main, g->magic_main, g->main_size);
    WRITE("rtld: present=%u NSO0=%u\n", g->has_rtld, g->magic_rtld);
    WRITE("sdk: present=%u NSO0=%u\n", g->has_sdk, g->magic_sdk);
    WRITE("Result: %s\n", g->path_error ? "path too long; inspection incomplete" :
        g->has_romfs && g->magic_npdm && g->magic_main ?
        "folder and signatures recognized; file integrity and launchability NOT checked" :
        "required folder or signature missing/unreadable");
    WRITE("Native execution is NOT implemented. Signatures do not validate a backup.\n");
#undef WRITE
    if (ferror(f)) failed = 1;
    if (fclose(f) != 0) failed = 1;
    if (failed) {
        snprintf(status, sizeof(status), "Report write failed; SD report may be incomplete.");
        return 0;
    }
    snprintf(status, sizeof(status), "Report saved: sdmc:/nx2backuploader-report.txt");
    return 1;
}
static void draw(void) {
    consoleClear();
    printf("NX2BackupLoader " APP_VERSION " | Switch 2 folder diagnostic\n");
    printf("=================================================\n");
    printf("SD folder: %s\n", GAME_ROOT);
    printf("Game files read only; A writes report to SD.\n");
    printf("Signature checks only. Game execution unavailable.\n\n");
    if (!count) printf("No game folders found.\n");
    int start = selection > 10 ? selection - 10 : 0;
    for (int i = start; i < count && i < start + 14; ++i) {
        printf("%c %-37.37s %s\n", i == selection ? '>' : ' ', games[i].name,
               !games[i].path_error && games[i].has_romfs && games[i].magic_main && games[i].magic_npdm ? "[SIGS]" : "[CHECK]");
    }
    if (count) {
        Game *g = &games[selection];
        printf("\nSelected: %.60s\n", g->name);
        printf("ExeFS %s | RomFS %s | NPDM %s | main %s\n",
               g->has_exefs ? "YES" : "NO", g->has_romfs ? "YES" : "NO",
               g->magic_npdm ? "META" : "BAD", g->magic_main ? "NSO0" : "BAD");
    }
    printf("\nUP/DOWN select | A report | X rescan | PLUS exit\n");
    printf("%s\n", status);
    consoleUpdate(NULL);
}
int main(int argc, char **argv) {
    (void)argc; (void)argv;
    consoleInit(NULL);
    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    scan();
    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 down = padGetButtonsDown(&pad);
        if (down & HidNpadButton_Plus) break;
        if (down & HidNpadButton_X) scan();
        if (count && (down & HidNpadButton_Up)) selection = (selection + count - 1) % count;
        if (count && (down & HidNpadButton_Down)) selection = (selection + 1) % count;
        if (count && (down & HidNpadButton_A)) { inspect(&games[selection]); report(&games[selection]); }
        draw();
    }
    consoleExit(NULL);
    return 0;
}
