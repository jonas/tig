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

#include <assert.h>
#include <ctype.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <curses.h>
#include <form.h>

static void die(const char *err, ...);
static void report(const char *msg, ...);

#define ARRAY_SIZE(x)	(sizeof(x) / sizeof(x[0]))

#define KEY_TAB		9
#define KEY_ESC		27
#define KEY_DEL		127

#define REQ_OFFSET	(MAX_COMMAND + 1)

/* Requests for switching between the different views. */
#define REQ_DIFF	(REQ_OFFSET + 0)
#define REQ_LOG		(REQ_OFFSET + 1)
#define REQ_MAIN	(REQ_OFFSET + 2)
#define REQ_VIEWS	(REQ_OFFSET + 3)

#define REQ_QUIT	(REQ_OFFSET + 11)
#define REQ_VERSION	(REQ_OFFSET + 12)
#define REQ_STOP	(REQ_OFFSET + 13)
#define REQ_UPDATE	(REQ_OFFSET + 14)
#define REQ_REDRAW	(REQ_OFFSET + 15)
#define REQ_FIRST_LINE	(REQ_OFFSET + 16)
#define REQ_LAST_LINE	(REQ_OFFSET + 17)

#define COLOR_TRANSP	(-1)


enum line_type {
	LINE_AUTHOR,
	LINE_COMMIT,
	LINE_DATE,
	LINE_DIFF,
	LINE_DIFF_ADD,
	LINE_DIFF_DEL,
	LINE_DIFF_CHUNK,
	LINE_DIFF_TREE,
	LINE_INDEX,
	LINE_PARENT,
	LINE_TREE,

	LINE_UNKNOWN,
	LINE_CURSOR,
	LINE_STATUS,
	LINE_TITLE,
};

struct line_info {
	enum line_type type;
	char *line;
	int linelen;

	int fg;
	int bg;
	int attr;
};

#define LINE(type, line, fg, bg, attr) \
	{ LINE_##type, (line), sizeof(line) - 1, (fg), (bg), (attr) }

static struct line_info line_info[] = {
	LINE(AUTHOR,	 "Author: ",	COLOR_CYAN,	COLOR_TRANSP,	0),
	//LINE(AUTHOR,	 "author ",	COLOR_CYAN,	COLOR_TRANSP,	0),
	LINE(COMMIT,	 "commit ",	COLOR_GREEN,	COLOR_TRANSP,	0),
	LINE(DATE,	 "Date:   ",	COLOR_YELLOW,	COLOR_TRANSP,	0),
	LINE(DIFF_ADD,	 "+",		COLOR_GREEN,	COLOR_TRANSP,	0),
	LINE(DIFF_CHUNK, "@",		COLOR_MAGENTA,	COLOR_TRANSP,	0),
	LINE(DIFF_DEL,	 "-",		COLOR_RED,	COLOR_TRANSP,	0),
	LINE(DIFF,	 "diff --git ",	COLOR_YELLOW,	COLOR_TRANSP,	0),
	LINE(DIFF_TREE,	 "diff-tree ",	COLOR_BLUE,	COLOR_TRANSP,	0),
	LINE(INDEX,	 "index ",	COLOR_BLUE,	COLOR_TRANSP,	0),
	LINE(PARENT,	 "parent ",	COLOR_GREEN,	COLOR_TRANSP,	0),
	LINE(TREE,	 "tree ",	COLOR_GREEN,	COLOR_TRANSP,	0),

	LINE(UNKNOWN,	 "",		COLOR_TRANSP,	COLOR_TRANSP,	A_NORMAL),

	LINE(CURSOR,	 "",		COLOR_WHITE,	COLOR_GREEN,	A_BOLD),
	LINE(STATUS,	 "",		COLOR_GREEN,	COLOR_TRANSP,	0),
	LINE(TITLE,	 "",		COLOR_YELLOW,	COLOR_BLUE,	A_BOLD),
};

static struct line_info *
get_line_info(char *line)
{
	int linelen = strlen(line);
	int i;

	for (i = 0; i < ARRAY_SIZE(line_info); i++) {
		if (linelen < line_info[i].linelen
		    || strncmp(line_info[i].line, line, line_info[i].linelen))
			continue;

		return &line_info[i];
	}

	return NULL;
}

static enum line_type
get_line_type(char *line)
{
	struct line_info *info = get_line_info(line);

	return info ? info->type : LINE_UNKNOWN;
}

static int
get_line_attr(enum line_type type)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(line_info); i++)
		if (line_info[i].type == type)
			return COLOR_PAIR(line_info[i].type) | line_info[i].attr;

	return A_NORMAL;
}

