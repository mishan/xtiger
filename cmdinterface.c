#include <stdlib.h>
#include "sysdeps.h"

#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>

#include "packets.h"
#include "cmdinterface.h"
#include "memory.h"
#include "specific.h"
#include "config.h"
#include "options.h"
#include "newcpu.h"
#include "hardware.h"
#include "core.h"

#include "globinfo.h"

char *varNames[0x15] = {"EXPR", "", "", "", "LIST", "", "MAT", "", "", "", "DATA", "TEXT", "STRING", "GDB",
                        "FIG", "", "PIC", "", "PRGM", "FUNC", "MAC" };

char fname[64];

UBYTE *backBuf = NULL;

int linkType = 0;
int progBar = 0;
int totalSize = 0;
int cmdState = 0;

tifile dirBuffer[64];

char currDir[128] = "";

int do_command(unsigned char *cmd, unsigned char *arg1);


void cmd_update_progbar(int size) {
  static cnt = 0;
  if((cnt++)&1) {
    printf(".");
    fflush(stdout);
  }
}

void print_dir(void) {
  int i = 0;
  while(dirBuffer[i].type != 255) {
    if(dirBuffer[i].type == 0x1f)
      printf("\n[%s]\n", dirBuffer[i].name);
    else
      printf("  %-012s %-4s %d\n", dirBuffer[i].name, varNames[dirBuffer[i].type], dirBuffer[i].size);
    i++;
  }
  
}

void cmd_link_progress(int type, char *name, int size) {
   switch(type) {
    case LINK_RUNNING:
      if(progBar)
	update_progbar(size);
      break;

    case LINK_FAIL:
    case LINK_STOP:
      speedyLink = 0;
      if(cmdState) {
	if(type == LINK_FAIL) {
	  printf("\nTransaction failed.\n");
	}
	else {
	  if(linkType == LINK_DIR)
	   print_dir();
	 else
	   printf("\nTransaction complete.\n");
	}
	linkType = progBar = 0;	 
	prompt_commands();
      }
      break;
    default:
      if(type == LINK_BACKUP)
	progBar = 1;
      linkType = type;
      totalSize = size;
   }
      
}

void save_cfg_file(char *name) {

  FILE *fp;
  if(fp = fopen(name, "w")) {

    fprintf(fp, "colors %d\n", globInf.grayPlanes);
    fprintf(fp, "romfile %s\n", globInf.romFile);
    fprintf(fp, "memfile %s\n", globInf.memFile);
    fprintf(fp, "tickrate %d\n", globInf.tickRate);
    fprintf(fp, "cyclerate %d\n", globInf.cycleRate);
    fprintf(fp, "itick %d\n", globInf.itick);
    fprintf(fp, "memsize %d\n", globInf.memSize);
    fprintf(fp, "iodevice %s\n", globInf.devFile);
    fprintf(fp, "darkcol #%08x\n", globInf.darkCol);
    fprintf(fp, "lightcol #%08x\n", globInf.lightCol);
    fclose(fp);
  }
}

void load_cfg_file(char *name) {

  FILE *fp;
  int linCnt = 0;
  char *cmd, *arg1, *arg2;
  char line[80];
  
  if(fp = fopen(name, "r")) {
    
    while(fgets(line, sizeof(line), fp)) {
      linCnt++;
      cmd = strtok(line, " \t\n");
      arg1 = strtok(NULL, " \t\n");
      arg2 = strtok(NULL, " \t\n");
      if(cmd && strlen(cmd) && cmd[0] != '#') {
	if(do_command(cmd, arg1))
	  printf("-Error in configfile line %d, command ignored\n", linCnt);
      }
    }
  }
}

int getOut = 0;

