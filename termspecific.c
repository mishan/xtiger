/*
 * Terminal frontend for xtiger.
 *
 * Renders the 240x128 LCD into the terminal using either:
 *  - Unicode braille (2x4 pixels per cell, 120x32 cells) with
 *    per-cell 24-bit color, or
 *  - half-blocks (1x2 pixels per cell, 240x64 cells) with true
 *    per-pixel 24-bit grayscale.
 *
 * The mode is chosen automatically from the terminal size and can
 * be toggled with Ctrl-T.
 *
 * Input quirks are inherent to terminals: there are no key-release
 * events and no bare modifier presses, so:
 *  - a key reads as held for a short while after its last press
 *    (keyboard autorepeat keeps it held)
 *  - uppercase letters press SHIFT+letter
 *  - Alt+key (ESC prefix) presses 2ND+key
 *  - Ctrl+key presses DIAMOND+key
 *  - Ctrl-O is the ON key, Ctrl-Q quits, Ctrl-L forces a redraw
 *
 * F9 enters the debugger, F10 command mode, F11 quits, as in the
 * SDL frontend. Both drop back to the normal terminal screen and
 * return to the emulator display afterwards.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/select.h>

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

#define WIDTH  240
#define HEIGHT 128

#define MAX_PLANES 6

#define ESTAT_SCREENON 1

/* How long a key stays pressed after we see it (ms). Keyboard
 * autorepeat refreshes this, so held keys mostly work. */
#define KEY_HOLD_MS 150

/* Modifiers (SHIFT/2ND/DIAMOND) accompany a key and are released
 * on the same schedule. */

/* Render modes, worst to best. All use 24-bit color. Braille and
 * sextants/quadrants trade resolution for terminal size; quadrants
 * use solid ink (much clearer than braille dots) and are in every
 * font, sextants (Unicode 13 "Symbols for Legacy Computing") are
 * denser but need a modern terminal. */
enum {
  MODE_BRAILLE,     /* 2x4 px/cell, 120x32 cells */
  MODE_SEXTANT,     /* 2x3 px/cell, 120x43 cells */
  MODE_QUADRANT,    /* 2x2 px/cell, 120x64 cells */
  MODE_HALFBLOCK,   /* 1x2 px/cell, 240x64 cells, true per-pixel gray */
  MODE_COUNT
};

static int contrast = 16;
static int lastc = 0;

static int convtab[512];

static UBYTE keyStates[120];
static long keyDeadline[120];   /* ms timestamp when key auto-releases */

static ULONG *screenBuf = NULL;

static int grayPlanes = 2;
static int currPlane = 0;

static ULONG palette[MAX_PLANES + 2]; /* 0xRRGGBB */

static unsigned char *the_screen = NULL;

static int emuState = 0;

static int renderMode = MODE_BRAILLE;
static int forcedMode = -1;      /* -1 = auto */
static int termActive = 0;       /* raw mode + alt screen in effect */
static int needRedraw = 1;

static volatile sig_atomic_t gotWinch = 0;
static volatile sig_atomic_t gotQuit = 0;

static struct termios savedTermios;
static int savedFdFlags = -1;

/* Cache of the previously drawn frame so unchanged rows are skipped.
 * Worst case is a half-block row where every cell changes fg and bg:
 * 240 * (2 * 19 + 3) bytes, so 16K per row is comfortable. */
#define ROWBUF 16384
static char prevRow[64][ROWBUF];

static void PutImage(void);

/* ------------------------------------------------------------------ */
/* Time                                                                */

static long now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

/* ------------------------------------------------------------------ */
/* Terminal state                                                      */

static void term_size(int *cols, int *rows) {
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
  }
  else {
    *cols = 80;
    *rows = 24;
  }
}

