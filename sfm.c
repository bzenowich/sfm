/* See LICENSE file for copyright and license details. */

#if defined(__linux__)
#define _GNU_SOURCE
#elif defined(__APPLE__)
#define _DARWIN_C_SOURCE
#elif defined(__FreeBSD__)
#define __BSD_VISIBLE 1
#endif
#include <sys/types.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#if defined(__linux__)
#include <sys/inotify.h>
#elif defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || \
	defined(__APPLE__)
#include <sys/event.h>
#endif

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <libgen.h>
#include <pthread.h>
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "term.h"
#include "util.h"

/* macros */
#define MAX_P 4096
#define MAX_N 255
#define MAX_USRI 32
#define MAX_EXT 4
#define MAX_STATUS 255
#define MAX_LINE 4096
#define MAX_USRN 32
#define MAX_GRPN 32
#define MAX_DTF 32
#define CURSOR(x) (x)->direntr[(x)->hdir - 1]

/* typedef */
typedef struct {
	char name[MAX_N];
	char real[MAX_P]; // real dir name cwd
	gid_t group;
	mode_t mode;
	off_t size;
	time_t dt;
	uid_t user;
} Entry;

typedef struct {
	//int pane_id;
	char dirn[MAX_P]; // dir name cwd
	//char *filter;
	Entry *direntr; // dir entries
	int dirc; // dir entries sum
	int hdir; // highlighted dir
	int x_srt;
	int x_end;
	int firstrow;
	//int parent_firstrow;
	//int parent_row; // FIX
	Cpair dircol;
	//int inotify_wd;
	//int event_fd;
} Pane;

typedef struct {
	const char **ext;
	size_t exlen;
	const void *v;
	size_t vlen;
} Rule;

typedef union {
	uint16_t key; /* one of the TB_KEY_* constants */
	uint32_t ch; /* unicode character */
} Evkey;

typedef union {
	int i;
	const void *v;
} Arg;

typedef struct {
	const Evkey evkey;
	void (*func)(const Arg *);
	const Arg arg;
} Key;

/* global variables */
static Term *term;
static pthread_t fsev_thread;
static Pane panes[2];
static Pane *cpane;
static int pane_idx;
static char *editor[2];
static char fed[] = "vi"; //TODO rename var
static char *shell[2];
static char sh[] = "/bin/sh";
//static int theight, twidth, hwidth, scrheight;
//static int *sel_indexes;
//static size_t sel_len = 0;
//static char **sel_files;
//static int cont_vmode = 0;
//static int cont_change = 0;
//static pid_t fork_pid = 0;
static pid_t main_pid;
//#if defined(_SYS_INOTIFY_H)
//#define READEVSZ 16
//static int inotify_fd;
//#elif defined(_SYS_EVENT_H_)
//#define READEVSZ 0
//static int kq;
//struct kevent evlist[2]; /* events we want to monitor */
//struct kevent chlist[2]; /* events that were triggered */
//static struct timespec gtimeout;
//#endif
#if defined(__linux__) || defined(__FreeBSD__)
#define OFF_T "%ld"
#elif defined(__NetBSD__) || defined(__OpenBSD__) || defined(__APPLE__)
#define OFF_T "%lld"
#endif
enum { Left, Right }; /* panes */
enum { Wait, DontWait }; /* spawn forks */

/* configuration, allows nested code to access above variables */
#include "config.h"

//static void
//get_hicol(Cpair *col, mode_t mode)
//{
//	switch (mode & S_IFMT) {
//	case S_IFREG:
//		*col = cfile;
//		if ((S_IXUSR | S_IXGRP | S_IXOTH) & mode)
//			*col = cexec;
//		break;
//	case S_IFDIR:
//		*col = cdir;
//		break;
//	case S_IFLNK:
//		*col = clnk;
//		break;
//	case S_IFBLK:
//		*col = cblk;
//		break;
//	case S_IFCHR:
//		*col = cchr;
//		break;
//	case S_IFIFO:
//		*col = cifo;
//		break;
//	case S_IFSOCK:
//		*col = csock;
//		break;
//	default:
//		*col = cother;
//		break;
//	}
//}

