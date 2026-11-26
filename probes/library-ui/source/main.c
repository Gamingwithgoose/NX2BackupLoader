/* SPDX-License-Identifier: GPL-2.0-only */
#include "ui.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char **argv){
 (void)argc;(void)argv;
 int action;
 do{
  PrintConsole *console=consoleInit(NULL);
  ConsoleFont fallback=console?console->font:(ConsoleFont){0};
  printf("BackupLoaderMKW\nReading /games and /nups...\n");consoleUpdate(NULL);
  Nx2Catalog catalog={0};
  nx2_catalog("sdmc:/games",&catalog);
  Nx2Catalog archives={0};
  nx2_catalog("sdmc:/nups",&archives);
  for(size_t i=0;i<archives.count&&catalog.count<4096;i++){
   if(!archives.items[i].is_nup)continue;
   Nx2Game *items=realloc(catalog.items,(catalog.count+1)*sizeof(*items));
   if(!items)break;
   catalog.items=items;catalog.items[catalog.count++]=archives.items[i];
  }
  nx2_catalog_free(&archives);
  consoleExit(NULL);
  action=nx2_library_ui(&catalog,fallback);
  nx2_catalog_free(&catalog);
  if(action<0){
   consoleInit(NULL);printf("Could not open the game library.\nSee SD:/BackupLoaderMKW-ui.log\n\nA / B: return\n");consoleUpdate(NULL);
   PadState pad;padConfigureInput(1,HidNpadStyleSet_NpadStandard);padInitializeDefault(&pad);
   while(appletMainLoop()){
    padUpdate(&pad);if(padGetButtonsDown(&pad)&(HidNpadButton_A|HidNpadButton_B))break;
    svcSleepThread(16666667);
   }
   consoleExit(NULL);
  }
 }while(action==2);
 return 0;
}