static void pick_mode(void) {
  int cols, rows;

  if (forcedMode != -1) {
    renderMode = forcedMode;
    return;
  }
  term_size(&cols, &rows);
  if (cols >= WIDTH && rows >= HEIGHT / 2)
    renderMode = MODE_HALFBLOCK;
  else if (cols >= WIDTH / 2 && rows >= HEIGHT / 2)
    renderMode = MODE_QUADRANT;
  else if (cols >= WIDTH / 2 && rows >= (HEIGHT + 2) / 3)
    renderMode = MODE_SEXTANT;
  else
    renderMode = MODE_BRAILLE;
}

static void term_enter(void) {
  struct termios t;

  if (termActive)
    return;

  tcgetattr(STDIN_FILENO, &savedTermios);
  t = savedTermios;
  t.c_lflag &= ~(ICANON | ECHO | ISIG);
  t.c_iflag &= ~(IXON | ICRNL);
  t.c_cc[VMIN] = 0;
  t.c_cc[VTIME] = 0;
  tcsetattr(STDIN_FILENO, TCSANOW, &t);

  /* VMIN=0 only helps on a tty; if stdin is a pipe, reads must not
   * block the emulation loop */
  savedFdFlags = fcntl(STDIN_FILENO, F_GETFL);
  if (savedFdFlags != -1)
    fcntl(STDIN_FILENO, F_SETFL, savedFdFlags | O_NONBLOCK);

  /* alt screen, hide cursor, clear */
  fputs("\033[?1049h\033[?25l\033[2J", stdout);
  fflush(stdout);

  memset(prevRow, 0, sizeof(prevRow));
  pick_mode();
  termActive = 1;
  needRedraw = 1;
}

static void term_leave(void) {
  if (!termActive)
    return;

  fputs("\033[0m\033[?25h\033[?1049l", stdout);
  fflush(stdout);
  tcsetattr(STDIN_FILENO, TCSANOW, &savedTermios);
  if (savedFdFlags != -1)
    fcntl(STDIN_FILENO, F_SETFL, savedFdFlags);
  termActive = 0;
}

static void on_winch(int sig) {
  gotWinch = 1;
}

static void on_quit(int sig) {
  gotQuit = 1;
}

/* ------------------------------------------------------------------ */
/* Keyboard                                                            */

static void press(int key) {
  if (key < 0 || key >= 120)
    return;
  keyStates[key] = 1;
  keyDeadline[key] = now_ms() + KEY_HOLD_MS;
}

static void press_mod(int mod, int key) {
  press(mod);
  press(key);
}

static void release_expired(void) {
  long t = now_ms();
  int i;
  for (i = 0; i < 120; i++)
    if (keyStates[i] && keyDeadline[i] && t >= keyDeadline[i]) {
      keyStates[i] = 0;
      keyDeadline[i] = 0;
    }
}

/* Map a plain (non-escape) character to a TI key, pressing SHIFT
 * where the character implies it. */
static void press_char(int c) {
  static const int letter[26] = {
    TIKEY_A, TIKEY_B, TIKEY_C, TIKEY_D, TIKEY_E, TIKEY_F, TIKEY_G,
    TIKEY_H, TIKEY_I, TIKEY_J, TIKEY_K, TIKEY_L, TIKEY_M, TIKEY_N,
    TIKEY_O, TIKEY_P, TIKEY_Q, TIKEY_R, TIKEY_S, TIKEY_T, TIKEY_U,
    TIKEY_V, TIKEY_W, TIKEY_X, TIKEY_Y, TIKEY_Z
  };
  static const int digit[10] = {
    TIKEY_0, TIKEY_1, TIKEY_2, TIKEY_3, TIKEY_4,
    TIKEY_5, TIKEY_6, TIKEY_7, TIKEY_8, TIKEY_9
  };

  if (c >= 'a' && c <= 'z') { press(letter[c - 'a']); return; }
  if (c >= 'A' && c <= 'Z') { press_mod(TIKEY_SHIFT, letter[c - 'A']); return; }
  if (c >= '0' && c <= '9') { press(digit[c - '0']); return; }

  switch (c) {
  case '\r': case '\n': press(TIKEY_ENTER1); break;
  case '\t':            press(TIKEY_STORE); break;
  case ' ':             press(TIKEY_SPACE); break;
  case 0x7f: case 0x08: press(TIKEY_BACKSPACE); break;
  case '(': case '[':   press(TIKEY_PALEFT); break;
  case ')': case ']':   press(TIKEY_PARIGHT); break;
  case '.':             press(TIKEY_PERIOD); break;
  case ',':             press(TIKEY_COMMA); break;
  case '+':             press(TIKEY_PLUS); break;
  case '*':             press(TIKEY_MULTIPLY); break;
  case '/':             press(TIKEY_DIVIDE); break;
  case '-':             press(TIKEY_MINUS); break;
  case '\\':            press(TIKEY_PLUS); break;
  case ';':             press(TIKEY_THETA); break;
  case '=':             press(TIKEY_EQUALS); break;
  case '<':             press(TIKEY_NEGATE); break;
  case '^':             press(TIKEY_POWER); break;
  case '`': case '~':   press(TIKEY_PARIGHT); break;
  default: break;
  }
}

