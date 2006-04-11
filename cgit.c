/*
 *
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <signal.h>

#include <curses.h>

#define CGIT_HELP "(q)uit, (s)hell, (j) down, (k) up"
#define KEY_ESC	27
#define KEY_TAB	9

/* +------------------------------------+
 * |titlewin				|
 * +------------------------------------+
 * |mainwin				|
 * |					|
 * |					|
 * |					|
 * |					|
 * +------------------------------------+
 * |statuswin				|
 * +------------------------------------+
 */

WINDOW *titlewin;
WINDOW *mainwin;
WINDOW *statuswin;

typedef void (*pipe_filter_T)(char *, int);

FILE *pipe;
long  pipe_lineno;
pipe_filter_T pipe_filter;

/*
 * Init and quit
 */

static void
quit(int sig)
{
	endwin();

	/* do your non-curses wrapup here */

	exit(0);
}

static void
init_colors(void)
{
	int bg = COLOR_BLACK;

	start_color();

	if (use_default_colors())
		bg = -1;

	init_pair(COLOR_BLACK,	 COLOR_BLACK,	bg);
	init_pair(COLOR_GREEN,	 COLOR_GREEN,	bg);
	init_pair(COLOR_RED,	 COLOR_RED,	bg);
	init_pair(COLOR_CYAN,	 COLOR_CYAN,	bg);
	init_pair(COLOR_WHITE,	 COLOR_WHITE,	bg);
	init_pair(COLOR_MAGENTA, COLOR_MAGENTA,	bg);
	init_pair(COLOR_BLUE,	 COLOR_BLUE,	bg);
	init_pair(COLOR_YELLOW,	 COLOR_YELLOW,	bg);
}

static void
init(void)
{
	int x, y;

	signal(SIGINT, quit);

	initscr();      /* initialize the curses library */
	nonl();         /* tell curses not to do NL->CR/NL on output */
	cbreak();       /* take input chars one at a time, no wait for \n */
	noecho();       /* don't echo input */

	if (has_colors())
		init_colors();

	getmaxyx(stdscr, y, x);

	titlewin = newwin(1, 0, 0, 0);

	wattrset(titlewin, COLOR_PAIR(COLOR_GREEN));
	waddch(titlewin, ACS_VLINE);
	wprintw(titlewin, "%s", "cg-view");
	waddch(titlewin, ACS_LTEE);
	whline(titlewin, ACS_HLINE, x);
	wrefresh(titlewin);

	statuswin = newwin(1, 0, y - 1, 0);

	wattrset(statuswin, COLOR_PAIR(COLOR_GREEN));
	wprintw(statuswin, "%s", CGIT_HELP);
	wrefresh(statuswin);

	mainwin = newwin(y - 2, 0, 1, 0);
	scrollok(mainwin, TRUE);
	keypad(mainwin, TRUE);  /* enable keyboard mapping */
}

/*
 * Pipe filters
 */

#define DIFF_CMD	\
	"git-rev-list $(git-rev-parse --since=1.month) HEAD^..HEAD | " \
	"git-diff-tree --stdin --pretty -r --cc --always"


#define LOG_CMD	\
	"git-rev-list $(git-rev-parse --since=1.month) HEAD | " \
	"git-diff-tree --stdin --pretty -r"

static void
log_filter(char *line, int lineno)
{
	static int log_filter_skip;

	if (!line) {
		wattrset(mainwin, A_NORMAL);
		log_filter_skip = 0;
		return;
	}

	if (!strncmp("commit ", line, 7)) {
		attrset(COLOR_PAIR(COLOR_GREEN));

	} else if (!strncmp("Author: ", line, 8)) {
		wattrset(mainwin, COLOR_PAIR(COLOR_CYAN));

	} else if (!strncmp("Date:   ", line, 6)) {
		wattrset(mainwin, COLOR_PAIR(COLOR_YELLOW));

	} else if (!strncmp("diff --git ", line, 11)) {
		wattrset(mainwin, COLOR_PAIR(COLOR_YELLOW));

	} else if (!strncmp("diff-tree ", line, 10)) {
		wattrset(mainwin, COLOR_PAIR(COLOR_BLUE));

	} else if (!strncmp("index ", line, 6)) {
		wattrset(mainwin, COLOR_PAIR(COLOR_BLUE));

	} else if (line[0] == '-') {
		wattrset(mainwin, COLOR_PAIR(COLOR_RED));

	} else if (line[0] == '+') {
		wattrset(mainwin, COLOR_PAIR(COLOR_GREEN));

	} else if (line[0] == '@') {
		wattrset(mainwin, COLOR_PAIR(COLOR_MAGENTA));

	} else if (line[0] == ':') {
		log_filter_skip = 1;
		return;

	} else if (log_filter_skip) {
		log_filter_skip = 0;
		return;

	} else {
		wattrset(mainwin, A_NORMAL);
	}

	mvwaddstr(mainwin, lineno, 0, line);
}

static FILE *
open_pipe(char *cmd, pipe_filter_T filter)
{
	pipe = popen(cmd, "r");
	pipe_lineno = 1;
	pipe_filter = filter;
	return pipe;
}

static void
read_pipe(int lines)
{
	char buffer[BUFSIZ];
	char *line;

	while ((line = fgets(buffer, sizeof(buffer), pipe))) {
		pipe_filter(line, pipe_lineno++);
		if (!--lines)
			break;
	}

	if (feof(pipe) || ferror(pipe)) {
		pipe_filter(NULL, pipe_lineno - 1);
		pclose(pipe);
		pipe = NULL;
		pipe_filter = NULL;
	}
}

/*
 * Main
 */

int
main(int argc, char *argv[])
{
	init();

	pipe = open_pipe(LOG_CMD, log_filter);

	for (;;) {
		int c;

		if (pipe) read_pipe(20);
		if (pipe) nodelay(mainwin, TRUE);

		c = wgetch(mainwin);     /* refresh, accept single keystroke of input */

		if (pipe) nodelay(mainwin, FALSE);

		if (c == ERR)
			continue;

		/* Process the command keystroke */
		switch (c) {
		case KEY_ESC:
		case 'q':
			quit(0);
			return 0;

		case KEY_DOWN:
		case 'j':
			wscrl(mainwin, 1);
			break;

		case KEY_UP:
		case 'k':
			wscrl(mainwin, -1);
			break;

		case 'd':
			pipe = open_pipe(DIFF_CMD, log_filter);
			wclear(mainwin);
			wmove(mainwin, 0, 0);
			break;

		case 'l':
			pipe = open_pipe(LOG_CMD, log_filter);
			wclear(mainwin);
			wmove(mainwin, 0, 0);
			break;

		case 's':
			mvwaddstr(statuswin, 0, 0, "Shelling out......................");
			def_prog_mode();           /* save current tty modes */
			endwin();                  /* restore original tty modes */
			system("sh");              /* run shell */

			wclear(statuswin);
			mvwaddstr(statuswin, 0, 0, CGIT_HELP);
			reset_prog_mode();
			//refresh();                 /* restore save modes, repaint screen */
			break;

/*                default:*/
/*                        if (isprint(c) || isspace(c))*/
/*                                addch(c);*/
		}

		redrawwin(mainwin);
		wrefresh(mainwin);
/*                redrawwin(titlewin);*/
/*                wrefresh(titlewin);*/
/*                redrawwin(statuswin);*/
/*                wrefresh(statuswin);*/
	}

	quit(0);
}

/*                        addch(ACS_LTEE);*/
/*                        addch(ACS_HLINE);*/
