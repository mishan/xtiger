 /*
  */

#include <stdlib.h>
#include "sysdeps.h"
#include <stdio.h>
#include <assert.h>
#include <ctype.h>
#include "config.h"
#include "options.h"
#include "memory.h"
#include "newcpu.h"
#include "debug.h"
#include "hardware.h"
#include "specific.h"
#include "globinfo.h"


struct GlobalInformation globInf;

/*
CPTR nextpc;

int enterDebugger = 0;
*/

int breakPoints = 0;
ULONG breakAdr[16];
UWORD breakSave[16];
int activeBreak = -1;


int intlink = 0;
int romsize = 0;
int dontSaveState = 0;

#ifdef IN_UNIX2
#define DEFAULT_CFGFILE "~/.tigerrc"
#define DEFAULT_ROMFILE "~/ti92.rom"
#define DEFAULT_MEMFILE "~/ti92.mem"
#else
#define DEFAULT_CFGFILE "tiger.cfg"
#define DEFAULT_ROMFILE "ti92.rom"
#define DEFAULT_MEMFILE "ti92.mem"
#endif

#define DEFAULT_DEVFILE "/dev/ti"


int main(int argc, char **argv)
{
  FILE *fp;
  char *arg, *cmd = NULL;
  int i;

  strcpy(globInf.cfgFile, DEFAULT_CFGFILE);
  strcpy(globInf.romFile, DEFAULT_ROMFILE);
  strcpy(globInf.memFile, DEFAULT_MEMFILE);
  strcpy(globInf.devFile, DEFAULT_DEVFILE);
  globInf.memSize = 128;
  globInf.grayPlanes = 2;
  cycleInstr = globInf.itick = 640;
  globInf.tickRate = 40000;
  globInf.cycleRate = 150;
  globInf.darkCol = 0x000034;
  globInf.lightCol = 0x989898;
  globInf.syncOne = 0;
  
  for(i=1; i<argc-1; i++) {
	  if(!strcmp(argv[i], "-c"))
		  strcpy(globInf.cfgFile, argv[i+1]);
	  if(!strcmp(argv[i], "-r"))
		  strcpy(globInf.romFile, argv[i+1]);
  }
  
  load_cfg_file(globInf.cfgFile);
  
  for(i=1; i<argc; i++) {
	  if(argv[i][0] == '-')
		  cmd = &argv[i][1];
	  else {
		  if(cmd) {
			  do_command(cmd, argv[i]);
			  cmd = NULL;
		  }
	  }
  }
  
  if((fp = fopen(globInf.romFile, "rb"))) {
    fseek(fp, 0, SEEK_END);
    romsize = ftell(fp);
    fclose(fp);
  }
  else {
    printf("**Error: Cant find \"%s\"!\n", globInf.romFile);
    exit(10);
  }

  if(!(romsize == 1024*1024 || romsize == 1024*1024*2)) {
    printf("**Error: Illegal romsize. Must be 1Mb or 2Mb.\n");
    exit(10);
  }
    
  if(!(mem_init(globInf.memSize, romsize))) {
    printf("**Error: Could not initialize memory.\n");
    exit(10);
  }    
    
  init_m68k();
  
  if((fp = fopen(globInf.romFile, "rb"))) {
    fread(ti_rom, 1, romsize, fp);
    fclose(fp);
  }
  else
    exit(100);

  init_keyboard();
  init_specific();
  screen_on();
  set_screen_ptr(ti_mem);

  init_hardware(globInf.devFile);

  if((fp = fopen(globInf.memFile, "rb"))) {
    int m;
    fread(&m, 1, sizeof(int), fp);
    if(m != MEM_SIZE) {
      fclose(fp);

      rom_at_0();
      MC68000_reset();
      ram_at_0();
      
    }
    else {
      fread(ti_mem, 1, MEM_SIZE, fp);
      fread(ti_io, 1, IO_SIZE, fp);
      fread(&regs, 1, sizeof(regs), fp);
      fread(&timerVal, 1, sizeof(timerVal), fp);
      fread(&specialflags, 1, sizeof(specialflags), fp);
      fclose(fp); 
      MakeFromSR();
      m68k_setpc(regs.pc);
      update_contrast();
      update_bitmap();
    }
  }
  else {
    rom_at_0();
    MC68000_reset();
    ram_at_0();
    
  }

#ifdef PENT_COUNTER
  if(globInf.tickRate)
    calibrate_pcounter();
#endif
  
  MC68000_run();

  if(!dontSaveState && strlen(globInf.memFile) && (fp = fopen(globInf.memFile, "wb"))) {
    int m = MEM_SIZE;
    m68k_setpc(m68k_getpc());
    MakeSR();
    fwrite(&m, 1, sizeof(int), fp);
    fwrite(ti_mem, 1, MEM_SIZE, fp);
    fwrite(ti_io, 1, IO_SIZE, fp);
    fwrite(&regs, 1, sizeof(regs), fp); 
    fwrite(&timerVal, 1, sizeof(timerVal), fp);
    fwrite(&specialflags, 1, sizeof(specialflags), fp);
    fclose(fp); 
  }

  exit_hardware();

  screen_off();
  exit_specific();
  mem_exit();
  return 0;
}
