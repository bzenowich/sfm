/* See LICENSE file for copyright and  license details. */

#ifndef TERM_H
#define TERM_H

#define CTRL_KEY(k) ((k)&0x1f)
#define CLEAR_SCREEN write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
#define CLEAR_LINE write(STDOUT_FILENO, "\x1b[2K", 4);
#define CURSOR_HIDE write(STDOUT_FILENO, "\x1b[?25l", 6);
#define CURSOR_SHOW write(STDOUT_FILENO, "\x1b[?25h", 6);


#define NORM 0
#define BOLD 1
#define RVS  7

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

Term* init_term(void);
static void set_term(void);
void reset_term(void);
void draw_frame(void);
//void twrite(int, int, char *, size_t, Cpair);
//void tprintf(int, int, Cpair, const char *, ...);
//void move_to_col(int);
char getkey(void);
static int get_term_size(int *, int *);
//void tprintf_status(const char *fmt, ...);
#endif /* TERM_H */
