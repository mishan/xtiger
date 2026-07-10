
extern void cmd_update_progbar(int size);
extern void cmd_link_progress(int type, char *name, int size);
extern void enter_command(void);
extern void load_cfg_file(char *name);
extern void save_cfg_file(char *name);
extern int do_command(char *cmd, char *arg1);
extern void prompt_commands(void);