/* Read one pending input byte, -1 if none */
static int in_byte(void) {
  unsigned char c;
  if (read(STDIN_FILENO, &c, 1) == 1)
    return c;
  return -1;
}

/* After ESC [, parse the rest of a CSI/SS3 sequence and press the
 * corresponding key. */
static void press_csi(int final, int num) {
  switch (final) {
  case 'A': press(TIKEY_UP); return;
  case 'B': press(TIKEY_DOWN); return;
  case 'C': press(TIKEY_RIGHT); return;
  case 'D': press(TIKEY_LEFT); return;
  case 'H': press(TIKEY_CLEAR); return;   /* Home */
  case 'F': press(TIKEY_COS); return;     /* End */
  case 'P': press(TIKEY_F1); return;      /* SS3 P..S = F1-F4 */
  case 'Q': press(TIKEY_F2); return;
  case 'R': press(TIKEY_F3); return;
  case 'S': press(TIKEY_F4); return;
  case '~':
    switch (num) {
    case 1: press(TIKEY_CLEAR); return;   /* Home */
    case 2: press(TIKEY_LN); return;      /* Insert */
    case 3: press(TIKEY_SIN); return;     /* Delete */
    case 4: press(TIKEY_COS); return;     /* End */
    case 5: press(TIKEY_MODE); return;    /* PageUp */
    case 6: press(TIKEY_TAN); return;     /* PageDown */
    case 11: press(TIKEY_F1); return;
    case 12: press(TIKEY_F2); return;
    case 13: press(TIKEY_F3); return;
    case 14: press(TIKEY_F4); return;
    case 15: press(TIKEY_F5); return;
    case 17: press(TIKEY_F6); return;
    case 18: press(TIKEY_F7); return;
    case 19: press(TIKEY_F8); return;
    case 20: /* F9 */
      enter_debugger();
      return;
    case 21: /* F10 */
      screen_off();
      enter_command();
      return;
    case 23: /* F11 */
      specialflags |= SPCFLAG_BRK;
      return;
    case 24: press(TIKEY_APPS); return;   /* F12 */
    }
    return;
  }
}

/* Handle an escape sequence after the initial ESC byte. Returns
 * having consumed whatever belonged to the sequence. */
static void handle_escape(void) {
  int c = in_byte();

  if (c == -1) {
    /* Lone ESC: give a slow terminal a moment */
    struct timespec ts = { 0, 10 * 1000000L };
    nanosleep(&ts, NULL);
    c = in_byte();
    if (c == -1) {
      press(TIKEY_ESCAPE);
      return;
    }
  }

  if (c == '[' || c == 'O') {
    int num = 0, final = 0, b;
    while ((b = in_byte()) != -1) {
      if (b >= '0' && b <= '9')
        num = num * 10 + (b - '0');
      else if (b == ';')
        num = 0;   /* discard modifier params */
      else {
        final = b;
        break;
      }
    }
    if (final)
      press_csi(final, num);
    return;
  }

  /* ESC + character = Alt + character = TI 2ND */
  press(TIKEY_2ND);
  press_char(c);
}

