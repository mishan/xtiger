
struct GlobalInformation {
  int grayPlanes;
  char romFile[64];
  char memFile[64];
  ULONG tickRate;
  ULONG cycleRate;
  int itick;
  int memSize;
  char devFile[64];
  char cfgFile[64];
  ULONG darkCol;
  ULONG lightCol;
  int syncOne;
};

extern struct GlobalInformation globInf;

