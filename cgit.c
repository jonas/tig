/* Cursed git browser
 *
 * Copyright (c) Jonas Fonseca, 2006
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>

#include <curses.h>


#define MSG_HELP "(q)uit, (s)hell, (j) down, (k) up"

#define KEY_ESC	27
#define KEY_TAB	9

//WINDOW *titlewin;
WINDOW *mainwin;
WINDOW *statuswin;

typedef void (*pipe_reader_T)(char *, int);

FILE *pipe;
long  pipe_lineno;
pipe_reader_T pipe_reader;


static void
put_status(char *msg, ...)
{
	va_list args;

	va_start(args, msg);
	werase(statuswin);
	wmove(statuswin, 0, 0);
	vwprintw(statuswin, msg, args);
	wrefresh(statuswin);
	va_end(args);
}

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

	if (use_default_colors() != ERR)
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
	leaveok(stdscr, TRUE);

	if (has_colors())
		init_colors();

	getmaxyx(stdscr, y, x);

#if 0
	titlewin = newwin(1, 0, y - 2, 0);

	wattrset(titlewin, COLOR_PAIR(COLOR_GREEN));
	waddch(titlewin, ACS_VLINE);
	wprintw(titlewin, "%s", "cg-view");
	waddch(titlewin, ACS_LTEE);
	whline(titlewin, ACS_HLINE, x);
	wrefresh(titlewin);
#endif
	statuswin = newwin(1, 0, y - 1, 0);

	wattrset(statuswin, COLOR_PAIR(COLOR_GREEN));
	put_status(MSG_HELP);

	mainwin = newwin(y - 1, 0, 0, 0);
	scrollok(mainwin, TRUE);
	keypad(mainwin, TRUE);  /* enable keyboard mapping */
}

/*
 * Pipe readers
 */

#define DIFF_CMD	\
	"git-rev-list HEAD^..HEAD | " \
	"git-diff-tree --stdin --pretty -r --cc --always --stat"


#define LOG_CMD0 \
	"git-rev-list $(git-rev-parse --since=1.month) HEAD | " \
	"git-diff-tree --stdin --pretty -r --root"

#define LOG_CMD	\
	"git-rev-list HEAD | git-diff-tree --stdin --pretty -r --root"

static void
log_reader(char *line, int lineno)
{
	static int log_reader_skip;

	if (!line) {
		wattrset(mainwin, A_NORMAL);
		log_reader_skip = 0;
		return;
	}

	if (!strncmp("commit ", line, 7)) {
		wattrset(mainwin, COLOR_PAIR(COLOR_GREEN));

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
		pipe_lineno--;
		log_reader_skip = 1;
		return;

	} else if (log_reader_skip) {
		pipe_lineno--;
		log_reader_skip = 0;
		return;

	} else {
		wattrset(mainwin, A_NORMAL);
	}

	mvwaddstr(mainwin, lineno, 0, line);
}

static FILE *
open_pipe(char *cmd, pipe_reader_T reader)
{
	pipe = popen(cmd, "r");
	pipe_lineno = 0;
	pipe_reader = reader;
	wclear(mainwin);
	wmove(mainwin, 0, 0);
	put_status("Loading...");
	return pipe;
}

static void
read_pipe(int lines)
{
	char buffer[BUFSIZ];
	char *line;
	int x, y;

	while ((line = fgets(buffer, sizeof(buffer), pipe))) {
		int linelen;

		if (!--lines)
			break;

		linelen = strlen(line);
		if (linelen)
			line[linelen - 1] = 0;

		pipe_reader(line, pipe_lineno++);
	}

	if (feof(pipe) || ferror(pipe)) {
		pipe_reader(NULL, pipe_lineno - 1);
		pclose(pipe);
		pipe = NULL;
		pipe_reader = NULL;
		put_status("%s (lines %d)", MSG_HELP, pipe_lineno - 1);
	}
}

/*
 * Main
 */

int
main(int argc, char *argv[])
{
	init();

	//pipe = open_pipe(LOG_CMD, log_reader);

	for (;;) {
		int c;

		if (pipe) read_pipe(20);
		if (pipe) nodelay(mainwin, TRUE);

		c = wgetch(mainwin);     /* refresh, accept single keystroke of input */

		if (pipe) nodelay(mainwin, FALSE);

		/* No input from wgetch() with nodelay() enabled. */
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

		case 'c':
			wclear(mainwin);
			break;

		case 'd':
			pipe = open_pipe(DIFF_CMD, log_reader);
			break;

		case 'l':
			pipe = open_pipe(LOG_CMD, log_reader);
			break;

		case 's':
			mvwaddstr(statuswin, 0, 0, "Shelling out...");
			def_prog_mode();           /* save current tty modes */
			endwin();                  /* restore original tty modes */
			system("sh");              /* run shell */

			werase(statuswin);
			mvwaddstr(statuswin, 0, 0, MSG_HELP);
			reset_prog_mode();
			break;
		}

		redrawwin(mainwin);
		wrefresh(mainwin);
	}

	quit(0);
}