//static void
//print_row(Pane *pane, size_t entpos, Cpair col)
//{
//	int x, y;
//	char *full_str, *rez_pth;
//	char lnk_full[MAX_N];
//
//	//int hwidth = pane->x_end - 2;
//	full_str = basename(pane->direntr[entpos].name);
//	x = pane->x_srt;
//	y = entpos - pane->firstrow + 2;
//
//	if (S_ISLNK(pane->direntr[entpos].mode) != 0) {
//		rez_pth = ecalloc(MAX_P, sizeof(char));
//		if (realpath(pane->direntr[entpos].name, rez_pth) != NULL) {
//			snprintf(
//				lnk_full, MAX_N, "%s -> %s", full_str, rez_pth);
//			full_str = lnk_full;
//		}
//		free(rez_pth);
//	}
//
//	//twrite(x, y, full_str, strlen(full_str), col);
//	tprintf(x, y, col, "%*.*s", ~(pane->x_end - 2), pane->x_end - 2,
//		full_str);
//}

//static void
//add_hi(Pane *pane, size_t entpos)
//{
//	Cpair col;
//	get_hicol(&col, pane->direntr[entpos].mode);
//	col.attr = 7;
//	print_row(pane, entpos, col);
//}

static char *
get_fullpath(char *first, char *second)
{
	char *full_path;

	full_path = ecalloc(MAX_P, sizeof(char));

	if (strncmp(first, "/", MAX_P) == 0)
		(void)snprintf(full_path, MAX_P, "/%s", second);
	else
		(void)snprintf(full_path, MAX_P, "%s/%s", first, second);

	return full_path;
}

//static void
//get_dirp(char *cdir)
//{
//	int counter, len, i;
//
//	counter = 0;
//	len = strnlen(cdir, MAX_P);
//	if (len == 1)
//		return;
//
//	for (i = len - 1; i > 1; i--) {
//		if (cdir[i] == '/')
//			break;
//		else
//			counter++;
//	}
//
//	cdir[len - counter - 1] = '\0';
//}
//
//static char *
//get_ext(char *str)
//{
//	char *ext;
//	char dot;
//	size_t counter, len, i;
//
//	dot = '.';
//	counter = 0;
//	len = strnlen(str, MAX_N);
//
//	for (i = len - 1; i > 0; i--) {
//		if (str[i] == dot) {
//			break;
//		} else {
//			counter++;
//		}
//	}
//
//	ext = ecalloc(MAX_EXT + 1, sizeof(char));
//	strncpy(ext, &str[len - counter], MAX_EXT);
//	ext[MAX_EXT] = '\0';
//	return ext;
//}

static int
get_fdt(char *result, time_t status)
{
	struct tm lt;
	localtime_r(&status, &lt);
	return strftime(result, MAX_DTF, dtfmt, &lt);
}

static char *
get_fgrp(gid_t status)
{
	char *result;
	struct group *gr;

	result = ecalloc(MAX_GRPN, sizeof(char));
	gr = getgrgid(status);
	if (gr == NULL)
		(void)snprintf(result, MAX_GRPN, "%u", status);
	else
		strncpy(result, gr->gr_name, MAX_GRPN);

	result[MAX_GRPN - 1] = '\0';
	return result;
}
static char *
get_fperm(mode_t mode)
{
	char *buf;
	size_t i;

	const char chars[] = "rwxrwxrwx";
	buf = ecalloc(11, sizeof(char));

	if (S_ISDIR(mode))
		buf[0] = 'd';
	else if (S_ISREG(mode))
		buf[0] = '-';
	else if (S_ISLNK(mode))
		buf[0] = 'l';
	else if (S_ISBLK(mode))
		buf[0] = 'b';
	else if (S_ISCHR(mode))
		buf[0] = 'c';
	else if (S_ISFIFO(mode))
		buf[0] = 'p';
	else if (S_ISSOCK(mode))
		buf[0] = 's';
	else
		buf[0] = '?';

	for (i = 1; i < 10; i++) {
		buf[i] = (mode & (1 << (9 - i))) ? chars[i - 1] : '-';
	}
	buf[10] = '\0';

	return buf;
}

