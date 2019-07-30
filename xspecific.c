#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>

#include "sysdeps.h"
#include "config.h"
#include "options.h"
#include "newcpu.h"
#include "keyboard.h"
#include "specific.h"
#include "cmdinterface.h"
#include "debug.h"
#include "globinfo.h"

#include <ggi/ggi.h>
#include <ggi/keyboard.h>

ggi_visual_t v;
int using_dbuf;

#define WIDTH  240
#define HEIGHT 128

#define ESTAT_SCREENON 1
#define ESTAT_LINKON 2
#define ESTAT_CMDS 8

int contrast = 16;
/* Planar->Chunky conversion table */
int convtab[512];
/* Keystates for TI92 keys */
UBYTE keyStates[120];
volatile int keyWasPressed;

ULONG *screenBuf = NULL;
int grayPlanes = 2;
int currPlane = 0;
/* Pointer to TI92 planar screen */

unsigned char *the_screen = NULL;

int tmp[80];

int emuState = 0;


void set_colors(void);
int OpenTigerWin(void);
void CloseTigerWin(void);
void PutImage(void);
int x_to_ti(int key);
void update_progbar(int size);

void update_progbar(int size) {
  cmd_update_progbar(size);
}

void link_progress(int type, char *name, int size) {
  cmd_link_progress(type, name, size);
}

int update_keys(void) {
  struct timeval tv;
  ggi_event ggievent;
  int n, l;

  tv.tv_sec = 0;
  tv.tv_usec = 100; /* change to 0 for non-blocking behaviour */

  ggiEventPoll(v, emKey, &tv);

  l = n = ggiEventsQueued(v, emKey);

  while(n--) {
    ggiEventRead(v, &ggievent, emKey);
    switch(ggievent.any.type) {
    case evKeyPress:
      keyStates[x_to_ti(ggievent.key.sym)] = 1;
      if(keyStates[OPT_DEBUGGER]) {
        keyStates[OPT_DEBUGGER] = 0;
        enter_debugger();
      }
      else if(keyStates[OPT_QUIT]) 
        specialflags |= SPCFLAG_BRK;
      else if(keyStates[OPT_LOADFILE]) {
        keyStates[OPT_LOADFILE] = 0;
        screen_off();
        enter_command();
      }
      break;
    case evKeyRelease:
      keyStates[x_to_ti(ggievent.key.sym)] = 0;
	}
  }

  return (0);
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

  return OpenTigerWin();
}

#define filter(v, l, h) (v<l ? l : (v>h ? h : v))

void set_colors(void) {
/*  
  int i,c;
  int sr = 42, sg = 45, sb =42;
  int er = 0, eg = 0, eb = 13;

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
       r2 = filter(r, 0, 42);
       g2 = filter(g, 0, 45);
       b2 = filter(b, 13, 42);
      vga_setpalette(i,r2,g2,b2);
    }
    vga_setpalette(0xff,0,0,32);
  }
*/
}

int lastc = 0;

void set_contrast(int c) {

  contrast = (c+lastc)/2;
  lastc = c;
  set_colors();
}

void screen_on(void) {
  emuState |= ESTAT_SCREENON;
}

void screen_off(void) {
  emuState &= ~ESTAT_SCREENON;
}

void exit_specific(void) {
  CloseTigerWin();
}

void set_screen_ptr(unsigned char *ptr) {
  the_screen = ptr;
}

void update_screen(void) {

  unsigned char *ptr = the_screen;
  int i,j,k;
/*
  if(grayPlanes != globInf.grayPlanes) {
    grayPlanes = globInf.grayPlanes;
    set_colors();
  }
*/
  if(!(emuState & ESTAT_SCREENON))
    return;

  if(!grayPlanes || !currPlane) {
	  for(j=0, k=0; k<128; k++) {
		  for(i=0; i<30; i++, ptr++) {
			  screenBuf[j++] = convtab[(*ptr)<<1];
			  screenBuf[j++] = convtab[((*ptr)<<1)+1];
//			   screenBuf[j++] = convtab[((*ptr)<<1)];
//			   screenBuf[j++] = convtab[((*ptr)<<1)+1];
		  }
	  }
  }

  else {
	  for(j=0, k=0; k<128; k++) {
		  for(i=0; i<30; i++, ptr++) {
			  screenBuf[j++] += convtab[(*ptr)<<1];
			  screenBuf[j++] += convtab[((*ptr)<<1)+1];
			  //screenBuf[j++] += convtab[(*ptr)<<1];
			  //screenBuf[j++] += convtab[((*ptr)<<1)+1];
		  }
	  }
	  }

  if(currPlane++ >= grayPlanes) {
    PutImage();
    currPlane = 0;
  }

}