static void
init_colors(void)
{
	int transparent_bg = COLOR_BLACK;
	int transparent_fg = COLOR_WHITE;
	int i;

	start_color();

	if (use_default_colors() != ERR) {
		transparent_bg = -1;
		transparent_fg = -1;
	}

	for (i = 0; i < ARRAY_SIZE(line_info); i++) {
		struct line_info *info = &line_info[i];
		int bg = info->bg == COLOR_TRANSP ? transparent_bg : info->bg;
		int fg = info->fg == COLOR_TRANSP ? transparent_fg : info->fg;

		init_pair(info->type, fg, bg);
	}
}




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
	/* Cursor navigation */
	{ KEY_UP,	REQ_PREV_LINE },
	{ 'k',		REQ_PREV_LINE },
	{ KEY_DOWN,	REQ_NEXT_LINE },
	{ 'j',		REQ_NEXT_LINE },
	{ KEY_HOME,	REQ_FIRST_LINE },
	{ KEY_END,	REQ_LAST_LINE },
	{ KEY_NPAGE,	REQ_NEXT_PAGE },
	{ KEY_PPAGE,	REQ_PREV_PAGE },

	/* Scrolling */
	{ KEY_IC,	REQ_SCR_BLINE }, /* scroll field backward a line */
	{ KEY_DC,	REQ_SCR_FLINE }, /* scroll field forward a line	*/
	{ 's',		REQ_SCR_FPAGE }, /* scroll field forward a page	*/
	{ 'w',		REQ_SCR_BPAGE }, /* scroll field backward a page */

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
	char *id;


	/* Rendering */
	int (*read)(struct view *, char *);
	int (*draw)(struct view *, unsigned int);
	size_t objsize;		/* Size of objects in the line index */

	WINDOW *win;
	int height, width;

	/* Navigation */
	unsigned long offset;	/* Offset of the window top */
	unsigned long lineno;	/* Current line number */

	/* Buffering */
	unsigned long lines;	/* Total number of lines */
	void **line;		/* Line index */

	/* Loading */
	FILE *pipe;
};

struct commit {
	char id[41];
	char title[75];
};

static int pager_draw(struct view *view, unsigned int lineno);
static int pager_read(struct view *view, char *line);

static int main_draw(struct view *view, unsigned int lineno);
static int main_read(struct view *view, char *line);

#define DIFF_CMD \
	"git log --stat -n1 %s ; echo; " \
	"git diff --find-copies-harder -B -C %s^ %s"

#define LOG_CMD	\
	"git log --stat -n100 %s"

#define MAIN_CMD \
	"git log --stat --pretty=raw %s"

/* The status window at the bottom. Used for polling keystrokes. */
static WINDOW *status_win;

static WINDOW *title_win;

#define SIZEOF_ID	1024
#define SIZEOF_VIEWS	(REQ_VIEWS - REQ_OFFSET)

char head_id[SIZEOF_ID] = "HEAD";
char commit_id[SIZEOF_ID] = "HEAD";

static unsigned int current_view;
static unsigned int nloading;

static struct view views[];
static struct view *display[];

static struct view views[] = {
	{ "diff",  DIFF_CMD,   commit_id,  pager_read,  pager_draw, sizeof(char) },
	{ "log",   LOG_CMD,    head_id,    pager_read,  pager_draw, sizeof(char) },
	{ "main",  MAIN_CMD,   head_id,    main_read,   main_draw,  sizeof(struct commit) },
};

static struct view *display[ARRAY_SIZE(views)];


#define foreach_view(view, i) \
	for (i = 0; i < sizeof(display) && (view = display[i]); i++)

