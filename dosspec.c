 
#include <stdlib.h>
#include "sysdeps.h"
#include "config.h"
#include "options.h"

#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <signal.h>
#include <unistd.h>
#include <pc.h>
#include <go32.h>
#include <dpmi.h>
#include <sys/nearptr.h>
#include <sys/exceptn.h>
#include <conio.h>

#include "newcpu.h"
#include "keyboard.h"
#include "specific.h"
#include "packets.h"
#include "hardware.h"
#include "memory.h"
#include "scancode.h"

static unsigned char escape_keys[128] = {
  0,			0,			0,		0,
  0, 			0,			0,		0,
  0, 			0,			0,		0,
  0, 			0,			0,		0,
  0, 			0,			0,		0,
  0, 			0,			0,		0,
  0,			0,			0,		0,
  SCANCODE_KEYPADENTER,	SCANCODE_RIGHTCONTROL,	0,		0,
  0, 			0,			0,		0,
  0, 			0,			0,		0,
  0, 			0,			0,		0,
  0, 			0,			0,		0,
  0, 			0,			0,		0,
  0, 			0,			0,		0,
  0, 			0,			0,		0,
  0, 			0,			0,		0,
  0, 			0,			0,		0,
  0,			0, 			0,		SCANCODE_HOME,
  SCANCODE_CURSORBLOCKUP,	SCANCODE_PAGEUP,		0,		SCANCODE_CURSORBLOCKLEFT,
  0,			SCANCODE_CURSORBLOCKRIGHT,	0,		SCANCODE_END,
  SCANCODE_CURSORBLOCKDOWN,SCANCODE_PAGEDOWN,		SCANCODE_INSERT,	SCANCODE_REMOVE,
  0,			0,			0,		0,
  0,			0,			0,		SCANCODE_LWIN95,
  SCANCODE_RWIN95,		SCANCODE_MWIN95,		0,		0,
  0,			0,			0,		0,
  0,			0,			0,		0,
  0,			0,			0,		0,
  0,			0,			0,		0,
  0,			0,			0,		0,
  0,			0,			0,		0,
  0,			0,			0,		0,
  0,			0,			0,		0
};

#define ESTAT_SCREENON 1
#define ESTAT_LINKON 2
#define ESTAT_UPDATESCREEN 4
#define ESTAT_CMDS 8


int contrast = 16;

/* Planar->Chunky conversion table */
int convtab[512];

/* Contrast for text & background */
int c0 = 0;
int c1 = 0;

/* Screenmode */
#define VGA_MODE 0x13
#define TEXT_MODE 0x3

/* Keystates for TI92 keys */
UBYTE keyStates[120];

UBYTE vga2ti[120];

volatile int keyWasPressed;

ULONG *screenBuf = NULL;

int grayPlanes = 0;
int currPlane = 0;
ULONG lightCol = 0;
ULONG darkCol = 0;

/* Pointer to TI92 planar screen */
unsigned char *the_screen = NULL;
int tmp[80];

int emuState = 0;

void set_colors(void);

void vga_setpalette(int n, int r, int g, int b) {
  outportb(0x3c8, n);
  outportb(0x3c9, r);
  outportb(0x3c9, g);
  outportb(0x3c9, b);
}

void vga_setmode(int mode) {
    _go32_dpmi_registers regs;
    regs.x.ax=mode;
    regs.x.ss=regs.x.sp=regs.x.flags=0;
    _go32_dpmi_simulate_int(0x10, &regs);
}


static void ti_keybhandler(void) {
  static is_escape = 0;
  int scancode, newstate, tikey;
  scancode = inportb(0x60);
  if (scancode == 0xe0) {
	is_escape = 1;
	outportb(0x20, 0x20);
	return;
  }
  newstate = !(scancode & 0x80);
  scancode = scancode & 0x7f;
  if (is_escape) {
	scancode = escape_keys[scancode];
	is_escape = 0;
  }
  outportb(0x20, 0x20);

  tikey = vga2ti[scancode];
  keyStates[tikey] = newstate;
  if(newstate && tikey != TIKEY_ON)
    keyWasPressed = 1;
}

void update_progbar(int size) {
  cmd_update_progbar(size);
}

void link_progress(int type, char *name, int size) {
  cmd_link_progress(type, name, size);
}