int x_to_ti(int key) {
	switch(key) {
	case GIIUC_a : return TIKEY_A;
	case GIIUC_b : return TIKEY_B;
	case GIIUC_c : return TIKEY_C;
	case GIIUC_d : return TIKEY_D;
	case GIIUC_e : return TIKEY_E;
	case GIIUC_f : return TIKEY_F;
	case GIIUC_g : return TIKEY_G;
	case GIIUC_h : return TIKEY_H;
	case GIIUC_i : return TIKEY_I;
	case GIIUC_j : return TIKEY_J;
	case GIIUC_k : return TIKEY_K;
	case GIIUC_l : return TIKEY_L;
	case GIIUC_m : return TIKEY_M;
	case GIIUC_n : return TIKEY_N;
	case GIIUC_o : return TIKEY_O;
	case GIIUC_p : return TIKEY_P;
	case GIIUC_q : return TIKEY_Q;
	case GIIUC_r : return TIKEY_R;
	case GIIUC_s : return TIKEY_S;
	case GIIUC_t : return TIKEY_T;
	case GIIUC_u : return TIKEY_U;
	case GIIUC_v : return TIKEY_V;
	case GIIUC_w : return TIKEY_W;
	case GIIUC_x : return TIKEY_X;
	case GIIUC_y : return TIKEY_Y;
	case GIIUC_z : return TIKEY_Z;
	case GIIUC_0 : return TIKEY_0;
	case GIIUC_1 : return TIKEY_1;
	case GIIUC_2 : return TIKEY_2;
	case GIIUC_3 : return TIKEY_3;
	case GIIUC_4 : return TIKEY_4;
	case GIIUC_5 : return TIKEY_5;
	case GIIUC_6 : return TIKEY_6;
	case GIIUC_7 : return TIKEY_7;
	case GIIUC_8 : return TIKEY_8;
	case GIIUC_9 : return TIKEY_9;
	case GIIK_P0 : return TIKEY_0;
	case GIIK_P1 : return TIKEY_1;
	case GIIK_P2 : return TIKEY_2;
	case GIIK_P3 : return TIKEY_3;
	case GIIK_P4 : return TIKEY_4;
	case GIIK_P5 : return TIKEY_5;
	case GIIK_P6 : return TIKEY_6;
	case GIIK_P7 : return TIKEY_7;
	case GIIK_P8 : return TIKEY_8;
	case GIIK_P9 : return TIKEY_9;
	case GIIK_Up : return TIKEY_UP;
	case GIIK_Left : return TIKEY_LEFT;
	case GIIK_Right : return TIKEY_RIGHT;
	case GIIK_Down : return TIKEY_DOWN;
	case GIIK_F1 : return TIKEY_F1;
	case GIIK_F2 : return TIKEY_F2;
	case GIIK_F3 : return TIKEY_F3;
	case GIIK_F4 : return TIKEY_F4;
	case GIIK_F5 : return TIKEY_F5;
	case GIIK_F6 : return TIKEY_F6;
	case GIIK_F7 : return TIKEY_F7;
	case GIIK_F8 : return TIKEY_F8;
	case GIIK_Enter : return TIKEY_ENTER1;
	case GIIK_PEnter : return TIKEY_ENTER2;
	case GIIK_Shift : return TIKEY_SHIFT;
	case GIIK_Ctrl : return TIKEY_DIAMOND;
	case GIIK_Alt : return TIKEY_2ND;
	case GIIK_Caps : return TIKEY_HAND;
	case GIIUC_Tab : return TIKEY_STORE;
	case GIIUC_Space : return TIKEY_SPACE;
	case GIIUC_Escape : return TIKEY_ESCAPE;
	case GIIUC_BackSpace : return TIKEY_BACKSPACE;
	case GIIUC_Aring :
	case GIIUC_Udiaeresis: case GIIUC_udiaeresis: 
	case GIIUC_ParenLeft : return TIKEY_PALEFT;
	case GIIUC_Tilde:
	case GIIUC_ParenRight : return TIKEY_PARIGHT;
	case GIIUC_Period : return TIKEY_PERIOD;
	case GIIUC_Comma : return TIKEY_COMMA;
	case GIIK_PPlus :
	case GIIUC_Plus : return TIKEY_PLUS;
	case GIIK_PStar :
	case GIIUC_Star : return TIKEY_MULTIPLY;
	case GIIK_PSlash :
	case GIIUC_Slash : return TIKEY_DIVIDE;
	case GIIK_PMinus :
	case GIIUC_Minus : return TIKEY_MINUS;
	case GIIUC_BackSlash : return TIKEY_PLUS;
	case GIIUC_Semicolon : return TIKEY_THETA;
	case GIIUC_Equal : return TIKEY_EQUALS;
	case GIIUC_Less : return TIKEY_NEGATE;
	case GIIK_PDecimal :
	case GIIK_Insert : return TIKEY_LN;
	case GIIK_Delete : return TIKEY_SIN;
	case GIIK_Home : return TIKEY_CLEAR;
	case GIIK_End : return TIKEY_COS;
	case GIIK_PageUp : return TIKEY_MODE;
	case GIIK_PageDown : return TIKEY_TAN;
	case GIIK_Scroll : return TIKEY_ON;
	case GIIK_F9 : return OPT_DEBUGGER;
	case GIIK_F10 : return OPT_LOADFILE;
	case GIIK_F11 : return OPT_QUIT;
	case GIIK_F12 :
	case GIIK_PrintScreen : return TIKEY_APPS;
	case GIIUC_Circumflex : return TIKEY_POWER;
	default : return TIKEY_NU;
  }
  return 0;
}