static void
redraw_view(struct view *view)
{
	int lineno;

	wclear(view->win);
	wmove(view->win, 0, 0);

	for (lineno = 0; lineno < view->height; lineno++) {
		if (!view->draw(view, lineno))
			break;
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
move_view(struct view *view, int lines)
{
	/* The rendering expects the new offset. */
	view->offset += lines;

	assert(0 <= view->offset && view->offset < view->lines);
	assert(lines);

	{
		int line = lines > 0 ? view->height - lines : 0;
		int end = line + (lines > 0 ? lines : -lines);

		wscrl(view->win, lines);

		for (; line < end; line++) {
			if (!view->draw(view, line))
				break;
		}
	}

	/* Move current line into the view. */
	if (view->lineno < view->offset) {
		view->lineno = view->offset;
		view->draw(view, 0);

	} else if (view->lineno >= view->offset + view->height) {
		view->lineno = view->offset + view->height - 1;
		view->draw(view, view->lineno - view->offset);
	}

	assert(view->offset <= view->lineno && view->lineno <= view->lines);

	redrawwin(view->win);
	wrefresh(view->win);

	report_position(view, lines);
}

static void
scroll_view(struct view *view, int request)
{
	int lines = 1;

	switch (request) {
	case REQ_SCR_FPAGE:
		lines = view->height;
	case REQ_SCR_FLINE:
		if (view->offset + lines > view->lines)
			lines = view->lines - view->offset;

		if (lines == 0 || view->offset + view->height >= view->lines) {
			report("already at last line");
			return;
		}
		break;

	case REQ_SCR_BPAGE:
		lines = view->height;
	case REQ_SCR_BLINE:
		if (lines > view->offset)
			lines = view->offset;

		if (lines == 0) {
			report("already at first line");
			return;
		}

		lines = -lines;
		break;
	}

	move_view(view, lines);
}


static void
navigate_view(struct view *view, int request)
{
	int steps;

	switch (request) {
	case REQ_FIRST_LINE:
		steps = -view->lineno;
		break;

	case REQ_LAST_LINE:
		steps = view->lines - view->lineno - 1;
		break;

	case REQ_PREV_PAGE:
		steps = view->height > view->lineno
		      ? -view->lineno : -view->height;
		break;

	case REQ_NEXT_PAGE:
		steps = view->lineno + view->height >= view->lines
		      ? view->lines - view->lineno - 1 : view->height;
		break;

	case REQ_PREV_LINE:
		steps = -1;
		break;

	case REQ_NEXT_LINE:
		steps = 1;
		break;
	}

	if (steps < 0 && view->lineno == 0) {
		report("already at first line");
		return;

	} else if (steps > 0 && view->lineno + 1 >= view->lines) {
		report("already at last line");
		return;
	}

	view->lineno += steps;
	view->draw(view, view->lineno - steps - view->offset);

	if (view->lineno < view->offset ||
	    view->lineno >= view->offset + view->height) {
		if (steps < 0 && -steps > view->offset) {
			steps = -view->offset;
		} else if (steps > 0 && steps > view->height) {
			steps -= view->height - 1;
		}

		move_view(view, steps);
		return;
	}

	view->draw(view, view->lineno - view->offset);

	redrawwin(view->win);
	wrefresh(view->win);

	report_position(view, view->height);
}

static void
resize_view(struct view *view)
{
	int lines, cols;

	getmaxyx(stdscr, lines, cols);

	if (view->win) {
		mvwin(view->win, 0, 0);
		wresize(view->win, lines - 2, cols);

	} else {
		view->win = newwin(lines - 2, 0, 0, 0);
		if (!view->win) {
			report("failed to create %s view", view->name);
			return;
		}
		scrollok(view->win, TRUE);
	}

	getmaxyx(view->win, view->height, view->width);
}


static bool
begin_update(struct view *view)
{
	char buf[1024];

	if (view->cmd) {
		char *id = view->id;

		if (snprintf(buf, sizeof(buf), view->cmd, id, id, id) < sizeof(buf))
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
	void **tmp;
	int redraw;
	int lines = view->height;

	if (!view->pipe)
		return TRUE;

	/* Only redraw after the first reading session. */
	/* FIXME: ... and possibly more. */
	redraw = view->height > view->lines;

	tmp = realloc(view->line, sizeof(*view->line) * (view->lines + lines));
	if (!tmp)
		goto alloc_error;

	view->line = tmp;

	while ((line = fgets(buffer, sizeof(buffer), view->pipe))) {
		int linelen;

		linelen = strlen(line);
		if (linelen)
			line[linelen - 1] = 0;

		if (!view->read(view, line))
			goto alloc_error;

		if (lines-- == 1)
			break;
	}

	if (redraw)
		redraw_view(view);

	if (ferror(view->pipe)) {
		report("failed to read %s", view->cmd);
		goto end;

	} else if (feof(view->pipe)) {
		report_position(view, 0);
		goto end;
	}

	return TRUE;

alloc_error:
	report("allocation failure");

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
			report("already in %s view", view->name);
		else
			report("FIXME: Maximize");

		return view;

	} else {
		foreach_view (displayed, i) {
			if (view == displayed) {
				current_view = i;
				report("new current view");
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
	case REQ_PREV_LINE:
	case REQ_FIRST_LINE:
	case REQ_LAST_LINE:
	case REQ_NEXT_PAGE:
	case REQ_PREV_PAGE:
		if (view)
			navigate_view(view, request);
		break;

	case REQ_SCR_FLINE:
	case REQ_SCR_BLINE:
	case REQ_SCR_FPAGE:
	case REQ_SCR_BPAGE:
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

static int
pager_draw(struct view *view, unsigned int lineno)
{
	enum line_type type;
	char *line;
	int attr;

	if (view->offset + lineno >= view->lines)
		return FALSE;

	line = view->line[view->offset + lineno];
	type = get_line_type(line);

	if (view->offset + lineno == view->lineno) {
		if (type == LINE_COMMIT)
			strncpy(commit_id, line + 7, SIZEOF_ID);
		type = LINE_CURSOR;
	}

	attr = get_line_attr(type);
	wattrset(view->win, attr);
	//mvwprintw(view->win, lineno, 0, "%4d: %s", view->offset + lineno, line);
	mvwaddstr(view->win, lineno, 0, line);

	return TRUE;
}

static int
pager_read(struct view *view, char *line)
{
	view->line[view->lines] = strdup(line);
	if (!view->line[view->lines])
		return FALSE;

	view->lines++;
	return TRUE;
}

static int
main_draw(struct view *view, unsigned int lineno)
{
	struct commit *commit;
	enum line_type type;

	if (view->offset + lineno >= view->lines)
		return FALSE;

	commit = view->line[view->offset + lineno];
	if (!commit) return FALSE;

	if (view->offset + lineno == view->lineno) {
		strncpy(commit_id, commit->id, SIZEOF_ID);
		type = LINE_CURSOR;
	} else {
		type = LINE_COMMIT;
	}

	mvwaddch(view->win, lineno, 0, ACS_LTEE);
	wattrset(view->win, get_line_attr(type));
	mvwaddstr(view->win, lineno, 2, commit->title);
	wattrset(view->win, A_NORMAL);

	return TRUE;
}

static int
main_read(struct view *view, char *line)
{
	enum line_type type = get_line_type(line);
	struct commit *commit;

	switch (type) {
	case LINE_COMMIT:
		commit = calloc(1, sizeof(struct commit));
		if (!commit)
			return FALSE;

		view->line[view->lines++] = commit;
		strncpy(commit->id, line + 7, 41);
		break;

	default:
		commit = view->line[view->lines - 1];
		if (!commit->title[0] &&
		    !strncmp(line, "    ", 4) &&
		    !isspace(line[5]))
			strncpy(commit->title, line + 4, sizeof(commit->title));
	}

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
	if (title_win)
		delwin(title_win);
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

#if 0
	if (display[current_view])
		wprintw(status_win, "%s %4s: ", commit_id, display[current_view]->name);
#endif
	vwprintw(status_win, msg, args);
	wrefresh(status_win);

	va_end(args);

	werase(title_win);
	wmove(title_win, 0, 0);
	wprintw(title_win, "commit %s", commit_id);
	wrefresh(title_win);

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

	title_win = newwin(1, 0, y - 2, 0);
	if (!title_win)
		die("Failed to create title window");

	/* Enable keyboard mapping */
	keypad(status_win, TRUE);
	wbkgdset(status_win, get_line_attr(LINE_STATUS));
	wbkgdset(title_win, get_line_attr(LINE_TITLE));

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

			mvwin(title_win, lines - 2, 0);
			wresize(title_win, 1, cols - 1);
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
