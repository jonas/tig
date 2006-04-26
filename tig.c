/**
 * TIG(1)
 * ======
 *
 * NAME
 * ----
 * tig - text-mode interface for git
 *
 * SYNOPSIS
 * --------
 * [verse]
 * tig
 * tig log  [git log options]
 * tig diff [git diff options]
 * tig < [git log or git diff output]
 *
 * DESCRIPTION
 * -----------
 * Browse changes in a git repository.
 *
 * OPTIONS
 * -------
 *
 * None.
 *
 **/

#define DEBUG

#ifndef DEBUG
#define NDEBUG
#endif

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <assert.h>

#include <curses.h>
#include <form.h>

static void die(const char *err, ...);
static void report(const char *msg, ...);

#define ARRAY_SIZE(x)	(sizeof(x) / sizeof(x[0]))

#define KEY_ESC		27
#define KEY_TAB		9

#define REQ_OFFSET	(MAX_COMMAND + 1)

/* Requests for switching between the different views. */
#define REQ_DIFF	(REQ_OFFSET + 0)
#define REQ_LOG		(REQ_OFFSET + 1)
#define REQ_MAIN	(REQ_OFFSET + 2)

#define REQ_QUIT	(REQ_OFFSET + 11)
#define REQ_VERSION	(REQ_OFFSET + 12)
#define REQ_STOP	(REQ_OFFSET + 13)
#define REQ_UPDATE	(REQ_OFFSET + 14)
#define REQ_REDRAW	(REQ_OFFSET + 15)


/**
 * KEYS
 * ----
 *
 * d::
 *	diff
 * l::
 *	log
 * q::
 *	quit
 * r::
 *	redraw screen
 * s::
 *	stop all background loading
 * j::
 *	down
 * k::
 *	up
 * h, ?::
 *	help
 * v::
 *	version
 *
 **/

#define HELP "(d)iff, (l)og, (m)ain, (q)uit, (v)ersion, (h)elp"

struct keymap {
	int alias;
	int request;
};

struct keymap keymap[] = {
	{ KEY_UP,	REQ_PREV_LINE },
	{ 'k',		REQ_PREV_LINE },
	{ KEY_DOWN,	REQ_NEXT_LINE },
	{ 'j',		REQ_NEXT_LINE },
	{ KEY_NPAGE,	REQ_NEXT_PAGE },
	{ KEY_PPAGE,	REQ_PREV_PAGE },

	{ 'd',		REQ_DIFF },
	{ 'l',		REQ_LOG },
	{ 'm',		REQ_MAIN },

	/* No input from wgetch() with nodelay() enabled. */
	{ ERR,		REQ_UPDATE },

	{ KEY_ESC,	REQ_QUIT },
	{ 'q',		REQ_QUIT },
	{ 's',		REQ_STOP },
	{ 'v',		REQ_VERSION },
	{ 'r',		REQ_REDRAW },
};

static int
get_request(int request)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(keymap); i++)
		if (keymap[i].alias == request)
			return keymap[i].request;

	return request;
}


/*
 * Viewer
 */

struct view {
	char *name;
	char *cmd;

	/* Rendering */
	int (*render)(struct view *, int);
	WINDOW *win;

	/* Navigation */
	unsigned long offset;	/* Offset of the window top */
	unsigned long lineno;	/* Current line number */

	/* Buffering */
	unsigned long lines;	/* Total number of lines */
	char **line;		/* Line index */

	/* Loading */
	FILE *pipe;
};

static int default_renderer(struct view *view, int lineno);

#define DIFF_CMD	\
	"git log --stat -n1 %s ; echo; " \
	"git diff --find-copies-harder -B -C %s^ %s"

#define LOG_CMD	\
	"git log --stat -n100 %s"

/* The status window at the bottom. Used for polling keystrokes. */
static WINDOW *status_win;

static struct view views[] = {
	{ "diff", DIFF_CMD, default_renderer },
	{ "log",  LOG_CMD,  default_renderer },
	{ "main", NULL },
};

static struct view *display[ARRAY_SIZE(views)];
static unsigned int current_view;
static unsigned int nloading;

#define foreach_view(view, i) \
	for (i = 0; i < sizeof(display) && (view = display[i]); i++)

static void
redraw_view(struct view *view)
{
	int lineno;
	int lines, cols;

	wclear(view->win);
	wmove(view->win, 0, 0);

	getmaxyx(view->win, lines, cols);

	for (lineno = 0; lineno < lines; lineno++) {
		view->render(view, lineno);
	}

	redrawwin(view->win);
	wrefresh(view->win);
}

/* FIXME: Fix percentage. */
static void
report_position(struct view *view, int all)
{
	report(all ? "line %d of %d (%d%%) viewing from %d"
		     : "line %d of %d",
	       view->lineno + 1,
	       view->lines,
	       view->lines ? view->offset * 100 / view->lines : 0,
	       view->offset);
}