int update_keys(void) {

  int rc = keyWasPressed;
    
  if(keyWasPressed) {
    if(keyStates[OPT_DEBUGGER]) {
      keyStates[OPT_DEBUGGER] = 0;
      enter_debugger();
    }
    else
      if(keyStates[OPT_QUIT]) 
	specialflags |= SPCFLAG_BRK;
    else
      if(keyStates[OPT_LOADFILE]) {
	keyStates[OPT_LOADFILE] = 0;
	screen_off();
	enter_command();
      }
  }
  keyWasPressed = 0;
  return rc;
}

int is_key_pressed(int key) {
  return keyStates[key];
}

int init_specific(void) {
  int i,j,k;
  unsigned char *tmptab = (unsigned char *)convtab;

  for(k = 0, i = 0; i<256; i++) {
    for(j = 7; j>=0; j--)
      if((i>>j)&1)
	tmptab[k++] = 1;
      else
	tmptab[k++] = 0;
  }

  for(i=0; i<120; i++)
    keyStates[i] = 0;

  for(i=0; i<120; i++)
    vga2ti[i] = vga_to_ti(i);

  screenBuf = malloc(320*128);
  for(i=0; i<80*128; i++)
    screenBuf[i] = 0xffffffff;
  
  return 1;
}



#define filter(v, l, h) (v<l ? l : (v>h ? h : v))

void set_colors(void) {
  
  int i,c;
  int sr = lightCol>>18,
      sg = (lightCol>>10)&0x3f, 
      sb = (lightCol>>2)&0x3f;

  int er = darkCol>>18,
      eg = (darkCol>>10)&0x3f, 
      eb = (darkCol>>2)&0x3f;

  int sr2 = sr, sg2 = sg, sb2 = sb;
  int er2 = er, eg2 = eg, eb2 = eb;
  
  int r,r2, g,g2, b,b2;

  if(contrast < 16) {
    sr = sr - (sr-er)*(16 - contrast)/13;
    sg = sg - (sg-eg)*(16 - contrast)/13;
    sb = sb - (sb-eb)*(16 - contrast)/13;
  }
  else {
    er = er - (er-sr)*(contrast - 16)/13;
    eg = eg - (eg-sg)*(contrast - 16)/13;
    eb = eb - (eb-sb)*(contrast - 16)/13;
  }
    
  r = sr, g = sg, b = sb;

  if(emuState & ESTAT_SCREENON) {
    for(i=0; i<=(grayPlanes+1); i++, r-=((sr-er)/(grayPlanes+1)), g-=((sg-eg)/(grayPlanes+1)), b-=((sb-eb)/(grayPlanes+1))) {
       r2 = filter(r, er2, sr2);
       g2 = filter(g, eg2, sg2);
       b2 = filter(b, eb2, sb2);
      vga_setpalette(i,r2,g2,b2);
    }
    vga_setpalette(0xff,0,0,32);
  }
}

int lastc = 0;

void set_contrast(int c) {

  int lco = contrast;

  contrast = (c+lastc)/2;
  lastc = c;
  if((emuState & ESTAT_SCREENON) && (lco != contrast))
    set_colors();
}

_go32_dpmi_seginfo old_kbd_handler, new_kbd_handler;

void screen_on(void) {
  int i;

  vga_setmode(VGA_MODE);

  if(__djgpp_nearptr_enable()) {
    void *ptr = __djgpp_conventional_base + 0xa0000;
    memset(ptr, 0xff, 200*320);
    __djgpp_nearptr_disable();
  }


  _go32_dpmi_get_protected_mode_interrupt_vector(9, &old_kbd_handler);
  new_kbd_handler.pm_offset = (int)ti_keybhandler;
  if(_go32_dpmi_allocate_iret_wrapper(&new_kbd_handler) != 0) {
    fprintf(stderr, "Can't allocate keyboard iret_wrapper.\n");
    return 0;
  }
  if (_go32_dpmi_set_protected_mode_interrupt_vector(9, &new_kbd_handler) != 0) {
    fprintf(stderr, "Can't set protected mode interrupt vector.\n");
    return 0;
  }
  
  emuState |= ESTAT_SCREENON;
  set_colors();
}