static char *
get_fsize(off_t size)
{
	char *result; /* need to be freed */
	char unit;
	int result_len;
	int counter;

	counter = 0;
	result_len = 6; /* 9999X/0 */
	result = ecalloc(result_len, sizeof(char));

	while (size >= 1000) {
		size /= 1024;
		++counter;
	}

	switch (counter) {
	case 0:
		unit = 'B';
		break;
	case 1:
		unit = 'K';
		break;
	case 2:
		unit = 'M';
		break;
	case 3:
		unit = 'G';
		break;
	case 4:
		unit = 'T';
		break;
	default:
		unit = '?';
	}

	if (snprintf(result, result_len, OFF_T "%c", size, unit) < 0)
		strncat(result, "???", result_len);

	return result;
}

static char *
get_fusr(uid_t status)
{
	char *result;
	struct passwd *pw;

	result = ecalloc(MAX_USRN, sizeof(char));
	pw = getpwuid(status);
	if (pw == NULL)
		(void)snprintf(result, MAX_USRN, "%u", status);
	else
		strncpy(result, pw->pw_name, MAX_USRN);

	result[MAX_USRN - 1] = '\0';
	return result;
}

//static void
//get_dirsize(char *fullpath, off_t *fullsize)
//{
//	DIR *dir;
//	char *ent_full;
//	mode_t mode;
//	struct dirent *entry;
//	struct stat status;
//
//	dir = opendir(fullpath);
//	if (dir == NULL) {
//		return;
//	}
//
//	while ((entry = readdir(dir)) != 0) {
//		if ((strncmp(entry->d_name, ".", 2) == 0 ||
//			    strncmp(entry->d_name, "..", 3) == 0))
//			continue;
//
//		ent_full = get_fullpath(fullpath, entry->d_name);
//		if (lstat(ent_full, &status) == 0) {
//			mode = status.st_mode;
//			if (S_ISDIR(mode)) {
//				get_dirsize(ent_full, fullsize);
//				free(ent_full);
//			} else {
//				*fullsize += status.st_size;
//				free(ent_full);
//			}
//		}
//	}
//
//	closedir(dir);
//	//clear_status();
//}

//static void
//print_info(Pane *pane, char *dirsize)
//{
//	char *sz, *ur, *gr, *dt, *prm;
//
//	dt = ecalloc(MAX_DTF, sizeof(char));
//
//	prm = get_fperm(CURSOR(pane).mode);
//	ur = get_fusr(CURSOR(pane).user);
//	gr = get_fgrp(CURSOR(pane).group);
//
//	if (get_fdt(dt, CURSOR(pane).dt) < 0)
//		*dt = '\0';
//
//	if (S_ISREG(CURSOR(pane).mode)) {
//		sz = get_fsize(CURSOR(pane).size);
//	} else {
//		if (dirsize == NULL) {
//			sz = ecalloc(1, sizeof(char));
//			*sz = '\0';
//		} else {
//			sz = dirsize;
//		}
//	}
//
//	tprintf_status("%02d/%02d %s %s:%s %s %s", pane->hdir, pane->dirc, prm,
//		ur, gr, dt, sz);
//
//	free(prm);
//	free(ur);
//	free(gr);
//	free(dt);
//	free(sz);
//}

static int
sort_name(const void *const A, const void *const B)
{
	int result;
	mode_t data1 = (*(Entry *)A).mode;
	mode_t data2 = (*(Entry *)B).mode;

	if (data1 < data2) {
		return -1;
	} else if (data1 == data2) {
		result = strncmp((*(Entry *)A).name, (*(Entry *)B).name, MAX_N);
		return result;
	} else {
		return 1;
	}
}

static void
set_direntr(Pane *pane, struct dirent *entry, DIR *dir, char *filter)
{
	int i;
	char *tmpfull;
	struct stat status;

	i = 0;
	pane->direntr =
		erealloc(pane->direntr, (10 + pane->dirc) * sizeof(Entry));
	while ((entry = readdir(dir)) != 0) {
		if (show_dotfiles == 1) {
			if (entry->d_name[0] == '.' &&
				(entry->d_name[1] == '\0' ||
					entry->d_name[1] == '.'))
				continue;
		} else {
			if (entry->d_name[0] == '.')
				continue;
		}

		tmpfull = get_fullpath(pane->dirn, entry->d_name);
		strncpy(pane->direntr[i].name, tmpfull, MAX_N);
		if (lstat(tmpfull, &status) == 0) {
			pane->direntr[i].size = status.st_size;
			pane->direntr[i].mode = status.st_mode;
			pane->direntr[i].group = status.st_gid;
			pane->direntr[i].user = status.st_uid;
			pane->direntr[i].dt = status.st_mtime;
		}
		if (S_ISLNK(status.st_mode) != 0) {
			realpath(tmpfull, pane->direntr[i].real);
		}
		i++;
		free(tmpfull);
	}

	pane->dirc = i;
}

