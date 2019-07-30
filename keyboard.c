
#include <stdlib.h>
#include "sysdeps.h"
#include <stdio.h>
#include "config.h"
#include "options.h"
#include "debug.h"
#include "memory.h"
#include "newcpu.h"
#include "hardware.h"
#include "packets.h"
#include "specific.h"
#include "keyboard.h"
#include <ctype.h>

int keyRow[10][8] =
{
{TIKEY_DOWN, TIKEY_RIGHT, TIKEY_UP, TIKEY_LEFT, TIKEY_HAND, TIKEY_SHIFT, TIKEY_DIAMOND, TIKEY_2ND},
{TIKEY_3, TIKEY_2, TIKEY_1, TIKEY_F8, TIKEY_W, TIKEY_S, TIKEY_Z, TIKEY_NU},
{TIKEY_6, TIKEY_5, TIKEY_4, TIKEY_F3, TIKEY_E, TIKEY_D, TIKEY_X, TIKEY_NU},
{TIKEY_9, TIKEY_8, TIKEY_7, TIKEY_F7, TIKEY_R, TIKEY_F, TIKEY_C, TIKEY_STORE},
{TIKEY_COMMA, TIKEY_PARIGHT, TIKEY_PALEFT, TIKEY_F2, TIKEY_T, TIKEY_G, TIKEY_V, TIKEY_SPACE},
{TIKEY_TAN, TIKEY_COS, TIKEY_SIN, TIKEY_F6, TIKEY_Y, TIKEY_H, TIKEY_B, TIKEY_DIVIDE},
{TIKEY_P, TIKEY_ENTER2, TIKEY_LN, TIKEY_F1, TIKEY_U, TIKEY_J, TIKEY_N, TIKEY_POWER},
{TIKEY_MULTIPLY, TIKEY_APPS, TIKEY_CLEAR, TIKEY_F5, TIKEY_I, TIKEY_K, TIKEY_M, TIKEY_EQUALS},
{TIKEY_NU, TIKEY_ESCAPE, TIKEY_MODE, TIKEY_PLUS, TIKEY_O, TIKEY_L, TIKEY_THETA, TIKEY_BACKSPACE},
{TIKEY_NEGATE, TIKEY_PERIOD, TIKEY_0, TIKEY_F4, TIKEY_Q, TIKEY_A, TIKEY_ENTER1, TIKEY_MINUS}
};


UBYTE matrixRows[120];
UBYTE matrixCols[120];


void init_keyboard(void) {
  int i,j;
  for(i=0; i<10; i++)
    for(j=0; j<8; j++) {
      matrixRows[keyRow[i][j]] = i;
      matrixCols[keyRow[i][j]] = j;
    }

}


int ti_update_keys(void) {
  int i,k;
  char *fname;

  int rc = update_keys();

  if(onKey = is_key_pressed(TIKEY_ON)) {
    specialflags |= SPCFLAG_INT;
    currIntLev = 6;
  }

  return rc;
}


UBYTE get_rowmask(UBYTE r) {
  UBYTE rc = 0;
  int i;

  for(i=0; i<8; i++)
    rc |= (is_key_pressed(keyRow[r][i])<<(7-i));
  return rc;
}

