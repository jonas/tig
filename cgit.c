/**
 * gitzilla(1)
 * ===========
 *
 * NAME
 * ----
 * gitzilla - cursed git browser
 *
 * SYNOPSIS
 * --------
 * gitzilla
 *
 * DESCRIPTION
 * -----------
 *
 * a
 *
 **/

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>

#include <curses.h>

/**
 * OPTIONS
 * -------
 *
 * None
 *
 **/

/**
 * KEYS
 * ----
 *
 * q::	quit
 * s::	shell
 * j::	down
 * k::	up
 *
 **/

#define MSG_HELP "(q)uit, (s)hell, (j) down, (k) up"

#define KEY_ESC	27
#define KEY_TAB	9

struct view {
	WINDOW *win;

	char *cmd;
	void (*reader)(char *, int);
	FILE *pipe;

	unsigned long lines;
	unsigned long lineno;
};

static struct view main_view;
static struct view diff_view;
static struct view log_view;
static struct view status_view;

int do_resize = 1;

static void
put_status(char *msg, ...)
{
	va_list args;

	va_start(args, msg);
	werase(status_view.win);
	wmove(status_view.win, 0, 0);
	vwprintw(status_view.win, msg, args);
	wrefresh(status_view.win);
	va_end(args);
}

static void
resize_views(void)
{
	int x, y;

	getmaxyx(stdscr, y, x);

	if (status_view.win)
		delwin(status_view.win);
	status_view.win = newwin(1, 0, y - 1, 0);

	wattrset(status_view.win, COLOR_PAIR(COLOR_GREEN));
	put_status(MSG_HELP);

	if (main_view.win)
		delwin(main_view.win);
	main_view.win = newwin(y - 1, 0, 0, 0);

	scrollok(main_view.win, TRUE);
	keypad(main_view.win, TRUE);  /* enable keyboard mapping */
	put_status("%d %d", y, x);
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
	signal(SIGINT, quit);

	initscr();      /* initialize the curses library */
	nonl();         /* tell curses not to do NL->CR/NL on output */
	cbreak();       /* take input chars one at a time, no wait for \n */
	noecho();       /* don't echo input */
	leaveok(stdscr, TRUE);
	/* curs_set(0); */

	if (has_colors())
		init_colors();
}

/*
 * Pipe readers
 */

#define DIFF_CMD	\
	"git log --stat -n1 HEAD ; echo; " \
	"git diff --find-copies-harder -B -C HEAD^ HEAD"

#define LOG_CMD	\
	"git log --stat -n100"

static void
log_reader(char *line, int lineno)
{
	static int log_reader_skip;

	if (!line) {
		wattrset(main_view.win, A_NORMAL);
		log_reader_skip = 0;
		return;
	}

	if (!strncmp("commit ", line, 7)) {
		wattrset(main_view.win, COLOR_PAIR(COLOR_GREEN));

	} else if (!strncmp("Author: ", line, 8)) {
		wattrset(main_view.win, COLOR_PAIR(COLOR_CYAN));

	} else if (!strncmp("Date:   ", line, 8)) {
		wattrset(main_view.win, COLOR_PAIR(COLOR_YELLOW));

	} else if (!strncmp("diff --git ", line, 11)) {
		wattrset(main_view.win, COLOR_PAIR(COLOR_YELLOW));

	} else if (!strncmp("diff-tree ", line, 10)) {
		wattrset(main_view.win, COLOR_PAIR(COLOR_BLUE));

	} else if (!strncmp("index ", line, 6)) {
		wattrset(main_view.win, COLOR_PAIR(COLOR_BLUE));

	} else if (line[0] == '-') {
		wattrset(main_view.win, COLOR_PAIR(COLOR_RED));

	} else if (line[0] == '+') {
		wattrset(main_view.win, COLOR_PAIR(COLOR_GREEN));

	} else if (line[0] == '@') {
		wattrset(main_view.win, COLOR_PAIR(COLOR_MAGENTA));

	} else if (line[0] == ':') {
		main_view.lines--;
		log_reader_skip = 1;
		return;

	} else if (log_reader_skip) {
		main_view.lines--;
		log_reader_skip = 0;
		return;

	} else {
		wattrset(main_view.win, A_NORMAL);
	}

	mvwaddstr(main_view.win, lineno, 0, line);
}

