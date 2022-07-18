/* See LICENSE file for copyright and  license details. */

#ifndef TERM_H
#define TERM_H

/* macros */
#define CTRL_KEY(k) ((k)&0x1f)
#define CLEAR_SCREEN write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
#define CLEAR_LINE write(STDOUT_FILENO, "\x1b[2K", 4);
#define CURSOR_HIDE write(STDOUT_FILENO, "\x1b[?25l", 6);
#define CURSOR_SHOW write(STDOUT_FILENO, "\x1b[?25h", 6);

#define NORM 0
#define BOLD 1
#define RVS  7

/* typedef */
typedef struct {
	int cx;
	int cy;
	int rows;
	int cols;
	int avail_cols;
	struct termios term;
} Term;

struct abuf {
	char *b;
	int len;
};

typedef struct {
	uint16_t fg;
	uint16_t bg;
	uint8_t attr;
} Cpair;

/* function declarations */
Term* init_term(void);
void quit_term(void);
void draw_frame(void);
char getkey(void);
void twrite(int, int, char *, size_t, Cpair);

#endif /* TERM_H */
