#include <switch.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>

#define GAME_ROOT "sdmc:/games"
#define MAX_GAMES 128
#define NAME_SIZE 180
#define PATH_SIZE 512

typedef struct {
    char name[NAME_SIZE];
    unsigned has_exefs, has_romfs, has_npdm;
    unsigned has_main, has_rtld, has_sdk;
    unsigned valid_npdm, valid_main, valid_rtld, valid_sdk;
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
static void path_join(char *dst, size_t cap, const char *parent, const char *leaf) {
    snprintf(dst, cap, "%s/%s", parent, leaf);
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
    path_join(dir, sizeof(dir), GAME_ROOT, g->name);
    path_join(exefs, sizeof(exefs), dir, "exefs");
    g->has_exefs = is_directory(exefs);
    path_join(p, sizeof(p), dir, "romfs");
    g->has_romfs = is_directory(p);
    path_join(p, sizeof(p), exefs, "main.npdm");
    g->has_npdm = is_file(p);
    g->valid_npdm = g->has_npdm && has_magic(p, "META");
    path_join(p, sizeof(p), exefs, "main");
    g->has_main = is_file(p);
    g->valid_main = g->has_main && has_magic(p, "NSO0");
    g->main_size = (g->has_main && stat(p, &st) == 0) ? (unsigned long long)st.st_size : 0;
    path_join(p, sizeof(p), exefs, "rtld");
    g->has_rtld = is_file(p);
    g->valid_rtld = g->has_rtld && has_magic(p, "NSO0");
    path_join(p, sizeof(p), exefs, "sdk");
    g->has_sdk = is_file(p);
    g->valid_sdk = g->has_sdk && has_magic(p, "NSO0");
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
    while ((entry = readdir(root)) && count < MAX_GAMES) {
        const char *name = entry->d_name;
        if (name[0] == '.' || strlen(name) >= NAME_SIZE) continue;
        char p[PATH_SIZE];
        path_join(p, sizeof(p), GAME_ROOT, name);
        if (!is_directory(p)) continue;
        Game *g = &games[count++];
        memset(g, 0, sizeof(*g));
        snprintf(g->name, sizeof(g->name), "%s", name);
        inspect(g);
    }
    closedir(root);
    snprintf(status, sizeof(status), "Found %d game folder(s). Press A for report.", count);
}
static int report(const Game *g) {
    FILE *f = fopen("sdmc:/nx2backuploader-report.txt", "a");
    if (!f) {
        snprintf(status, sizeof(status), "Could not write SD report; check storage.");
        return 0;
    }
    fprintf(f, "\n=== NX2BackupLoader diagnostic v0.1 ===\n");
    fprintf(f, "Folder: %s/%s\n", GAME_ROOT, g->name);
    fprintf(f, "ExeFS folder: %u | RomFS folder: %u\n", g->has_exefs, g->has_romfs);
    fprintf(f, "main.npdm: present=%u META=%u\n", g->has_npdm, g->valid_npdm);
    fprintf(f, "main: present=%u NSO0=%u size=%llu\n", g->has_main, g->valid_main, g->main_size);
    fprintf(f, "rtld: present=%u NSO0=%u\n", g->has_rtld, g->valid_rtld);
    fprintf(f, "sdk: present=%u NSO0=%u\n", g->has_sdk, g->valid_sdk);
    fprintf(f, "Result: %s\n", g->has_romfs && g->valid_npdm && g->valid_main ?
        "basic layout recognized; native execution NOT implemented" :
        "missing or invalid required game files");
    fclose(f);
    snprintf(status, sizeof(status), "Report saved: sdmc:/nx2backuploader-report.txt");
    return 1;
}
static void draw(void) {
    consoleClear();
    printf("NX2BackupLoader v0.1 | Switch 2 research tool\n");
    printf("=================================================\n");
    printf("SD folder: %s\n", GAME_ROOT);
    printf("READ-ONLY: does NOT launch/install games.\n\n");
    if (!count) printf("No game folders found.\n");
    int start = selection > 10 ? selection - 10 : 0;
    for (int i = start; i < count && i < start + 14; ++i) {
        printf("%c %-37.37s %s\n", i == selection ? '>' : ' ', games[i].name,
               games[i].valid_main && games[i].valid_npdm ? "[EXEFS]" : "[CHECK]");
    }
    if (count) {
        Game *g = &games[selection];
        printf("\nSelected: %.60s\n", g->name);
        printf("ExeFS %s | RomFS %s | NPDM %s | main %s\n",
               g->has_exefs ? "YES" : "NO", g->has_romfs ? "YES" : "NO",
               g->valid_npdm ? "META" : "BAD", g->valid_main ? "NSO0" : "BAD");
    }
    printf("\nUP/DOWN select | A report | X rescan | PLUS exit\n");
    printf("%.75s\n", status);
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