int do_command(unsigned char *cmd, unsigned char *arg1) {
    

  if(cmdState) {
    if(!(strcmp(cmd, "ls"))) {
      char ext[4];
      struct stat aStat;
      struct dirent *dent;
      DIR *dir = opendir(currDir);
      printf("%s\n", currDir);
      while(dent = readdir(dir)) {
	strcpy(ext, &dent->d_name[strlen(dent->d_name)-3]); 
	ext[2] = toupper(ext[2]);
	stat(dent->d_name, &aStat);
	if(S_ISDIR(aStat.st_mode))
	  printf("  [%s]\n", dent->d_name);
	else
	if(!strcmp(ext, "92P") || !strcmp(ext, "92B"))
	  printf("  %-32s %d\n", dent->d_name, aStat.st_size);
      }
      closedir(dir);
    }
    else
    if(!(strcmp(cmd, "cd"))) {
      if(arg1)  {
	if(chdir(arg1))
	  printf("\nIlleagal directory!\n");
        else
	  printf("\nCurrent directory changed to:\n%s\n", getcwd(currDir, sizeof(currDir)));
      }
      else
	printf("\nCurrent directory is:\n%s\n", currDir);

    }
    else
    if(!(strcmp(cmd, "files"))) {
      start_get_directory(dirBuffer, 64);
      getOut = 1;
    }
    else
    if(!(strcmp(cmd, "quit"))) {
      cmdState = 0;
      if(backBuf) {
		  free(backBuf);
		  backBuf = NULL;
      }
      screen_on();
      getOut = 1;
    }
    else
    if(!(strcmp(cmd, "load"))) {
      if(!send_ti_file(arg1, backBuf))
	   printf("File not found\n");
	 else
	   getOut =1;
    }
    else
    if(!(strcmp(cmd, "fargo"))) {
      char c;
      printf("This will reset the calc and try to load the fargo core.\nAre you sure you want to do this (Y/N)? ");
      scanf("%c", &c);
      if(toupper(c) == 'Y') {
	memcpy(ti_mem+0x1fab8, fargo_core, sizeof(fargo_core));
	put_long(0x64, 0x1fab8);
	cmdState = 0;
	screen_on();
	getOut = 1;
      }
    }
    else
    if(!(strcmp(cmd, "libreq"))) {
      char *ptrs[16];
      int i, n = query_libs(arg1, backBuf, ptrs);
      if(n >= 0) {
	printf("\nRequired libs:\n");
	for(i=0; i<n; i++)
	  printf("%s\n", ptrs[i]);
      }
    }
    else
    if(!(strcmp(cmd, "help"))) {
      printf("Currently recognized commands:\nfargo itick colors load quit files ls cd help\n");
    }
    else
    if(!(strcmp(cmd, "putcfg"))) {
      if(arg1)
	save_cfg_file(arg1);
      else
	save_cfg_file(globInf.cfgFile);
      printf("Config file saved\n");
    }
    else
    if(!(strcmp(cmd, "getcfg"))) {
      if(arg1)
	load_cfg_file(arg1);
      else
	load_cfg_file(globInf.cfgFile);
    }
  }

  if(!(strcmp(cmd, "colors"))) {
    if(arg1) {
      globInf.grayPlanes = atoi(arg1);
      set_colors();
    }
    if(cmdState)
      printf("\nCurrent extra colors: %d\n", globInf.grayPlanes);
  }
  else
  if(!(strcmp(cmd, "itick"))) {
    if(arg1)
      cycleInstr = globInf.itick = atoi(arg1);
    if(cmdState)
      printf("\nCurrent instruction per tick: %d\n", cycleInstr);
  }
  else
  if(!(strcmp(cmd, "memfile"))) {
    if(arg1)
      strcpy(globInf.memFile, arg1);
    if(cmdState)
      printf("\nMemfile is set to: \"%s\"\n", globInf.memFile);
    }
  else
  if(!(strcmp(cmd, "romfile"))) {
    if(arg1)
      strcpy(globInf.romFile, arg1);
    if(cmdState)
	printf("\nRomfile is set to: \"%s\"\n", globInf.romFile);
  }
  else
  if(!(strcmp(cmd, "iodevice"))) {
    if(arg1)
      strcpy(globInf.devFile, arg1);
    if(cmdState)
      printf("\nIODevice is set to: \"%s\"\n", globInf.devFile);
  }
  else
  if(!(strcmp(cmd, "memsize"))) {
    if(arg1)
      globInf.memSize = atoi(arg1);
    if(cmdState)
      printf("\nMemsize is set to: %d\n", globInf.memSize);
  }
  else
  if(!(strcmp(cmd, "cyclerate"))) {
    if(arg1)
      globInf.cycleRate = atoi(arg1);
    if(cmdState)
      printf("\nCyclerate is set to: %d\n", globInf.cycleRate);
  }
  else
  if(!(strcmp(cmd, "tickrate"))) {
    if(arg1)
      globInf.tickRate = atoi(arg1);
    if(cmdState)
      printf("\nTickrate is set to: %d\n", globInf.tickRate);
  }
  else
  if(!(strcmp(cmd, "darkcol"))) {
    if(arg1)
      globInf.darkCol = strtol(&arg1[1], NULL, 16);
    if(cmdState)
      printf("\nDarkcolor is set to: #%08x\n", globInf.darkCol);
  }
  else
  if(!(strcmp(cmd, "lightcol"))) {
    if(arg1)
      globInf.lightCol = strtol(&arg1[1], NULL, 16);
    if(cmdState)
      printf("\nLightcolor is set to: #%08x\n", globInf.lightCol);
  }
  else
  if(!(strcmp(cmd, "sync"))) {
    if(arg1)
      globInf.syncOne = atoi(arg1);
    if(cmdState)
      printf("\nScreen synced after %s\n", globInf.syncOne ? "irq1" : "timer");
  }
  else
    return 1;

  return 0;
  
}

void lf_strip (char *str)
{
	int len = strlen(str), i;

	for(i = 0; i < len; i++) {
		if(str[i] == '\n') {
			str[i] = 0;
			return;
		}
	}
}

void prompt_commands(void) {

  char *cmd, *arg1, *arg2;
  char line[80];
   
  getOut = 0;

  while(!getOut) {   
    printf("\ntiger>");

    fgets(line, 80, stdin);
	lf_strip(line);
    if(strlen(line) <= 0) {
		continue;
    }
    cmd = strtok(line, " ");
    arg1 = strtok(NULL, " ");
    arg2 = strtok(NULL, " ");
    if(cmd) {
		do_command(cmd, arg1);
	}
  }
}

void enter_command(void) {
  if(!strlen(currDir))
    getcwd(currDir, sizeof(currDir));

  backBuf = malloc(128*1024);

  printf("Commandstate entered. \"quit\" to quit.\n");
  cmdState = 1;
  prompt_commands();
}