int OpenTigerWin(void)
{
  ggi_directbuffer *dbuf;
  ggi_color XPal[4];

  if (ggiInit () != 0)
    return 0;
  v = ggiOpen (NULL);
  if (ggiSetGraphMode (v, WIDTH, HEIGHT, GGI_AUTO, GGI_AUTO, 8))
  {
    ggiClose (v);
    v = ggiOpen ("display-palemu", NULL);
    ggiSetGraphMode (v, WIDTH, HEIGHT, GGI_AUTO, GGI_AUTO, 8);
  }

  if ((dbuf = ggiDBGetBuffer(v, 0)))
  {
    ggiSetFlags (v, GGIFLAG_ASYNC);
    screenBuf = (unsigned long*) dbuf->write;
	using_dbuf = 1;
  }

  else
  {
    screenBuf=(unsigned long *)malloc (WIDTH * HEIGHT);
	using_dbuf = 0;
  }

  if(!screenBuf) {
	  return(0);
  }

  XPal[0].r = XPal[0].g = XPal[0].b = 0x98 << 8;
  XPal[1].r = XPal[1].g = 0x66 << 8; XPal[1].b = 0x77 << 8;
  XPal[2].r = XPal[2].g = 0x33 << 8; XPal[2].b = 0x55 << 8;
  XPal[3].r = XPal[3].g = 0x00 << 8; XPal[3].b = 0x34 << 8;

  ggiSetPalette (v, GGI_PALETTE_DONTCARE, 4, XPal);

  return(1);
}


void CloseTigerWin(void)
{
  ggiClose(v);
}


/** PutImage *************************************************/
/** Put an image on the screen.                             **/
/*************************************************************/
void PutImage(void)
{
  if (!using_dbuf)
    ggiPutBox (v, 0, 0, WIDTH, HEIGHT, screenBuf);
  else
    ggiFlush (v);
}

