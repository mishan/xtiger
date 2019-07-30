/*
 * Hardware handling of the TI92 (keyboard reading, linkport, timers etc)
 *
 */

#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>
#include "sysdeps.h"
#include "config.h"
#include "options.h"
#include "hardware.h"
#include "memory.h"
#include "keyboard.h"
#include "specific.h"
#include "newcpu.h"
#include "packets.h"
#include "globinfo.h"

#define TO_VALUE 100

int onKey = 0;
int timerVal = 0;
int cycle_count = 0;

int internalLink = 0;

int timeOut = 0;

cycleInstr = 640;

void update_contrast(void) {
  set_contrast( ((ti_io[0x1d]&0xf)<<1) | ((ti_io[0]>>5)&1) );
}

void update_timer(UBYTE arg) {
  timerVal = arg;
}

void update_bitmap(void) {
  UWORD arg = (((UWORD)ti_io[0x10])<<8)|ti_io[0x11];
  set_screen_ptr(&ti_mem[arg<<3]);  
}

UBYTE read_keyboard_mask(void) {
    int i;
    UBYTE arg = 0;
    UWORD mask = (((UWORD)ti_io[0x18])<<8)|ti_io[0x19];

    for(i=0; i<10; i++)
      if(!(mask & (1<<i)))
	arg |= get_rowmask(i);
    return ~arg;
}

UBYTE read_onkey(void) {
  return onKey;
}

int fast_cycle = 0;

int byteAvail = 0;

/* This function should be called everytime the counter increases */
void do_period() {


   
  if(link_checkread())
    ti_io[0xc] |= 0x2;
  
  
  if(ti_io[0xc]&0x2) {
    specialflags |= SPCFLAG_INT;
    currIntLev = 4;
  }
  
  if(ti_io[0x17]++ == 0) {
    ti_io[0x17] = timerVal;
    specialflags |= SPCFLAG_INT;
    if(currIntLev < 5)
      currIntLev = 5;
  }
  
  if(!(fast_cycle&3)) {
    specialflags |= SPCFLAG_INT;
    currIntLev = 1;
  }

  
  if(++fast_cycle == 16) {

    if(internalLink) {
      if(timeOut++ >= TO_VALUE) {
	internalLink = 0;
	link_progress(LINK_FAIL, NULL, 0);
	timeOut = 0;
      }
    }
    if(ti_update_keys() && currIntLev < 1)
      currIntLev = 2;

    if(!globInf.syncOne)
      update_screen();
    
    fast_cycle = 0;

  }

}

UBYTE lastByte;



int linkFPr = -1;
int linkFPw = -1;


char linkReadName[64] = "/dev/ti";
char linkWriteName[64] = "/dev/ti";


void link_putbyte(UBYTE arg) {
  timeOut = 0;
  if(internalLink)
    prot_receive_byte(arg);
  else
    if(linkFPw >= 0)
      write(linkFPw, &arg, 1);
}

UBYTE link_getbyte(void) {
  timeOut = 0;
  if(internalLink) {
    return prot_get_byte();
  }

  byteAvail = 0;
  return lastByte;
}

int link_byteavail(void) {
  if(internalLink) {
    if(prot_bytes_left())
      return 1;
    else
      return 0;
  }
  return byteAvail;
}

int link_checkread(void) {
  if(internalLink) {
    if(prot_bytes_left())
      return 1;
    else
      return 0;
  }
  if(byteAvail)
    return 0;
  if(linkFPr >= 0)
    byteAvail = read(linkFPr, &lastByte, 1);
  if(byteAvail != 1)
    byteAvail = 0;
  return byteAvail;
}

void init_hardware(char *name) {

  int myPid = 0;
  strcpy(linkReadName, name);
  strcpy(linkWriteName, name);

  linkFPr = open(linkReadName, O_RDONLY);
  linkFPw = open(linkWriteName, O_WRONLY|O_CREAT, S_IRWXU);

  init_protocol();
  reset_protocol();

}

void exit_hardware(void) {
  if(linkFPr >= 0)
    close(linkFPr);
  if(linkFPw >= 0)
    close(linkFPw);

}
