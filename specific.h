
/* Init system specific stuff */
extern int init_specific(void);

/* Free system specific stuff */
extern void exit_specific(void);

/* Sets the pointer to the planar 240x128 pixel bitplane (30*128 = 3940 byte) */
extern void set_screen_ptr(unsigned char *ptr);

/* Convert the TI92 planar bitplane to the current display */
extern void update_screen(void);

/* Turn on display */
extern void screen_on(void);

/* Turn off display */
extern void screen_off(void);

/* Update which keys are pressed and not pressed */
extern int update_keys(void);

/* Set the TI92 contrast (between 0-32) */
extern void set_contrast(int);

/* Return if a certain TI92 key was pressed when update_keys() was last called */
extern int is_key_pressed(int);

#define LINK_RUNNING 0
#define LINK_STOP -1
#define LINK_FAIL -2
#define LINK_BACKUP -3
#define LINK_DIR -4
#define LINK_FILE -5

/* Report linking progress for local files
 * type = See above, name = file/varialbe name, size = current size
 *        or size of whole file
 */
extern void link_progress(int type, char *name, int size);

/* Set & Get grayplanes. Returns previous number of planes
 * and sets if pl != -1
 */
extern int gray_planes(int pl);