int update_keys(void) {
  int c;

  if (gotQuit) {
    specialflags |= SPCFLAG_BRK;
    gotQuit = 0;
  }

  if (gotWinch) {
    gotWinch = 0;
    pick_mode();
    memset(prevRow, 0, sizeof(prevRow));
    fputs("\033[2J", stdout);
    needRedraw = 1;
  }

  release_expired();

  while ((c = in_byte()) != -1) {
    if (c == 0x1b) {
      handle_escape();
    }
    else if (c == 0x11) {           /* Ctrl-Q: quit */
      specialflags |= SPCFLAG_BRK;
    }
    else if (c == 0x14) {           /* Ctrl-T: cycle render mode */
      forcedMode = (renderMode + 1) % MODE_COUNT;
      renderMode = forcedMode;
      memset(prevRow, 0, sizeof(prevRow));
      fputs("\033[2J", stdout);
      needRedraw = 1;
    }
    else if (c == 0x0c) {           /* Ctrl-L: redraw */
      memset(prevRow, 0, sizeof(prevRow));
      fputs("\033[2J", stdout);
      needRedraw = 1;
    }
    else if (c == 0x0f) {           /* Ctrl-O: ON key */
      press(TIKEY_ON);
    }
    else if (c == '\r' || c == '\n' || c == '\t' || c == 0x7f || c == 0x08) {
      press_char(c);
    }
    else if (c < 0x20) {
      /* Ctrl + letter = TI DIAMOND + letter */
      press(TIKEY_DIAMOND);
      press_char(c - 1 + 'a');
    }
    else {
      press_char(c);
    }
  }

  return 0;
}

int is_key_pressed(int key) {
  return keyStates[key];
}

/* ------------------------------------------------------------------ */
/* Palette                                                             */

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

    palette[i] = (r << 16) | (g << 8) | b;
  }
  needRedraw = 1;
  memset(prevRow, 0, sizeof(prevRow));
}

void set_contrast(int c) {
  contrast = (c + lastc) / 2;
  lastc = c;
  set_colors();
}

/* ------------------------------------------------------------------ */
/* specific.h plumbing                                                 */

void update_progbar(int size) {
  cmd_update_progbar(size);
}

void link_progress(int type, char *name, int size) {
  cmd_link_progress(type, name, size);
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

  for (i = 0; i < 120; i++) {
    keyStates[i] = 0;
    keyDeadline[i] = 0;
  }

  screenBuf = malloc(WIDTH * HEIGHT);
  if (!screenBuf)
    return 0;
  memset(screenBuf, 0, WIDTH * HEIGHT);

  set_colors();

  signal(SIGWINCH, on_winch);
  signal(SIGINT, on_quit);
  signal(SIGTERM, on_quit);

  return 1;
}

void exit_specific(void) {
  term_leave();
  free(screenBuf);
  screenBuf = NULL;
}

void set_screen_ptr(unsigned char *ptr) {
  the_screen = ptr;
}

/* screen_on/off double as "give the terminal to the emulator" /
 * "give it back to stdio" (command mode, debugger). */
void screen_on(void) {
  emuState |= ESTAT_SCREENON;
  term_enter();
}

