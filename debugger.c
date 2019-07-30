
#include <stdlib.h>
#include "sysdeps.h"
#include <stdio.h>
#include <assert.h>
#include <ctype.h>
#include <string.h>
#include "config.h"
#include "options.h"
#include "memory.h"
#include "newcpu.h"
#include "specific.h"
#include "debug.h"

int parsenum(char *s, int n, ULONG *l) {
  char tmp[128];
  long rc;
  int i;

  strcpy(tmp, s);
  s = strtok(tmp, " ,");
  for(i=0; i<n && s; i++)
    s = strtok(NULL, " ,");
  if(s) {
    if(s[0] == '#') {
      int t = strtol(&s[1], NULL, 10);
      if(t<breakPoints) {
	*l = breakAdr[t];
	return 1;
      }
      return 0;
    }
    else
    if(isdigit(s[1]) && (toupper(s[0]) == 'A' || (toupper(s[1]) == 'D'))) {
      if(toupper(s[0]) == 'D')
	*l = regs.d[s[1]-'0'];
      else
	*l = regs.a[s[1]-'0'];
      return 1;
    }
    else
    if(s[0] == '$')
      *l = strtol(&s[1], NULL, 16);
    else
      *l = strtol(s, NULL, 0);
    return 1;
  }
  else
    return 0;
}

extern void lf_strip(char *str);

void enter_debugger(void) {
  char c[32] = "";
  char *ptr;
  long l;
  int i;

  CPTR nextpc, currpc;
  ULONG mempos, len, what, asize;

  int getOut = 0;

  screen_off();
  
  for(i=0; i<breakPoints; i++)
    put_word(breakAdr[i], breakSave[i]);


  printf("---BREAK TO DEBUGGER---%d\n", ti_mem[0x85]);
  currpc = m68k_getpc();
  MC68000_dumpstate(&nextpc);

  while(!getOut) {
    printf("-->");
    fgets(c, 32, stdin);
	lf_strip(c);

    switch(c[0]) {
    case 'b':
      mempos = 0;
      if(parsenum(c, 1, &mempos)) {
	int rem = -1;
	for(i=0; i<breakPoints; i++) {
	  if(breakAdr[i] == mempos)
	    rem = i;
	  if(rem >= 0) {
	    breakAdr[i] = breakAdr[i+1];
	    breakSave[i] = breakSave[i+1];
	  }
	}

	if(rem >= 0) {
	  printf("Breakpoint at 0x%08p removed\n", mempos);
	  breakPoints--;
	}
	else {
	  printf("Breakpoint at 0x%08p added\n", mempos);
	  breakAdr[breakPoints] = mempos;
	  breakSave[breakPoints++] = get_word(mempos);
	}
      }
      else {
	printf("Defined breakpoints:\n");
	for(i=0; i<breakPoints; i++)
	  printf("Break #%d: 0x%08p\n",i, breakAdr[i]);
      }
      break;
    case 't' :
      specialflags |= SPCFLAG_DBTRACE;
      return;
    case 's':
      specialflags |= SPCFLAG_DBSKIP;
      return;
    case 'g' :
      nextpc = currpc;
      parsenum(c, 1, &nextpc);
      m68k_setpc(nextpc);
      screen_on();
      getOut = 1;
      break;
    case 'x' :
      MC68000_dumpstate(&nextpc);
      break;
    case 'q':
      screen_on();
      getOut = 1;
      break;
    case 'h' :
      what = 0;
      mempos = nextpc;
      len = 1024;
      asize = 1;
      if(c[1] == 'w') {
	asize = 2;
	mempos &= 0xfffffffe;
      }
      if(c[1] == 'l') {
	asize = 3;
	mempos &= 0xfffffffe;
      }
      if(parsenum(c, 1, &what)) {
	int found = 0;
	parsenum(c, 2, &mempos);
	parsenum(c, 3, &len);
	printf("Searching for $%x between $%08p and $%08p...\n", what, mempos, mempos+len);
	for(nextpc = mempos ; nextpc < (mempos+len); nextpc += ((asize == 1) ? 1 : 2)) {
	  if(asize == 1)
	    found = (get_byte(nextpc) == what);
	  else
	  if(asize == 2)
	    found = (get_word(nextpc) == what);
	  else
	  if(asize == 3)
	    found = (get_long(nextpc) == what);
	  
	  if(found) {
	    found = 0;
	    printf(" $%08p", nextpc);
	  }
	}
	printf("\n");
      }
      break;
    case 'd':
      mempos = nextpc;
      len = 16;
      parsenum(c, 1, &mempos);
      parsenum(c, 2, &len);
      MC68000_disasm(mempos, &nextpc, len);
      break;
    case 'm':
      mempos = 0;
      len = 16;
      parsenum(c, 1, &mempos);
      parsenum(c, 2, &len);

      ptr = get_real_address(mempos);
      while(len > 0) {
	printf("\n0x%08x:", mempos);
	for(i=0; i<(len > 16 ? 16 : len); i++)
	  printf(" %02x", (*ptr++)&0xff);
	mempos+=16;
	len-=16;
      }
      printf("\n");
    }
  }
  for(i=0; i<breakPoints; i++)
    put_word(breakAdr[i], 0xdfff);

}   
