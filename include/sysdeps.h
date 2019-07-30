/* Set ULONG, LONG, UWORD, WORD, UBYTE & BYTE
 * 
 */

#define SIZEOF_SHORT 2
#define SIZEOF_INT 4
#define SIZEOF_LONG 4

/* If char has more then 8 bits, good night. */
typedef unsigned char UBYTE;
typedef signed char BYTE;

#if SIZEOF_SHORT == 2
typedef unsigned short UWORD;
typedef short WORD;
#elif SIZEOF_INT == 2
typedef unsigned int UWORD;
typedef int WORD;
#else
#error No 2 byte type, you lose.
#endif

#if SIZEOF_INT == 4
typedef unsigned int ULONG;
typedef int LONG;
#elif SIZEOF_LONG == 4
typedef unsigned long ULONG;
typedef long LONG;
#else
#error No 4 byte type, you lose.
#endif
typedef ULONG CPTR;