//static void
//refresh_pane(Pane *pane)
//{
//	Cpair col;
//	col.bg=222;
//	col.fg=2;
//	//int x;
//	//char buf[25];
//	size_t buf_srt, buf_end;
//	Entry buf[25];
//
//	buf_srt = 0;
//	buf_end = MIN(term->rows, pane->dirc);
//	//printf("\n\n\n\n");
//	//printf("%d\n", pane->dirc);
//	//printf("%d\n", term->rows);
//	//printf("%zu\n", buf_end);
//
//	memcpy(&buf, pane->direntr, sizeof(buf));
//
//	//printf("%s\n", buf[0].name);
//	//printf("%s\n", buf[1].name);
//	//printf("%s\n", pane->direntr[0].name);
//
//
//	//twrite(2, 2, buf[0], strlen(buf[0]), col);
//	//tprintf(2, 2, col, "%s", (buf[0]).name);
//	//tprintf(2, 3, col, "%s", (buf[1]).name);
//	//tprintf(2, 4, col, "%s", (buf[2]).name);
//
//	//for (x = 0; x < pane->dirc; x++) {
//	//	get_hicol(&col, pane->direntr[x].mode);
//	//	entry = basename(pane->direntr[x].name);
//	//	//if (pane->direntr[x].real[0] != '\0')
//	//		//entry = pane->direntr[x].real;
//	//	//twrite(pane->x_srt, x + 2, entry,
//	//		//pane->direntr[x].real,
//	//		//basename(pane->direntr[x].name),
//	//		//strlen(pane->direntr[x].name), col);
//	//	tprintf(pane->x_srt, x+2, col, "%s (%d) [%d]",
//	//		pane->direntr[x].name,
//	//		strlen((pane->direntr[x].name)),
//	//		sizeof(basename(&pane->direntr[x].name[0])));
//	////free(entry);
//	//}
//
//	//size_t y, dyn_max, start_from;
//	//Cpair col;
//	//col.bg = 0;
//
//	//y = 1;
//	//start_from = pane->firstrow;
//	//dyn_max = MIN(pane->dirc, (term->rows - 1) + pane->firstrow);
//
//	//while (start_from < dyn_max) {
//	//	print_row(pane, start_from, col);
//	//	start_from++;
//	//	y++;
//	//}
//
//	//if (pane->dirc > 0)
//	//	print_info(pane, NULL);
//	//	else
//	//clear_status();
//
//	/* print current directory title */
//	//tprintf(pane->x_srt, 1, pane->dircol, "%.*s", term->avail_cols, pane->dirn); //349,275
//	twrite(pane->x_srt, 0, pane->dirn, strlen(pane->dirn), pane->dircol); //347,862
//}

//static int
//listdir(Pane *pane)
//{
//	DIR *dir;
//	struct dirent *entry;
//
//	pane->dirc = 0;
//
//	dir = opendir(pane->dirn);
//	if (dir == NULL)
//		return -1;
//
//	/* get content and filter sum */
//	while ((entry = readdir(dir)) != 0) {
//		pane->dirc++;
//	}
//
//	rewinddir(dir); /* reset position */
//	set_direntr(pane, entry, dir, NULL); /* create array of entries */
//	qsort(pane->direntr, pane->dirc, sizeof(Entry), sort_name); /*[5971]*/
//	refresh_pane(pane);
//
//	if (pane->hdir > pane->dirc)
//		pane->hdir = pane->dirc;
//
//	//if (pane == cpane && pane->dirc > 0)
//	//add_hi(pane, pane->hdir - 1);
//
//	if (closedir(dir) < 0)
//		return -1;
//	return 0;
//}

