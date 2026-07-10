/*
 * SDL2 frontend for xtiger.
 *
 * Replaces the old GGI frontend (xspecific.c). Runs natively on
 * Wayland and X11 (chosen at runtime by SDL), as well as Windows
 * and macOS.
 *
 * Improvements over the GGI version:
 *  - grayscale palette and LCD contrast actually work (the GGI
 *    version had set_colors() commented out)
 *  - integer-scaled, resizable window
 *  - frame pacing to approximate real TI-92 speed (the old
 *    non-PENT_COUNTER build ran unthrottled)
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "sysdeps.h"
#include "config.h"
#include "options.h"
#include "newcpu.h"
#include "keyboard.h"
#include "hardware.h"
#include "specific.h"
#include "cmdinterface.h"
#include "debug.h"
#include "globinfo.h"

#include <SDL.h>

#define WIDTH  240
#define HEIGHT 128

/* Default window scale factor */
#define DEFAULT_SCALE 3

/* Maximum number of extra gray planes we support */
#define MAX_PLANES 6

#define ESTAT_SCREENON 1

static SDL_Window   *win = NULL;
static SDL_Renderer *ren = NULL;
static SDL_Texture  *tex = NULL;

static int contrast = 16;
static int lastc = 0;

/* Planar->Chunky conversion table: convtab[b*2] holds the first four
 * pixels of byte b packed into one 32-bit word, convtab[b*2+1] the
 * last four. */
static int convtab[512];

/* Keystates for TI92 keys */
static UBYTE keyStates[120];

/* Gray accumulation buffer, one byte per pixel, written four pixels
 * at a time through a ULONG pointer. */
static ULONG *screenBuf = NULL;

static int grayPlanes = 2;
static int currPlane = 0;

static Uint32 palette[MAX_PLANES + 2];

/* Pointer to TI92 planar screen */
static unsigned char *the_screen = NULL;

static int emuState = 0;

static void PutImage(void);
static int sdl_to_ti(SDL_Keycode key);

void update_progbar(int size) {
  cmd_update_progbar(size);
}

void link_progress(int type, char *name, int size) {
  cmd_link_progress(type, name, size);
}

int update_keys(void) {
  SDL_Event ev;

  while (SDL_PollEvent(&ev)) {
    switch (ev.type) {
    case SDL_QUIT:
      specialflags |= SPCFLAG_BRK;
      break;

    case SDL_KEYDOWN:
      if (ev.key.repeat)
        break;
      keyStates[sdl_to_ti(ev.key.keysym.sym)] = 1;
      if (keyStates[OPT_DEBUGGER]) {
        keyStates[OPT_DEBUGGER] = 0;
        enter_debugger();
      }
      else if (keyStates[OPT_QUIT])
        specialflags |= SPCFLAG_BRK;
      else if (keyStates[OPT_LOADFILE]) {
        keyStates[OPT_LOADFILE] = 0;
        screen_off();
        enter_command();
      }
      break;

    case SDL_KEYUP:
      keyStates[sdl_to_ti(ev.key.keysym.sym)] = 0;
      break;
    }
  }

  return 0;
}

int is_key_pressed(int key) {
  return keyStates[key];
}

static int OpenTigerWin(void) {
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    fprintf(stderr, "**Error: SDL_Init failed: %s\n", SDL_GetError());
    return 0;
  }

  win = SDL_CreateWindow("xtiger - TI-92 emulator",
                         SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                         WIDTH * DEFAULT_SCALE, HEIGHT * DEFAULT_SCALE,
                         SDL_WINDOW_RESIZABLE);
  if (!win) {
    fprintf(stderr, "**Error: SDL_CreateWindow failed: %s\n", SDL_GetError());
    return 0;
  }

  ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC);
  if (!ren)
    ren = SDL_CreateRenderer(win, -1, 0);
  if (!ren) {
    fprintf(stderr, "**Error: SDL_CreateRenderer failed: %s\n", SDL_GetError());
    return 0;
  }

  SDL_RenderSetLogicalSize(ren, WIDTH, HEIGHT);
  SDL_RenderSetIntegerScale(ren, SDL_TRUE);

  tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                          SDL_TEXTUREACCESS_STREAMING, WIDTH, HEIGHT);
  if (!tex) {
    fprintf(stderr, "**Error: SDL_CreateTexture failed: %s\n", SDL_GetError());
    return 0;
  }

  screenBuf = malloc(WIDTH * HEIGHT);
  if (!screenBuf)
    return 0;
  memset(screenBuf, 0, WIDTH * HEIGHT);

  set_colors();

  return 1;
}

static void CloseTigerWin(void) {
  free(screenBuf);
  screenBuf = NULL;
  if (tex) SDL_DestroyTexture(tex);
  if (ren) SDL_DestroyRenderer(ren);
  if (win) SDL_DestroyWindow(win);
  SDL_Quit();
}