void screen_off(void) {

  _go32_dpmi_set_protected_mode_interrupt_vector(9, &old_kbd_handler);
  _go32_dpmi_free_iret_wrapper(&new_kbd_handler);

  vga_setmode(TEXT_MODE);
  emuState &= ~ESTAT_SCREENON;
}

void exit_specific(void) {
}

void set_screen_ptr(unsigned char *ptr) {
  the_screen = ptr;
}

void update_screen(void) {

  unsigned char *ptr = the_screen;
  int i,j,k;

  if(!(emuState & ESTAT_SCREENON))
    return;
  
  if(grayPlanes != globInf.grayPlanes || darkCol != globInf.darkCol || lightCol != globInf.lightCol) {
    grayPlanes = globInf.grayPlanes;
    darkCol = globInf.darkCol;
    lightCol = globInf.lightCol;
    set_colors();
  }

  if(!grayPlanes || !currPlane) {
    for(j=0, k=0; k<128; k++, j+=20)
      for(i=0; i<30; i++, ptr++) {
	screenBuf[10+j++] = convtab[(*ptr)<<1];
	screenBuf[10+j++] = convtab[((*ptr)<<1)+1];
      }
  }
  else {
    for(j=0, k=0; k<128; k++, j+=20)
      for(i=0; i<30; i++, ptr++) {
	screenBuf[10+j++] += convtab[(*ptr)<<1];
	screenBuf[10+j++] += convtab[((*ptr)<<1)+1];
      }
  }

  if(currPlane++ >= grayPlanes) {    
    if(__djgpp_nearptr_enable()) {
      ptr = __djgpp_conventional_base + 0xa0000 + 36*320;
      memcpy(ptr, screenBuf, 128*320);
      __djgpp_nearptr_disable();
    }
    currPlane = 0 ;
  }
}
 
