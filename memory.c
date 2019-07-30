
#include <stdlib.h>
#include "sysdeps.h"
#include <string.h>
#include <stdio.h>
#include "config.h"
#include "options.h"
#include "memory.h"
#include "debug.h"
#include "hardware.h"
#include "newcpu.h"

int buserr = 0;

UBYTE *ti_mem;
UBYTE *ti_rom;
UBYTE *ti_io;

UBYTE * mem_tab[8] = {0,0,0,0,0,0,0,0};
ULONG mem_mask[8] = {0,0,0,0,0,0,0,0};

int MEM_SIZE;
int ROM_SIZE;


/* --- Support for memory access logging --- */

FILE *logFP = NULL;

char * bwl_str[] = {"byte", "word", "long"};
char * rw_str[] = {"read", "write"};

#define LM_BYTE 0
#define LM_WORD 1
#define LM_LONG 2

#define LM_READ 0
#define LM_WRITE 1

void log_mem(CPTR adr, int size, int rw, int arg) {
  if(logFP) {
    fprintf(logFP, "%s%s at 0x%x (%d)\n", bwl_str[size], rw_str[rw], adr, arg);
  }
}

/* ------------------------------------------ */


/* Initialize Memory */
int mem_init(int msize, int rsize) {
  int i;
  if(msize != 128 && msize != 256)
    return 0;
  MEM_SIZE = msize*1024;
  ROM_SIZE = rsize;

  ti_mem = malloc(MEM_SIZE+4);
  ti_rom = malloc(ROM_SIZE+4);
  ti_io  = malloc(IO_SIZE+4);

  memset(ti_io, 0, IO_SIZE);

  /* Set all banks to RAM (with mask 0 per default) */
  for(i=0; i<8; i++)
    mem_tab[i] = ti_mem;


  /* Map RAM */
  mem_tab[0] = ti_mem;
  mem_mask[0] = MEM_SIZE-1;


  /* Map ROM in two places */
  mem_tab[1] = ti_rom;
  mem_mask[1] = ROM_SIZE-1;
  mem_tab[2] = ti_rom;
  mem_mask[2] = ROM_SIZE-1;


  /* Map IO */
  mem_tab[3] = ti_io;
  mem_mask[3] = IO_SIZE-1;


  /*  logFP = fopen("iolog.txt", "w"); */

  return(ti_mem && ti_rom && ti_io);

}

/* Free Memory */
void mem_exit(void) {
  if(ti_mem)
    free(ti_mem);
  if(ti_rom)
    free(ti_rom);
  if(ti_io)
    free(ti_io);
  /*  if(logFP)
    fclose(logFP);*/
}

#define bput(adr, arg) {mem_tab[(adr)>>21][(adr) & mem_mask[(adr)>>21]] = (arg);}
#define wput(adr, arg) {bput((adr), (arg)>>8) ; bput((adr)+1, (arg)&0xff);}
#define lput(adr, arg) {wput((adr), (arg)>>16) ; wput((adr)+2, (arg)&0xffff);}

#define bget(adr) (mem_tab[(adr)>>21][(adr)&mem_mask[(adr)>>21]])
#define wget(adr) ((UWORD)(((UWORD)bget(adr))<<8|bget((adr)+1)))
#define lget(adr) ((ULONG)(((ULONG)wget(adr))<<16|wget((adr)+2)))

void io_put_byte(CPTR adr, UBYTE arg) {
  switch(adr) {
  case 0xc :
    if(arg&0x2) {
      specialflags |= SPCFLAG_INT;
      currIntLev = 4;
    }
    break;
  case 0xf :
    link_putbyte(arg);
    return;
  case 0x1d :
    ti_io[adr] = arg;     
    update_contrast();
    return;
  case 0x17 :
    update_timer(arg);
    return;
  case 0x10 :
  case 0x11 :
    ti_io[adr] = arg;
    update_bitmap();
    return;
  }
  ti_io[adr] = arg;
}

void io_put_word(CPTR adr, UWORD arg) {
  io_put_byte(adr, arg>>8);
  io_put_byte(adr+1, arg&0xff);
}

void io_put_long(CPTR adr, ULONG arg) {
  io_put_word(adr, arg>>16);
  io_put_word(adr+2, arg&0xffff);
}


UBYTE io_get_byte(CPTR adr) {
  switch(adr) {
  case 0x00 :
    return ti_io[0]|4;
  case 0x01 :
    return (MEM_SIZE == 128*1024 ? ti_io[1]|1 : ti_io[1]&~1);
  case 0x0c:
    break;
  case 0x0d :
    /* Always return transmitbuffer = empty */
    return (link_byteavail() ? 0x60 : 0x40);
  case 0x0f :
    return link_getbyte();
  case 0x1a :
    return (1-read_onkey())<<1;
  case 0x1b :
    return read_keyboard_mask();
  default :
    return ti_io[adr];
  }
  return ti_io[adr];
}

UWORD io_get_word(CPTR adr) {
  return (((UWORD)io_get_byte(adr))<<8)|io_get_byte(adr+1);
}

ULONG io_get_long(CPTR adr) {
  return (((ULONG)io_get_word(adr))<<16)|io_get_word(adr+2);
}


ULONG get_long(CPTR adr) {
  adr &= 0xFFFFFF;
  if(adr&1) {
    specialflags |= SPCFLAG_ADRERR;
    return 0;
  }
  if(adr >= 0x600000)
    return io_get_long(adr&0x1f);
  else
    return lget(adr);  
}

UWORD get_word(CPTR adr) {
  adr &= 0xFFFFFF;
  if(adr&1) {
    specialflags |= SPCFLAG_ADRERR;
    return 0;
  }
  if(adr >= 0x600000)
    return io_get_word(adr&0x1f);
  else
    return wget(adr);
 
}

UBYTE get_byte(CPTR adr) {
  adr &= 0xFFFFFF;
  if(adr >= 0x600000)
    return io_get_byte(adr&0x1f);
  else
    return bget(adr);
 
}

void put_long(CPTR adr, ULONG arg) {
  adr &= 0xFFFFFF;
  if(adr&1)
    specialflags |= SPCFLAG_ADRERR;
  else {
    if(adr >= 0x600000)
      io_put_long(adr&0x1f, arg);
    else
      lput(adr, arg);
  }  

}

void put_word(CPTR adr, UWORD arg) {
  adr &= 0xFFFFFF;
  if(adr&1)
    specialflags |= SPCFLAG_ADRERR;
  else {
    if(adr >= 0x600000)
      io_put_word(adr&0x1f, arg);
    else
      wput(adr, arg);
  }  
}

void put_byte(CPTR adr, UBYTE arg) {
  adr &= 0xFFFFFF;
  if(adr >= 0x600000)
    io_put_byte(adr&0x1f,arg);
  else
    bput(adr, arg);
}

UBYTE *get_real_address(CPTR adr) {
  return &mem_tab[(adr>>21)&0x7][adr&mem_mask[(adr>>21)&0x7]];
}

int valid_address(CPTR adr, ULONG size) {
  return 1;
}