void screen_off(void) {
  emuState &= ~ESTAT_SCREENON;
  term_leave();
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

/* ------------------------------------------------------------------ */
/* Rendering                                                           */

static void pace_frame(void) {
  static long nextFrame = 0;
  long now, frameMs;

  if (!globInf.tickRate) {
    nextFrame = 0;
    return;
  }

  frameMs = (long)cycleInstr * 16 * (grayPlanes + 1) / 900;
  if (frameMs < 1)
    frameMs = 1;

  now = now_ms();
  if (nextFrame == 0)
    nextFrame = now;
  nextFrame += frameMs;

  if (now < nextFrame) {
    struct timespec ts;
    long d = nextFrame - now;
    ts.tv_sec = d / 1000;
    ts.tv_nsec = (d % 1000) * 1000000L;
    nanosleep(&ts, NULL);
  }
  else if (now - nextFrame > 250)
    nextFrame = now;
}

static char *emit_color(char *p, ULONG rgb, int bg) {
  return p + sprintf(p, "\033[%d;2;%lu;%lu;%lum",
                     bg ? 48 : 38,
                     (unsigned long)((rgb >> 16) & 0xff),
                     (unsigned long)((rgb >> 8) & 0xff),
                     (unsigned long)(rgb & 0xff));
}

static char *emit_utf8(char *p, unsigned int cp) {
  if (cp < 0x80)
    *p++ = cp;
  else if (cp < 0x800) {
    *p++ = 0xc0 | (cp >> 6);
    *p++ = 0x80 | (cp & 0x3f);
  }
  else if (cp < 0x10000) {
    *p++ = 0xe0 | (cp >> 12);
    *p++ = 0x80 | ((cp >> 6) & 0x3f);
    *p++ = 0x80 | (cp & 0x3f);
  }
  else {
    *p++ = 0xf0 | (cp >> 18);
    *p++ = 0x80 | ((cp >> 12) & 0x3f);
    *p++ = 0x80 | ((cp >> 6) & 0x3f);
    *p++ = 0x80 | (cp & 0x3f);
  }
  return p;
}

/* Braille dot bit for pixel (dx,dy) inside a 2x4 cell */
static const int brbit[4][2] = {
  { 0x01, 0x08 },
  { 0x02, 0x10 },
  { 0x04, 0x20 },
  { 0x40, 0x80 }
};

/* Quadrant characters indexed by TL|TR<<1|BL<<2|BR<<3 */
static const unsigned int quadchar[16] = {
  0x0020, 0x2598, 0x259D, 0x2580, 0x2596, 0x258C, 0x259E, 0x259B,
  0x2597, 0x259A, 0x2590, 0x259C, 0x2584, 0x2599, 0x259F, 0x2588
};

/* Sextant codepoint for a 2x3 bit pattern (row-major, bit0 = top
 * left). U+1FB00.. omits the patterns that already exist elsewhere
 * in Unicode. */
static unsigned int sextchar(int bits) {
  if (bits == 0x00) return 0x0020;
  if (bits == 0x15) return 0x258c;   /* left half */
  if (bits == 0x2a) return 0x2590;   /* right half */
  if (bits == 0x3f) return 0x2588;   /* full block */
  return 0x1fb00 + bits - 1 - (bits > 0x15) - (bits > 0x2a);
}

/* Ink bit index inside a cell for the three sub-cell modes */
static int cell_bit(int mode, int dx, int dy) {
  if (mode == MODE_BRAILLE)
    return brbit[dy][dx];
  if (mode == MODE_SEXTANT)
    return 1 << (dy * 2 + dx);
  return 1 << (dy * 2 + dx);   /* quadrant */
}

/* Generic sub-cell renderer for braille/sextant/quadrant.
 *
 * Each cell is quantized to two gray levels split at the midpoint
 * of its own min/max (block truncation coding, in essence): darker
 * pixels become glyph ink colored with their average gray, lighter
 * ones become the cell background colored likewise. Uniform cells
 * are pure background, so solid regions have no dot/gap speckle,
 * and grayscale content survives as color even where the glyph
 * pattern can't express it. */
static void render_cells(int mode, int cw, int ch) {
  unsigned char *src = (unsigned char *)screenBuf;
  int maxLevel = grayPlanes + 1;
  int cellRows = (HEIGHT + ch - 1) / ch;
  int cellCols = WIDTH / cw;
  int cy, cx, dy, dx;
  int lvl[8];
  char row[ROWBUF], *p;

  for (cy = 0; cy < cellRows; cy++) {
    ULONG lastFg = 0xffffffff, lastBg = 0xffffffff;
    p = row;
    for (cx = 0; cx < cellCols; cx++) {
      int bits = 0;
      int min = 255, max = 0, thresh;
      int inkSum = 0, inkN = 0, bgSum = 0, bgN = 0;
      ULONG fg, bg;

      for (dy = 0; dy < ch; dy++) {
        int y = cy * ch + dy;
        for (dx = 0; dx < cw; dx++) {
          int v = (y < HEIGHT) ? src[y * WIDTH + cx * cw + dx] : 0;
          if (v > maxLevel)
            v = maxLevel;
          lvl[dy * cw + dx] = v;
          if (v < min) min = v;
          if (v > max) max = v;
        }
      }

      if (min == max) {
        /* uniform cell: no glyph, just background */
        bg = palette[min];
        fg = palette[maxLevel];
        bits = 0;
      }
      else {
        thresh = (min + max + 1) / 2;
        for (dy = 0; dy < ch; dy++)
          for (dx = 0; dx < cw; dx++) {
            int v = lvl[dy * cw + dx];
            if (v >= thresh) {
              bits |= cell_bit(mode, dx, dy);
              inkSum += v;
              inkN++;
            }
            else {
              bgSum += v;
              bgN++;
            }
          }
        fg = palette[(inkSum + inkN / 2) / inkN];
        bg = palette[(bgSum + bgN / 2) / bgN];
      }

      if (bg != lastBg) { p = emit_color(p, bg, 1); lastBg = bg; }
      if (bits && fg != lastFg) { p = emit_color(p, fg, 0); lastFg = fg; }

      if (mode == MODE_BRAILLE)
        p = emit_utf8(p, 0x2800 + bits);
      else if (mode == MODE_SEXTANT)
        p = emit_utf8(p, sextchar(bits));
      else
        p = emit_utf8(p, quadchar[bits]);
    }
    *p = 0;

    if (strcmp(row, prevRow[cy])) {
      strcpy(prevRow[cy], row);
      printf("\033[%d;1H%s\033[0m", cy + 1, row);
    }
  }
  fflush(stdout);
}

static void render_halfblock(void) {
  unsigned char *src = (unsigned char *)screenBuf;
  int maxLevel = grayPlanes + 1;
  int cy, x;
  char row[ROWBUF], *p;

  for (cy = 0; cy < HEIGHT / 2; cy++) {
    ULONG lastFg = 0xffffffff, lastBg = 0xffffffff;
    p = row;
    for (x = 0; x < WIDTH; x++) {
      int hi = src[(cy * 2) * WIDTH + x];
      int lo = src[(cy * 2 + 1) * WIDTH + x];
      ULONG fg, bg;
      if (hi > maxLevel) hi = maxLevel;
      if (lo > maxLevel) lo = maxLevel;
      fg = palette[hi];
      bg = palette[lo];
      if (fg != lastFg) { p = emit_color(p, fg, 0); lastFg = fg; }
      if (bg != lastBg) { p = emit_color(p, bg, 1); lastBg = bg; }
      p = emit_utf8(p, 0x2580);   /* upper half block */
    }
    *p = 0;

    if (strcmp(row, prevRow[cy])) {
      strcpy(prevRow[cy], row);
      printf("\033[%d;1H%s\033[0m", cy + 1, row);
    }
  }
  fflush(stdout);
}

static void PutImage(void) {
  if (!termActive)
    return;

  switch (renderMode) {
  case MODE_HALFBLOCK: render_halfblock(); break;
  case MODE_QUADRANT:  render_cells(MODE_QUADRANT, 2, 2); break;
  case MODE_SEXTANT:   render_cells(MODE_SEXTANT, 2, 3); break;
  default:             render_cells(MODE_BRAILLE, 2, 4); break;
  }

  needRedraw = 0;

  pace_frame();
}