int vga_to_ti(int key) {
  switch(key) {
  case SCANCODE_A : return TIKEY_A;
  case SCANCODE_B : return TIKEY_B;
  case SCANCODE_C : return TIKEY_C;
  case SCANCODE_D : return TIKEY_D;
  case SCANCODE_E : return TIKEY_E;
  case SCANCODE_F : return TIKEY_F;
  case SCANCODE_G : return TIKEY_G;
  case SCANCODE_H : return TIKEY_H;
  case SCANCODE_I : return TIKEY_I;
  case SCANCODE_J : return TIKEY_J;
  case SCANCODE_K : return TIKEY_K;
  case SCANCODE_L : return TIKEY_L;
  case SCANCODE_M : return TIKEY_M;
  case SCANCODE_N : return TIKEY_N;
  case SCANCODE_O : return TIKEY_O;
  case SCANCODE_P : return TIKEY_P;
  case SCANCODE_Q : return TIKEY_Q;
  case SCANCODE_R : return TIKEY_R;
  case SCANCODE_S : return TIKEY_S;
  case SCANCODE_T : return TIKEY_T;
  case SCANCODE_U : return TIKEY_U;
  case SCANCODE_V : return TIKEY_V;
  case SCANCODE_W : return TIKEY_W;
  case SCANCODE_X : return TIKEY_X;
  case SCANCODE_Y : return TIKEY_Y;
  case SCANCODE_Z : return TIKEY_Z;
  case SCANCODE_0 : return TIKEY_0;
  case SCANCODE_1 : return TIKEY_1;
  case SCANCODE_2 : return TIKEY_2;
  case SCANCODE_3 : return TIKEY_3;
  case SCANCODE_4 : return TIKEY_4;
  case SCANCODE_5 : return TIKEY_5;
  case SCANCODE_6 : return TIKEY_6;
  case SCANCODE_7 : return TIKEY_7;
  case SCANCODE_8 : return TIKEY_8;
  case SCANCODE_9 : return TIKEY_9;
  case SCANCODE_KEYPAD0 : return TIKEY_0;
  case SCANCODE_KEYPAD1 : return TIKEY_1;
  case SCANCODE_KEYPAD2 : return TIKEY_2;
  case SCANCODE_KEYPAD3 : return TIKEY_3;
  case SCANCODE_KEYPAD4 : return TIKEY_4;
  case SCANCODE_KEYPAD5 : return TIKEY_5;
  case SCANCODE_KEYPAD6 : return TIKEY_6;
  case SCANCODE_KEYPAD7 : return TIKEY_7;
  case SCANCODE_KEYPAD8 : return TIKEY_8;
  case SCANCODE_KEYPAD9 : return TIKEY_9;
  case SCANCODE_CURSORBLOCKUP : return TIKEY_UP;
  case SCANCODE_CURSORBLOCKLEFT : return TIKEY_LEFT;
  case SCANCODE_CURSORBLOCKRIGHT : return TIKEY_RIGHT;
  case SCANCODE_CURSORBLOCKDOWN : return TIKEY_DOWN;
  case SCANCODE_F1 : return TIKEY_F1;
  case SCANCODE_F2 : return TIKEY_F2;
  case SCANCODE_F3 : return TIKEY_F3;
  case SCANCODE_F4 : return TIKEY_F4;
  case SCANCODE_F5 : return TIKEY_F5;
  case SCANCODE_F6 : return TIKEY_F6;
  case SCANCODE_F7 : return TIKEY_F7;
  case SCANCODE_F8 : return TIKEY_F8;
  case SCANCODE_ENTER : return TIKEY_ENTER1;
  case SCANCODE_KEYPADENTER : return TIKEY_ENTER2;
  case SCANCODE_LEFTSHIFT : return TIKEY_SHIFT;
  case SCANCODE_RIGHTSHIFT : return TIKEY_SHIFT;
  case SCANCODE_LEFTCONTROL : return TIKEY_DIAMOND;
  case SCANCODE_RIGHTCONTROL : return TIKEY_DIAMOND;
  case SCANCODE_LEFTALT : return TIKEY_2ND;
  case SCANCODE_RIGHTALT : return TIKEY_2ND;
  case SCANCODE_CAPSLOCK : return TIKEY_HAND;
  case SCANCODE_TAB : return TIKEY_STORE;
  case SCANCODE_SPACE : return TIKEY_SPACE;
  case SCANCODE_ESCAPE : return TIKEY_ESCAPE;
  case SCANCODE_BACKSPACE : return TIKEY_BACKSPACE;
  case SCANCODE_BRACKET_LEFT : return TIKEY_PALEFT;
  case SCANCODE_BRACKET_RIGHT : return TIKEY_PARIGHT;
  case SCANCODE_PERIOD : return TIKEY_PERIOD;
  case SCANCODE_COMMA : return TIKEY_COMMA;
  case SCANCODE_KEYPADMINUS : return TIKEY_MINUS;
  case SCANCODE_KEYPADPLUS : return TIKEY_PLUS;
  case SCANCODE_KEYPADMULTIPLY : return TIKEY_MULTIPLY;
  case SCANCODE_KEYPADDIVIDE : return TIKEY_DIVIDE;    
  case SCANCODE_MINUS : return TIKEY_MINUS;
  case SCANCODE_BACKSLASH : return TIKEY_PLUS;
  case SCANCODE_APOSTROPHE : return TIKEY_MULTIPLY;
  case SCANCODE_SLASH : return TIKEY_DIVIDE;
  case SCANCODE_SEMICOLON : return TIKEY_THETA;
  case SCANCODE_EQUAL : return TIKEY_EQUALS;
  case SCANCODE_GRAVE : return TIKEY_POWER;
  case SCANCODE_LESS : return TIKEY_NEGATE;
  case SCANCODE_KEYPADPERIOD : return TIKEY_PERIOD;
  case SCANCODE_INSERT : return TIKEY_LN;
  case SCANCODE_REMOVE : return TIKEY_SIN;
  case SCANCODE_HOME : return TIKEY_CLEAR;
  case SCANCODE_END : return TIKEY_COS;
  case SCANCODE_PAGEUP : return TIKEY_MODE;
  case SCANCODE_PAGEDOWN : return TIKEY_TAN;
  case SCANCODE_F12 :
  case SCANCODE_PRINTSCREEN : return TIKEY_APPS;
  case SCANCODE_SCROLLLOCK : return TIKEY_ON;
  case SCANCODE_F9 : return OPT_DEBUGGER;
  case SCANCODE_F10 : return OPT_LOADFILE;
  case SCANCODE_F11 : return OPT_QUIT;
  case SCANCODE_BREAK_ALTERNATIVE :
  case SCANCODE_BREAK : return OPT_QUITNOSAVE;
  default : return TIKEY_NU;
  }
}

