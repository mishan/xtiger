# Generated automatically from Makefile.in by configure.
#
# Makefile.in for UAE
#

CC        = gcc
CPP       = gcc -E
CFLAGS    = -O1 -fomit-frame-pointer -Wall -Wno-unused -Wno-format -DX86_ASSEMBLY -D__inline__=inline -DSTATFS_NO_ARGS=2 -DSTATBUF_BAVAIL=f_bavail -g
XLIBRARIES = -lggi
.SUFFIXES: .o .c .h .m

INCLUDES=-Iinclude
PREFIX=/usr/local

OBJS = main.o newcpu.o memory.o debugger.o hardware.o packets.o keyboard.o \
       readcpu.o cpudefs.o cpu0.o cpu1.o cpu2.o cpu3.o cpu4.o cpu5.o cpu6.o \
       cpu7.o cpu8.o cpu9.o cpuA.o cpuB.o cpuC.o cpuD.o cpuE.o cpuF.o \
       cpustbl.o cmdinterface.o
       
tiger: $(OBJS) xspecific.o
	$(CC) $(OBJS) xspecific.o -o tiger $(LDFLAGS) $(XLIBRARIES)

install:
	@echo Installing 'tiger' to $(PREFIX)/bin ...
	install -c tiger $(PREFIX)/bin/
uninstall:
	rm -f $(PREFIX)/bin/tiger

clean:
	-rm -f *.o tiger
	-rm -f gencpu build68k cpudefs.c
	-rm -f cpu?.c blit.h
	-rm -f cputbl.h cpustbl.c


halfclean:
	-rm -f $(OBJS)

build68k: build68k.o
	$(CC) $(LDFLAGS) -o $@ $?
gencpu: gencpu.o readcpu.o cpudefs.o
	$(CC) $(LDFLAGS) -o $@ gencpu.o readcpu.o cpudefs.o

cpudefs.c: build68k table68k
	./build68k >cpudefs.c
cpustbl.c: gencpu
	./gencpu s >cpustbl.c
cputbl.c: gencpu
	./gencpu t >cputbl.c
cputbl.h: gencpu
	./gencpu h >cputbl.h

cpu0.c: gencpu
	./gencpu f 0 >cpu0.c
cpu1.c: gencpu
	./gencpu f 1 >cpu1.c
cpu2.c: gencpu
	./gencpu f 2 >cpu2.c
cpu3.c: gencpu
	./gencpu f 3 >cpu3.c
cpu4.c: gencpu
	./gencpu f 4 >cpu4.c
cpu5.c: gencpu
	./gencpu f 5 >cpu5.c
cpu6.c: gencpu
	./gencpu f 6 >cpu6.c
cpu7.c: gencpu
	./gencpu f 7 >cpu7.c
cpu8.c: gencpu
	./gencpu f 8 >cpu8.c
cpu9.c: gencpu
	./gencpu f 9 >cpu9.c
cpuA.c: gencpu
	./gencpu f 10 >cpuA.c
cpuB.c: gencpu
	./gencpu f 11 >cpuB.c
cpuC.c: gencpu
	./gencpu f 12 >cpuC.c
cpuD.c: gencpu
	./gencpu f 13 >cpuD.c
cpuE.c: gencpu
	./gencpu f 14 >cpuE.c
cpuF.c: gencpu
	./gencpu f 15 >cpuF.c

cpu0.o: cpu0.c cputbl.h
	$(CC) $(INCLUDES) -c $(INCDIRS) $(CFLAGS) $*.c
cpu1.o: cpu1.c cputbl.h
	$(CC) $(INCLUDES) -c $(INCDIRS) $(CFLAGS) $*.c
cpu2.o: cpu2.c cputbl.h
	$(CC) $(INCLUDES) -c $(INCDIRS) $(CFLAGS) $*.c
cpu3.o: cpu3.c cputbl.h
	$(CC) $(INCLUDES) -c $(INCDIRS) $(CFLAGS) $*.c
cpu4.o: cpu4.c cputbl.h
	$(CC) $(INCLUDES) -c $(INCDIRS) $(CFLAGS) $*.c
cpu5.o: cpu5.c cputbl.h
	$(CC) $(INCLUDES) -c $(INCDIRS) $(CFLAGS) $*.c
cpu6.o: cpu6.c cputbl.h
	$(CC) $(INCLUDES) -c $(INCDIRS) $(CFLAGS) $*.c
cpu7.o: cpu7.c cputbl.h
	$(CC) $(INCLUDES) -c $(INCDIRS) $(CFLAGS) $*.c
cpu8.o: cpu8.c cputbl.h
	$(CC) $(INCLUDES) -c $(INCDIRS) $(CFLAGS) $*.c
cpu9.o: cpu9.c cputbl.h
	$(CC) $(INCLUDES) -c $(INCDIRS) $(CFLAGS) $*.c
cpuA.o: cpuA.c cputbl.h
	$(CC) $(INCLUDES) -c $(INCDIRS) $(CFLAGS) $*.c
cpuB.o: cpuB.c cputbl.h
	$(CC) $(INCLUDES) -c $(INCDIRS) $(CFLAGS) $*.c
cpuC.o: cpuC.c cputbl.h
	$(CC) $(INCLUDES) -c $(INCDIRS) $(CFLAGS) $*.c
cpuD.o: cpuD.c cputbl.h
	$(CC) $(INCLUDES) -c $(INCDIRS) $(CFLAGS) $*.c
cpuE.o: cpuE.c cputbl.h
	$(CC) $(INCLUDES) -c $(INCDIRS) $(CFLAGS) $*.c
cpuF.o: cpuF.c cputbl.h
	$(CC) $(INCLUDES) -c $(INCDIRS) $(CFLAGS) $*.c
       
.m.o:
	$(CC) $(INCLUDES) -c $(INCDIRS) $(CFLAGS) $*.m
.c.o:
	$(CC) $(INCLUDES) -c $(INCDIRS) $(CFLAGS) $*.c
.c.s:
	$(CC) $(INCLUDES) -S $(INCDIRS) $(CFLAGS) $*.c
.S.o:
	$(CC) $(INCLUDES) -c $(INCDIRS) $(CFLAGS) $*.S

sceen.o : screen.h

# Some more dependencies...
cpustbl.o: cputbl.h
cputbl.o: cputbl.h

build68k.o: include/readcpu.h
readcpu.o: include/readcpu.h


