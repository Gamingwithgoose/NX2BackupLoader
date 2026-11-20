#pragma once
#include <stdint.h>
typedef uint64_t u64;
typedef struct { int unused; } PadState;
enum { HidNpadStyleSet_NpadStandard=1, HidNpadButton_Plus=1, HidNpadButton_X=2, HidNpadButton_Up=4, HidNpadButton_Down=8, HidNpadButton_A=16 };
static void consoleInit(void *x) {(void)x;}
static void consoleExit(void *x) {(void)x;}
static void consoleUpdate(void *x) {(void)x;}
static void consoleClear(void) {}
static void padConfigureInput(int x, int y) {(void)x;(void)y;}
static void padInitializeDefault(PadState *x) {(void)x;}
static void padUpdate(PadState *x) {(void)x;}
static u64 padGetButtonsDown(PadState *x) {(void)x;return 0;}
static int appletMainLoop(void) {return 0;}
