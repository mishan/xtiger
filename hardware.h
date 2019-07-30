
extern void update_contrast(void);
extern void update_timer(UBYTE arg);
extern void update_bitmap(void);
extern UBYTE read_keyboard_mask(void);
extern UBYTE read_onkey(void);
extern void do_period(void);


extern void link_putbyte(UBYTE arg);
extern UBYTE link_getbyte(void);
extern int link_byteavail(void);
extern void init_hardware(char*);
extern void exit_hardware(void);

#define CYCLES_PER_INSTR 10
#define CYCLES_PER_TICK 6400

extern int cycleInstr;

extern int cycle_count;

extern int onKey;
extern int timerVal;

extern int internalLink;

extern char linkReadName[64];
extern char linkWriteName[64];

static __inline__ void do_cycles(void) {
  if(cycle_count++ >= cycleInstr) {
    do_period();
    cycle_count = 0;
  }
}