static struct view *
update_view(struct view *view, char *cmd)
{
	view->cmd	= cmd;
	view->pipe	= popen(cmd, "r");
	view->lines	= 0;
	view->lineno	= 0;
	view->reader	= log_reader;

	wclear(view->win);
	wmove(view->win, 0, 0);

	put_status("Loading...");

	return view;
}

static struct view *
read_pipe(struct view *view, int lines)
{
	char buffer[BUFSIZ];
	char *line;
	int x, y;

	while ((line = fgets(buffer, sizeof(buffer), view->pipe))) {
		int linelen;

		if (!--lines)
			break;

		linelen = strlen(line);
		if (linelen)
			line[linelen - 1] = 0;

		view->reader(line, view->lines++);
	}

	if (ferror(view->pipe)) {
		put_status("Failed to read %s", view->cmd, view->lines - 1);

	} else if (feof(view->pipe)) {
		put_status("%s (lines %d)", MSG_HELP, view->lines - 1);

	} else {
		return view;
	}

	view->reader(NULL, view->lines - 1);
	pclose(view->pipe);
	view->pipe = NULL;
	view->reader = NULL;
}

/*
 * Main
 */

int
main(int argc, char *argv[])
{
	static struct view *loading_view;

	init();

	//pipe = open_pipe(LOG_CMD, log_reader);

	for (;;) {
		int c;

		if (do_resize) {
			resize_views();
			do_resize = 0;
		}

		if (loading_view && (loading_view = read_pipe(loading_view, 20)))
			nodelay(loading_view->win, TRUE);

		c = wgetch(main_view.win);     /* refresh, accept single keystroke of input */

		if (loading_view)
			nodelay(loading_view->win, FALSE);

		/* No input from wgetch() with nodelay() enabled. */
		if (c == ERR) {
			doupdate();
			continue;
		}

		/* Process the command keystroke */
		switch (c) {
		case KEY_RESIZE:
			fprintf(stderr, "resize");
			exit;
			break;

		case KEY_ESC:
		case 'q':
			quit(0);
			main_view.lineno--;
			return 0;

		case KEY_DOWN:
		case 'j':
		{
			int x, y;

			getmaxyx(main_view.win, y, x);
			if (main_view.lineno + y < main_view.lines) {
				wscrl(main_view.win, 1);
				main_view.lineno++;
				put_status("line %d out of %d (%d%%)",
					   main_view.lineno,
					   main_view.lines,
					   100 * main_view.lineno / main_view.lines);
			} else {
				put_status("last line reached");
			}
			break;
		}
		case KEY_UP:
		case 'k':
			if (main_view.lineno > 1) {
				wscrl(main_view.win, -1);
				main_view.lineno--;
				put_status("line %d out of %d (%d%%)",
					   main_view.lineno,
					   main_view.lines,
					   100 * main_view.lineno / main_view.lines);
			} else {
				put_status("first line reached");
			}
			break;

		case 'c':
			wclear(main_view.win);
			break;

		case 'd':
			loading_view = update_view(&main_view, DIFF_CMD);
			break;

		case 'l':
			loading_view = update_view(&main_view, LOG_CMD);
			break;

		case 's':
			mvwaddstr(status_view.win, 0, 0, "Shelling out...");
			def_prog_mode();           /* save current tty modes */
			endwin();                  /* restore original tty modes */
			system("sh");              /* run shell */

			werase(status_view.win);
			mvwaddstr(status_view.win, 0, 0, MSG_HELP);
			reset_prog_mode();
			break;
		}

		redrawwin(main_view.win);
		wrefresh(main_view.win);
	}

	quit(0);
}

/**
 * COPYRIGHT
 * ---------
 * Copyright (c) Jonas Fonseca, 2006
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * SEE ALSO
 * --------
 * gitlink:cogito[7],
 * gitlink:git[7]
 **/
