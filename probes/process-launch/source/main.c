/* SPDX-License-Identifier: GPL-2.0-only */
#include <switch.h>
#include <kaihen.h>
#include <stdio.h>
#include <string.h>
#include "../../game-launch/source/folder.h"
#include "../../game-launch/source/resource_diagnostic.h"
#include "process_trial.h"

static PadState pad;
static int cleanup_incomplete;
static char outcome[160]="Select OOT, then A to test separate-process capability.";

static void run(const Nx2Game *game){
 FILE *log=fopen("sdmc:/BackupLoaderMKW-process.log","w");
 FILE *crash=fopen("sdmc:/BackupLoaderMKW-process-crash.log","w");
 if(!log){if(crash)fclose(crash);snprintf(outcome,sizeof(outcome),"Cannot create process experiment log.");return;}
 fprintf(log,"PROCESS_LAUNCH_EXPERIMENT version=1 selected=%s title=%016llx source=%s root=%s base_root=%s\n",
         game->name,(unsigned long long)game->id,game->is_nup?"nup":"folder",game->root,game->base_root);
 fprintf(log,"SCOPE kernel_child_selftest_only game_entry_not_called=1 folder_backend_pending=1 application_registration_pending=1\n");
 fprintf(log,"SELECTED_NPDM declared_resource=%u stack=%u priority=%u core=%u\n",
         game->resource,game->main_stack_size,game->main_priority,game->main_core);
 fflush(log);
 if(crash){fprintf(crash,"PROCESS_CRASH_REPORT version=1 selected=%s title=%016llx last_completed_operation_not_terminal_cause=1\n",game->name,(unsigned long long)game->id);fflush(crash);}
 u64 host=0,pid=0;
 Result title_rc=svcGetInfo(&host,InfoType_ProgramId,CUR_PROCESS_HANDLE,0);
 Result pid_rc=svcGetProcessId(&pid,CUR_PROCESS_HANDLE);
 fprintf(log,"HOST_ID title_rc=%08x title=%016llx pid_rc=%08x pid=%llu\n",title_rc,(unsigned long long)host,pid_rc,(unsigned long long)pid);fflush(log);
 Service ro={0},hen={0};HenStatus status={0};
 Result rc=kaihenGetRoSession(&ro);
 fprintf(log,"HEN_RO_SESSION result=%08x\n",rc);fflush(log);
 if(R_SUCCEEDED(rc)){
  rc=kaihenOpenHomebrewSession(&ro,&hen);
  fprintf(log,"HEN_HOMEBREW_SESSION result=%08x\n",rc);fflush(log);
  if(R_SUCCEEDED(rc)){
   rc=henGetStatus(&hen,&status);
   fprintf(log,"HEN_STATUS result=%08x protocol=%u\n",rc,status.protocol_version);fflush(log);
   serviceClose(&hen);
  }
 }
 Nx2ResourceDiagnostic resources={0};
 u64 trial_begin=armGetSystemTick();
 nx2_resource_diagnostic(log,&resources,"process_trial_before",0,game->resource,1);fflush(log);
 consoleClear();printf("BackupLoader Process Experiment 1\n\nTesting independent child process...\n\nThis build tests the process boundary.\nIt does not run the selected game yet.\n");consoleUpdate(NULL);
 int passed=nx2_process_trial(log,crash);
 nx2_resource_diagnostic(log,&resources,"process_trial_after",armTicksToNs(armGetSystemTick()-trial_begin)/1000000,game->resource,1);fflush(log);
 if(passed<0){
  cleanup_incomplete=1;
  snprintf(outcome,sizeof(outcome),"Cleanup incomplete. Close and restart the host title before another test; send logs.");
 }else snprintf(outcome,sizeof(outcome),passed?
   "Child test passed; full game process backend still required. Send process logs.":
   "Child test blocked or failed. Send both process logs for the exact stage.");
 if(crash)fclose(crash);
 fclose(log);
}

int main(int argc,char **argv){
 (void)argc;(void)argv;
 consoleInit(NULL);padConfigureInput(1,HidNpadStyleSet_NpadStandard);padInitializeDefault(&pad);
 Nx2Catalog catalog={0};nx2_catalog("sdmc:/games",&catalog);size_t selected=0;
 while(appletMainLoop()){
  padUpdate(&pad);u64 down=padGetButtonsDown(&pad);
  if(down&HidNpadButton_Plus)break;
  if(down&HidNpadButton_X){nx2_catalog_free(&catalog);nx2_catalog("sdmc:/games",&catalog);selected=0;}
  if(catalog.count){
   if(down&HidNpadButton_Up)selected=(selected+catalog.count-1)%catalog.count;
   if(down&HidNpadButton_Down)selected=(selected+1)%catalog.count;
   if((down&HidNpadButton_A)&&!cleanup_incomplete)run(&catalog.items[selected]);
  }
  consoleClear();printf("BackupLoader Process Experiment 1\n\nIndependent-process capability test\nFull game launching is not implemented in this build.\n\n");
  size_t first=(selected/12)*12;
  for(size_t i=first;i<catalog.count && i<first+12;i++)printf("%s %.62s\n",i==selected?">":" ",catalog.items[i].name);
  printf("\nA: test child process   X: refresh   +: exit\n\n%s\n\nLogs: SD root\nBackupLoaderMKW-process.log\nBackupLoaderMKW-process-crash.log\n",outcome);
  consoleUpdate(NULL);svcSleepThread(16666667);
 }
 nx2_catalog_free(&catalog);consoleExit(NULL);return 0;
}