static void
set_panes(void)
{
	char *home;
	char cwd[MAX_P];

	home = getenv("HOME");
	if (home == NULL)
		home = "/";
	if ((getcwd(cwd, sizeof(cwd)) == NULL))
		strncpy(cwd, home, MAX_P);

	pane_idx = Left; /* cursor pane */
	cpane = &panes[pane_idx];

	//panes[Left].pane_id = 0;
	panes[Left].x_srt = 2;
	//panes[Left].x_end = (term->cols / 2) - 1;
	panes[Left].dircol = cpanell;
	panes[Left].firstrow = 0;
	panes[Left].direntr = ecalloc(0, sizeof(Entry));
	strncpy(panes[Left].dirn, cwd, MAX_P);
	panes[Left].hdir = 1;
	//panes[Left].inotify_wd = -1;
	//panes[Left].parent_row = 1;

	//panes[Right].pane_id = 1;
	panes[Right].x_srt = (term->cols / 2) + 2;
	//panes[Right].x_end = term->cols - 1;
	panes[Right].dircol = cpanelr;
	panes[Right].firstrow = 0;
	panes[Right].direntr = ecalloc(0, sizeof(Entry));
	strncpy(panes[Right].dirn, home, MAX_P);
	panes[Right].hdir = 1;
	//panes[Right].inotify_wd = -1;
	//panes[Right].parent_row = 1;
}

static void
get_editor(void)
{
	editor[0] = getenv("EDITOR");
	editor[1] = NULL;

	if (editor[0] == NULL)
		editor[0] = fed;
}

static void
get_shell(void)
{
	shell[0] = getenv("SHELL");
	shell[1] = NULL;

	if (shell[0] == NULL)
		shell[0] = sh;
}

void
quit()
{
	//CLEAR_SCREEN
	//CURSOR_SHOW
	//free(panes[Left].direntr);
	//free(panes[Right].direntr);
	reset_term();
	exit(0);
}

void
keypress()
{
	char c = getkey();
	switch (c) {
	case 'q':
		quit();
		break;
	default:
		//tprintf(2, term->rows - 2, {0}, "SIGWINCH = %d\n", 12);
		//twrite(2, term->rows - 2, buf, strlen(buf), col);
		break;
	}
}

//void
//sighandler(int signo)
//{
//	switch (signo) {
//	case SIGWINCH:
//		get_term_size(&term->rows, &term->cols);
//		CLEAR_SCREEN
//		draw_frame();
//		term->avail_cols = (term->cols - 4 ) / 2;
//		tprintf(2, term->rows - 3, cprompt,
//			"123456789012345678901234567890123456789X1234567890123456789012345678901234567890",
//			term->avail_cols, term->cols);
//		tprintf(2, term->rows - 2, cprompt,
//			"avail_cols = %d %d",
//			term->avail_cols, term->cols);
//		refresh_pane(cpane);
//		break;
//	case SIGUSR1:
//		break;
//	case SIGUSR2:
//		break;
//	}
//}

//static int
//start_signal(void)
//{
//	struct sigaction sa;
//	main_pid = getpid();
//	sa.sa_handler = sighandler;
//	sigemptyset(&sa.sa_mask);
//	sa.sa_flags = SA_RESTART;
//	sigaction(SIGUSR1, &sa, 0);
//	sigaction(SIGUSR2, &sa, 0);
//	sigaction(SIGWINCH, &sa, 0);
//	return 0;
//}

void
start(void)
{
	term = init_term();
	//draw_frame();
	//set_panes();
	//get_editor();
	//get_shell();
	//start_signal();
	//PERROR(fsev_init() < 0);
	//listdir(&panes[Left]);
	//listdir(&panes[Right]);

	//pthread_create(&fsev_thread, NULL, read_th, NULL);

	//listdir(&panes[Right]);

	//pthread_create(&fsev_thread, NULL, read_th, NULL);

	//tprintf(2, term->rows - 3, cprompt,
	//	"123456789012345678901234567890"
	//	"12345678901234567890");

	//	tprintf(2, term->rows - 2, cprompt,
	//		"avail_cols = %d %d",
	//		term->avail_cols, term->cols);
	//listdir(&panes[Right]);
	////pthread_create(&fsev_thread, NULL, read_th, NULL);
	while (1) {
		keypress();
	}
	quit();
}

int
main(int argc, char *argv[])
{
#if defined(__OpenBSD__)
	if (pledge("cpath exec getpw proc rpath stdio tmppath tty wpath",
		    NULL) == -1)
		die("pledge");
#endif /* __OpenBSD__ */
	if (argc == 1)
		start();
	else if (argc == 2 && strncmp("-v", argv[1], 2) == 0)
		die("sfm-" VERSION);
	else
		die("usage: sfm [-v]");
	return 0;
}