static void
scroll_view(struct view *view, int request)
{
	int x, y, lines = 1;
	enum { BACKWARD = -1,  FORWARD = 1 } direction = FORWARD;

	getmaxyx(view->win, y, x);

	switch (request) {
	case REQ_NEXT_PAGE:
		lines = y;
	case REQ_NEXT_LINE:
		if (view->offset + lines > view->lines)
			lines = view->lines - view->offset - 1;

		if (lines == 0 || view->offset + y >= view->lines) {
			report("already at last line");
			return;
		}
		break;

	case REQ_PREV_PAGE:
		lines = y;
	case REQ_PREV_LINE:
		if (lines > view->offset)
			lines = view->offset;

		if (lines == 0) {
			report("already at first line");
			return;
		}

		direction = BACKWARD;
		break;

	default:
		lines = 0;
	}

	report("off=%d lines=%d lineno=%d move=%d", view->offset, view->lines, view->lineno, lines * direction);

	/* The rendering expects the new offset. */
	view->offset += lines * direction;

	/* Move current line into the view. */
	if (view->lineno < view->offset)
		view->lineno = view->offset;
	if (view->lineno > view->offset + y)
		view->lineno = view->offset + y;

	assert(0 <= view->offset && view->offset < view->lines);
	//assert(0 <= view->offset + lines && view->offset + lines < view->lines);
	assert(view->offset <= view->lineno && view->lineno <= view->lines);

	if (lines) {
		int from = direction == FORWARD ? y - lines : 0;
		int to	 = from + lines;

		wscrl(view->win, lines * direction);

		for (; from < to; from++) {
			if (!view->render(view, from))
				break;
		}
	}

	redrawwin(view->win);
	wrefresh(view->win);

	report_position(view, lines);
}

static void
resize_view(struct view *view)
{
	int lines, cols;

	getmaxyx(stdscr, lines, cols);

	if (view->win) {
		mvwin(view->win, 0, 0);
		wresize(view->win, lines - 1, cols);

	} else {
		view->win = newwin(lines - 1, 0, 0, 0);
		if (!view->win) {
			report("Failed to create %s view", view->name);
			return;
		}
		scrollok(view->win, TRUE);
	}
}


static bool
begin_update(struct view *view)
{
	char buf[1024];

	if (view->cmd) {
		if (snprintf(buf, sizeof(buf), view->cmd, "HEAD", "HEAD", "HEAD") < sizeof(buf))
			view->pipe = popen(buf, "r");

		if (!view->pipe)
			return FALSE;

		if (nloading++ == 0)
			nodelay(status_win, TRUE);
	}

	display[current_view] = view;

	view->offset = 0;
	view->lines  = 0;
	view->lineno = 0;

	return TRUE;
}

static void
end_update(struct view *view)
{
	wattrset(view->win, A_NORMAL);
	pclose(view->pipe);
	view->pipe = NULL;

	if (nloading-- == 1)
		nodelay(status_win, FALSE);
}

static int
update_view(struct view *view)
{
	char buffer[BUFSIZ];
	char *line;
	int lines, cols;
	char **tmp;
	int redraw;

	if (!view->pipe)
		return TRUE;

	getmaxyx(view->win, lines, cols);

	redraw = !view->line;

	tmp = realloc(view->line, sizeof(*view->line) * (view->lines + lines));
	if (!tmp)
		goto alloc_error;

	view->line = tmp;

	while ((line = fgets(buffer, sizeof(buffer), view->pipe))) {
		int linelen;

		if (!lines--)
			break;

		linelen = strlen(line);
		if (linelen)
			line[linelen - 1] = 0;

		view->line[view->lines] = strdup(line);
		if (!view->line[view->lines])
			goto alloc_error;
		view->lines++;
	}

	if (redraw)
		redraw_view(view);

	if (ferror(view->pipe)) {
		report("Failed to read %s", view->cmd);
		goto end;

	} else if (feof(view->pipe)) {
		report_position(view, 0);
		goto end;
	}

	return TRUE;

alloc_error:
	report("Allocation failure");

end:
	end_update(view);
	return FALSE;
}


static struct view *
switch_view(struct view *prev, int request)
{
	struct view *view = &views[request - REQ_OFFSET];
	struct view *displayed;
	int i;

	if (view == prev) {
		foreach_view (displayed, i) ;

		if (i == 1)
			report("Already in %s view", view->name);
		else
			report("FIXME: Maximize");

		return view;

	} else {
		foreach_view (displayed, i) {
			if (view == displayed) {
				current_view = i;
				report("New current view");
				return view;
			}
		}
	}

	if (!view->win)
		resize_view(view);

	/* Reload */

	if (view->line) {
		for (i = 0; i < view->lines; i++)
			if (view->line[i])
				free(view->line[i]);

		free(view->line);
		view->line = NULL;
	}

	if (prev && prev->pipe)
		end_update(prev);

	if (begin_update(view)) {
		if (!view->cmd)
			report("%s", HELP);
		else
			report("loading...");
	}

	return view;
}


