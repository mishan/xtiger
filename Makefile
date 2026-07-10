#
# Makefile for xtiger, a TI-92 emulator
#
# The 68k CPU core is generated at build time by build68k/gencpu,
# a process inherited from UAE.
#

CC         ?= cc
PKG_CONFIG ?= pkg-config

SDL_CFLAGS := $(shell $(PKG_CONFIG) --cflags sdl2)
SDL_LIBS   := $(shell $(PKG_CONFIG) --libs sdl2)

OPTFLAGS  ?= -O2 -g
CFLAGS    += $(OPTFLAGS) -std=gnu17 -Wall -Wno-unused
CPPFLAGS  += -I. -Iinclude

PREFIX ?= /usr/local

CPUOBJS = cpu0.o cpu1.o cpu2.o cpu3.o cpu4.o cpu5.o cpu6.o cpu7.o \
          cpu8.o cpu9.o cpuA.o cpuB.o cpuC.o cpuD.o cpuE.o cpuF.o \
          cpustbl.o cpudefs.o

COREOBJS = main.o newcpu.o memory.o debugger.o hardware.o packets.o \
           keyboard.o readcpu.o cmdinterface.o $(CPUOBJS)

all: tiger tiger-term

tiger: $(COREOBJS) sdlspecific.o
	$(CC) $(COREOBJS) sdlspecific.o -o tiger $(LDFLAGS) $(SDL_LIBS)

tiger-term: $(COREOBJS) termspecific.o
	$(CC) $(COREOBJS) termspecific.o -o tiger-term $(LDFLAGS)

sdlspecific.o: sdlspecific.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SDL_CFLAGS) -c $< -o $@

install: tiger tiger-term
	install -d $(DESTDIR)$(PREFIX)/bin
	install -c tiger tiger-term $(DESTDIR)$(PREFIX)/bin/

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/tiger $(DESTDIR)$(PREFIX)/bin/tiger-term

clean:
	-rm -f *.o tiger tiger-term
	-rm -f gencpu build68k cpudefs.c
	-rm -f cpu?.c
	-rm -f cputbl.h cpustbl.c

halfclean:
	-rm -f $(COREOBJS) sdlspecific.o termspecific.o

# --- CPU core generation (inherited from UAE) ------------------------

build68k: build68k.c include/readcpu.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ build68k.c

gencpu: gencpu.c readcpu.c cpudefs.c include/readcpu.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ gencpu.c readcpu.c cpudefs.c

cpudefs.c: build68k table68k
	./build68k >cpudefs.c
cpustbl.c: gencpu
	./gencpu s >cpustbl.c
cputbl.h: gencpu
	./gencpu h >cputbl.h

cpu0.c: gencpu
	./gencpu f 0 >$@
cpu1.c: gencpu
	./gencpu f 1 >$@
cpu2.c: gencpu
	./gencpu f 2 >$@
cpu3.c: gencpu
	./gencpu f 3 >$@
cpu4.c: gencpu
	./gencpu f 4 >$@
cpu5.c: gencpu
	./gencpu f 5 >$@
cpu6.c: gencpu
	./gencpu f 6 >$@
cpu7.c: gencpu
	./gencpu f 7 >$@
cpu8.c: gencpu
	./gencpu f 8 >$@
cpu9.c: gencpu
	./gencpu f 9 >$@
cpuA.c: gencpu
	./gencpu f 10 >$@
cpuB.c: gencpu
	./gencpu f 11 >$@
cpuC.c: gencpu
	./gencpu f 12 >$@
cpuD.c: gencpu
	./gencpu f 13 >$@
cpuE.c: gencpu
	./gencpu f 14 >$@
cpuF.c: gencpu
	./gencpu f 15 >$@

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

# --- Extra dependencies ----------------------------------------------

cpu0.o cpu1.o cpu2.o cpu3.o cpu4.o cpu5.o cpu6.o cpu7.o: cputbl.h
cpu8.o cpu9.o cpuA.o cpuB.o cpuC.o cpuD.o cpuE.o cpuF.o: cputbl.h
cpustbl.o: cputbl.h
readcpu.o: include/readcpu.h

.PHONY: all install uninstall clean halfclean
