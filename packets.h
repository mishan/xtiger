
typedef struct {
  char name[10];
  UBYTE type;
  int size;
} tifile;

extern int init_protocol(void);
extern void reset_protocol(void);
extern void prot_receive_byte(UBYTE arg);
extern UBYTE prot_get_byte(void);
extern void start_send_backup(UBYTE *ptr, int size);
extern void start_send_variable(UBYTE *var, char *name, UBYTE type, int size);
extern void start_get_directory(tifile *buffer , int size);
extern int prot_bytes_left(void);
extern int send_ti_file(char *fname, UBYTE *backBuf);
extern int query_libs(char *fname, char *libBuf, char **libsPtrs);

void receive_packet(int type, int size, UBYTE *data);
void resend_lastpacket(void);
void send_packet(int type, int size, UBYTE *data);
void send_varheader(int vartype, int size, char *name);


#define PKT_READY 0x56
#define PKT_EXPECT 0x09
#define PKT_EOD 0x92
#define PKT_VARDATA 0x15
#define PKT_VARHEAD 0x06
#define PKT_VARREQ 0xA2
#define PKT_CONT 0x78

#define ACT_VARHEAD 1
#define ACT_EOF 2
#define ACT_VARDATA 3
#define ACT_NOACTION 4
#define ACT_CONTEOD 5

#define TXM_BACKUP 1
#define TXM_SENDVAR 2
#define TXM_GETDIR 3

#define VARTYPE_BACKUP 0x1d