/* Process a keystroke */
static int
view_driver(struct view *view, int key)
{
	int request = get_request(key);
	int i;

	switch (request) {
	case REQ_NEXT_LINE:
	case REQ_NEXT_PAGE:
	case REQ_PREV_LINE:
	case REQ_PREV_PAGE:
		if (view)
			scroll_view(view, request);
		break;

	case REQ_MAIN:
	case REQ_LOG:
	case REQ_DIFF:
		view = switch_view(view, request);
		break;

	case REQ_REDRAW:
		redraw_view(view);
		break;

	case REQ_STOP:
		foreach_view (view, i) {
			if (view->pipe) {
				end_update(view);
				scroll_view(view, 0);
			}
		}
		break;

	case REQ_VERSION:
		report("version %s", VERSION);
		return TRUE;

	case REQ_UPDATE:
		doupdate();
		return TRUE;

	case REQ_QUIT:
		return FALSE;

	default:
		report(HELP);
		return TRUE;
	}

	return TRUE;
}


/*
 * Rendering
 */

#define ATTR(line, attr) { (line), sizeof(line) - 1, (attr) }

struct attr {
	char *line;
	int linelen;
	int attr;
};

static struct attr attrs[] = {
	ATTR("commit ",		COLOR_PAIR(COLOR_GREEN)),
	ATTR("Author: ",	COLOR_PAIR(COLOR_CYAN)),
	ATTR("Date:   ",	COLOR_PAIR(COLOR_YELLOW)),
	ATTR("diff --git ",	COLOR_PAIR(COLOR_YELLOW)),
	ATTR("diff-tree ",	COLOR_PAIR(COLOR_BLUE)),
	ATTR("index ",		COLOR_PAIR(COLOR_BLUE)),
	ATTR("-",		COLOR_PAIR(COLOR_RED)),
	ATTR("+",		COLOR_PAIR(COLOR_GREEN)),
	ATTR("@",		COLOR_PAIR(COLOR_MAGENTA)),
};

static int
default_renderer(struct view *view, int lineno)
{
	char *line;
	int linelen;
	int attr = A_NORMAL;
	int i;

	line = view->line[view->offset + lineno];
	if (!line) return FALSE;

	linelen = strlen(line);

	for (i = 0; i < ARRAY_SIZE(attrs); i++) {
		if (linelen < attrs[i].linelen
		    || strncmp(attrs[i].line, line, attrs[i].linelen))
			continue;

		attr = attrs[i].attr;
		break;
	}

	wattrset(view->win, attr);
	mvwprintw(view->win, lineno, 0, "%4d: %s", view->offset + lineno, line);

	return TRUE;
}

/*
 * Main
 */

static void
quit(int sig)
{
	if (status_win)
		delwin(status_win);
	endwin();

	/* FIXME: Shutdown gracefully. */

	exit(0);
}

static void die(const char *err, ...)
{
	va_list args;

	endwin();

	va_start(args, err);
	fputs("tig: ", stderr);
	vfprintf(stderr, err, args);
	fputs("\n", stderr);
	va_end(args);

	exit(1);
}

static void
report(const char *msg, ...)
{
	va_list args;

	va_start(args, msg);

	werase(status_win);
	wmove(status_win, 0, 0);

	if (display[current_view])
		wprintw(status_win, "%4s: ", display[current_view]->name);

	vwprintw(status_win, msg, args);
	wrefresh(status_win);

	va_end(args);
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

int
main(int argc, char *argv[])
{
	int request = REQ_MAIN;
	int x, y;

	signal(SIGINT, quit);

	initscr();      /* initialize the curses library */
	nonl();         /* tell curses not to do NL->CR/NL on output */
	cbreak();       /* take input chars one at a time, no wait for \n */
	noecho();       /* don't echo input */
	leaveok(stdscr, TRUE);
	/* curs_set(0); */

	if (has_colors())
		init_colors();

	getmaxyx(stdscr, y, x);
	status_win = newwin(1, 0, y - 1, 0);
	if (!status_win)
		die("Failed to create status window");

	/* Enable keyboard mapping */
	keypad(status_win, TRUE);
	wattrset(status_win, COLOR_PAIR(COLOR_GREEN));

	while (view_driver(display[current_view], request)) {
		struct view *view;
		int i;

		foreach_view (view, i) {
			if (view->pipe) {
				update_view(view);
			}
		}

		/* Refresh, accept single keystroke of input */
		request = wgetch(status_win);
		if (request == KEY_RESIZE) {
			int lines, cols;

			getmaxyx(stdscr, lines, cols);
			mvwin(status_win, lines - 1, 0);
			wresize(status_win, 1, cols - 1);
		}
	}

	quit(0);

	return 0;
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
 * link:http://www.kernel.org/pub/software/scm/git/docs/[git(7)],
 * link:http://www.kernel.org/pub/software/scm/cogito/docs/[cogito(7)]
 **/