int init_specific(void) {
  int i, j, k;
  unsigned char *tmptab = (unsigned char *)convtab;

  for (k = 0, i = 0; i < 256; i++) {
    for (j = 7; j >= 0; j--)
      if ((i >> j) & 1)
        tmptab[k++] = 1;
      else
        tmptab[k++] = 0;
  }

  for (i = 0; i < 120; i++)
    keyStates[i] = 0;

  return OpenTigerWin();
}

void exit_specific(void) {
  CloseTigerWin();
}

/* Recompute the palette. Interpolates between globInf.lightCol and
 * globInf.darkCol over grayPlanes+2 levels. The LCD contrast
 * (0..31, ~16 = neutral) scales how quickly the ramp saturates
 * toward dark: low contrast washes the display out, high contrast
 * blackens it. */
void set_colors(void) {
  int i, levels;
  int lr, lg, lb, dr, dg, db;

  if (globInf.grayPlanes >= 0 && globInf.grayPlanes <= MAX_PLANES)
    grayPlanes = globInf.grayPlanes;

  levels = grayPlanes + 2;

  lr = (globInf.lightCol >> 16) & 0xff;
  lg = (globInf.lightCol >> 8) & 0xff;
  lb = globInf.lightCol & 0xff;
  dr = (globInf.darkCol >> 16) & 0xff;
  dg = (globInf.darkCol >> 8) & 0xff;
  db = globInf.darkCol & 0xff;

  for (i = 0; i < levels; i++) {
    int num = i * contrast;
    int den = (levels - 1) * 16;
    int r, g, b;

    if (num > den)
      num = den;

    r = lr + (dr - lr) * num / den;
    g = lg + (dg - lg) * num / den;
    b = lb + (db - lb) * num / den;

    palette[i] = 0xff000000 | (r << 16) | (g << 8) | b;
  }
}

void set_contrast(int c) {
  contrast = (c + lastc) / 2;
  lastc = c;
  set_colors();
}

void screen_on(void) {
  emuState |= ESTAT_SCREENON;
}

void screen_off(void) {
  emuState &= ~ESTAT_SCREENON;
}

void set_screen_ptr(unsigned char *ptr) {
  the_screen = ptr;
}

int gray_planes(int pl) {
  int old = grayPlanes;
  if (pl != -1 && pl >= 0 && pl <= MAX_PLANES) {
    grayPlanes = pl;
    set_colors();
  }
  return old;
}

void update_screen(void) {
  unsigned char *ptr = the_screen;
  int i, j, k;

  if (!(emuState & ESTAT_SCREENON))
    return;

  if (!grayPlanes || !currPlane) {
    for (j = 0, k = 0; k < 128; k++) {
      for (i = 0; i < 30; i++, ptr++) {
        screenBuf[j++] = convtab[(*ptr) << 1];
        screenBuf[j++] = convtab[((*ptr) << 1) + 1];
      }
    }
  }
  else {
    for (j = 0, k = 0; k < 128; k++) {
      for (i = 0; i < 30; i++, ptr++) {
        screenBuf[j++] += convtab[(*ptr) << 1];
        screenBuf[j++] += convtab[((*ptr) << 1) + 1];
      }
    }
  }

  if (currPlane++ >= grayPlanes) {
    PutImage();
    currPlane = 0;
  }
}

/* Pace the emulation so a full grayscale frame takes roughly as long
 * as it would on a real ~10MHz TI-92 (about 900k instructions/sec).
 * One PutImage happens every cycleInstr*16*(grayPlanes+1)
 * instructions. Set "tickrate 0" to run unthrottled. */
static void pace_frame(void) {
  static Uint32 nextFrame = 0;
  Uint32 now, frameMs;

  if (!globInf.tickRate) {
    nextFrame = 0;
    return;
  }

  frameMs = (Uint32)cycleInstr * 16 * (grayPlanes + 1) / 900;
  if (frameMs < 1)
    frameMs = 1;

  now = SDL_GetTicks();
  if (nextFrame == 0)
    nextFrame = now;
  nextFrame += frameMs;

  if (now < nextFrame)
    SDL_Delay(nextFrame - now);
  else if (now - nextFrame > 250)
    nextFrame = now;  /* fell way behind; resync */
}

static void PutImage(void) {
  static Uint32 frame[WIDTH * HEIGHT];
  unsigned char *src = (unsigned char *)screenBuf;
  int maxLevel = grayPlanes + 1;
  int p;

  for (p = 0; p < WIDTH * HEIGHT; p++) {
    int v = src[p];
    if (v > maxLevel)
      v = maxLevel;
    frame[p] = palette[v];
  }

  SDL_UpdateTexture(tex, NULL, frame, WIDTH * sizeof(Uint32));
  SDL_RenderClear(ren);
  SDL_RenderCopy(ren, tex, NULL, NULL);
  SDL_RenderPresent(ren);

  pace_frame();
}

static int sdl_to_ti(SDL_Keycode key) {
  switch (key) {
  case SDLK_a: return TIKEY_A;
  case SDLK_b: return TIKEY_B;
  case SDLK_c: return TIKEY_C;
  case SDLK_d: return TIKEY_D;
  case SDLK_e: return TIKEY_E;
  case SDLK_f: return TIKEY_F;
  case SDLK_g: return TIKEY_G;
  case SDLK_h: return TIKEY_H;
  case SDLK_i: return TIKEY_I;
  case SDLK_j: return TIKEY_J;
  case SDLK_k: return TIKEY_K;
  case SDLK_l: return TIKEY_L;
  case SDLK_m: return TIKEY_M;
  case SDLK_n: return TIKEY_N;
  case SDLK_o: return TIKEY_O;
  case SDLK_p: return TIKEY_P;
  case SDLK_q: return TIKEY_Q;
  case SDLK_r: return TIKEY_R;
  case SDLK_s: return TIKEY_S;
  case SDLK_t: return TIKEY_T;
  case SDLK_u: return TIKEY_U;
  case SDLK_v: return TIKEY_V;
  case SDLK_w: return TIKEY_W;
  case SDLK_x: return TIKEY_X;
  case SDLK_y: return TIKEY_Y;
  case SDLK_z: return TIKEY_Z;
  case SDLK_0: case SDLK_KP_0: return TIKEY_0;
  case SDLK_1: case SDLK_KP_1: return TIKEY_1;
  case SDLK_2: case SDLK_KP_2: return TIKEY_2;
  case SDLK_3: case SDLK_KP_3: return TIKEY_3;
  case SDLK_4: case SDLK_KP_4: return TIKEY_4;
  case SDLK_5: case SDLK_KP_5: return TIKEY_5;
  case SDLK_6: case SDLK_KP_6: return TIKEY_6;
  case SDLK_7: case SDLK_KP_7: return TIKEY_7;
  case SDLK_8: case SDLK_KP_8: return TIKEY_8;
  case SDLK_9: case SDLK_KP_9: return TIKEY_9;
  case SDLK_UP: return TIKEY_UP;
  case SDLK_LEFT: return TIKEY_LEFT;
  case SDLK_RIGHT: return TIKEY_RIGHT;
  case SDLK_DOWN: return TIKEY_DOWN;
  case SDLK_F1: return TIKEY_F1;
  case SDLK_F2: return TIKEY_F2;
  case SDLK_F3: return TIKEY_F3;
  case SDLK_F4: return TIKEY_F4;
  case SDLK_F5: return TIKEY_F5;
  case SDLK_F6: return TIKEY_F6;
  case SDLK_F7: return TIKEY_F7;
  case SDLK_F8: return TIKEY_F8;
  case SDLK_RETURN: return TIKEY_ENTER1;
  case SDLK_KP_ENTER: return TIKEY_ENTER2;
  case SDLK_LSHIFT: case SDLK_RSHIFT: return TIKEY_SHIFT;
  case SDLK_LCTRL: case SDLK_RCTRL: return TIKEY_DIAMOND;
  case SDLK_LALT: case SDLK_RALT: return TIKEY_2ND;
  case SDLK_CAPSLOCK: return TIKEY_HAND;
  case SDLK_TAB: return TIKEY_STORE;
  case SDLK_SPACE: return TIKEY_SPACE;
  case SDLK_ESCAPE: return TIKEY_ESCAPE;
  case SDLK_BACKSPACE: return TIKEY_BACKSPACE;
  case SDLK_LEFTPAREN:
  case SDLK_LEFTBRACKET: return TIKEY_PALEFT;
  case SDLK_RIGHTPAREN:
  case SDLK_RIGHTBRACKET:
  case SDLK_BACKQUOTE: return TIKEY_PARIGHT;
  case SDLK_PERIOD: return TIKEY_PERIOD;
  case SDLK_COMMA: return TIKEY_COMMA;
  case SDLK_PLUS: case SDLK_KP_PLUS: return TIKEY_PLUS;
  case SDLK_ASTERISK: case SDLK_KP_MULTIPLY: return TIKEY_MULTIPLY;
  case SDLK_SLASH: case SDLK_KP_DIVIDE: return TIKEY_DIVIDE;
  case SDLK_MINUS: case SDLK_KP_MINUS: return TIKEY_MINUS;
  case SDLK_BACKSLASH: return TIKEY_PLUS;
  case SDLK_SEMICOLON: return TIKEY_THETA;
  case SDLK_EQUALS: return TIKEY_EQUALS;
  case SDLK_LESS: return TIKEY_NEGATE;
  case SDLK_KP_PERIOD:
  case SDLK_INSERT: return TIKEY_LN;
  case SDLK_DELETE: return TIKEY_SIN;
  case SDLK_HOME: return TIKEY_CLEAR;
  case SDLK_END: return TIKEY_COS;
  case SDLK_PAGEUP: return TIKEY_MODE;
  case SDLK_PAGEDOWN: return TIKEY_TAN;
  case SDLK_SCROLLLOCK: return TIKEY_ON;
  case SDLK_F9: return OPT_DEBUGGER;
  case SDLK_F10: return OPT_LOADFILE;
  case SDLK_F11: return OPT_QUIT;
  case SDLK_F12:
  case SDLK_PRINTSCREEN: return TIKEY_APPS;
  case SDLK_CARET: return TIKEY_POWER;
  default: return TIKEY_NU;
  }
}
