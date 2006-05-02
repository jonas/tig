/* Copyright (c) 2006 Jonas Fonseca <fonseca@diku.dk>
 * See license info at the bottom. */
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
 * tig [options]
 * tig [options] log  [git log options]
 * tig [options] diff [git diff options]
 * tig [options] <    [git log or git diff output]
 *
 * DESCRIPTION
 * -----------
 * Browse changes in a git repository.
 **/

#ifndef DEBUG
#define NDEBUG
#endif

#ifndef	VERSION
#define VERSION	"tig-0.1"
#endif

#include <assert.h>
#include <errno.h>
#include <ctype.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <curses.h>

static void die(const char *err, ...);
static void report(const char *msg, ...);

#define SIZEOF_REF	256	/* Size of symbolic or SHA1 ID. */
#define SIZEOF_CMD	1024	/* Size of command buffer. */

/* This color name can be used to refer to the default term colors. */
#define COLOR_DEFAULT	(-1)

/* The format and size of the date column in the main view. */
#define DATE_FORMAT	"%Y-%m-%d %H:%M"
#define DATE_COLS	(STRING_SIZE("2006-04-29 14:21 "))

/* The default interval between line numbers. */
#define NUMBER_INTERVAL	1

#define	SCALE_SPLIT_VIEW(height)	((height) * 2 / 3)

#define ABS(x)		((x) >= 0 ? (x) : -(x))
#define MIN(x, y)	((x) < (y) ? (x) : (y))

#define ARRAY_SIZE(x)	(sizeof(x) / sizeof(x[0]))
#define STRING_SIZE(x)	(sizeof(x) - 1)

/* Some ascii-shorthands that fit into the ncurses namespace. */
#define KEY_TAB		'\t'
#define KEY_RETURN	'\r'
#define KEY_ESC		27

/* User action requests. */
enum request {
	/* Offset all requests to avoid conflicts with ncurses getch values. */
	REQ_OFFSET = KEY_MAX + 1,

	/* XXX: Keep the view request first and in sync with views[]. */
	REQ_VIEW_MAIN,
	REQ_VIEW_DIFF,
	REQ_VIEW_LOG,
	REQ_VIEW_HELP,

	REQ_QUIT,
	REQ_SHOW_VERSION,
	REQ_SHOW_COMMIT,
	REQ_STOP_LOADING,
	REQ_SCREEN_REDRAW,
	REQ_SCREEN_UPDATE,
	REQ_TOGGLE_LINE_NUMBERS,

	REQ_MOVE_UP,
	REQ_MOVE_DOWN,
	REQ_MOVE_PAGE_UP,
	REQ_MOVE_PAGE_DOWN,
	REQ_MOVE_FIRST_LINE,
	REQ_MOVE_LAST_LINE,

	REQ_SCROLL_LINE_UP,
	REQ_SCROLL_LINE_DOWN,
	REQ_SCROLL_PAGE_UP,
	REQ_SCROLL_PAGE_DOWN,
};

struct commit {
	char id[41];		/* SHA1 ID. */
	char title[75];		/* The first line of the commit message. */ 
	char author[75];	/* The author of the commit. */
	struct tm time;		/* Date from the author ident. */
};


static inline void
string_ncopy(char *dst, char *src, int dstlen)
{
	strncpy(dst, src, dstlen - 1);
	dst[dstlen - 1] = 0;
}

/* Shorthand for safely copying into a fixed buffer. */
#define string_copy(dst, src) \
	string_ncopy(dst, src, sizeof(dst))


/**
 * OPTIONS
 * -------
 **/

static int opt_line_number	= FALSE;
static int opt_num_interval	= NUMBER_INTERVAL;
static int opt_request		= REQ_VIEW_MAIN;

char ref_head[SIZEOF_REF]	= "HEAD";
char ref_commit[SIZEOF_REF]	= "HEAD";

/* Returns the index of log or diff command or -1 to exit. */
static int
parse_options(int argc, char *argv[])
{
	int i;

	for (i = 1; i < argc; i++) {
		char *opt = argv[i];

		/**
		 * log [options]::
		 *	git log options.
		 *
		 * diff [options]::
		 *	git diff options.
		 **/
		if (!strcmp(opt, "log") ||
		    !strcmp(opt, "diff")) {
			opt_request = opt[0] == 'l'
				    ? REQ_VIEW_LOG : REQ_VIEW_DIFF;
			return i;

		/**
		 * -l::
		 *	Start up in log view.
		 **/
		} else if (!strcmp(opt, "-l")) {
			opt_request = REQ_VIEW_LOG;

		/**
		 * -d::
		 *	Start up in diff view.
		 **/
		} else if (!strcmp(opt, "-d")) {
			opt_request = REQ_VIEW_DIFF;

		/**
		 * -n[INTERVAL], --line-number[=INTERVAL]::
		 *	Prefix line numbers in log and diff view.
		 *	Optionally, with interval different than each line.
		 **/
		} else if (!strncmp(opt, "-n", 2) ||
		           !strncmp(opt, "--line-number", 13)) {
			char *num = opt;

			if (opt[1] == 'n') {
				num = opt + 2;

			} else if (opt[STRING_SIZE("--line-number")] == '=') {
				num = opt + STRING_SIZE("--line-number=");
			}

			if (isdigit(*num))
				opt_num_interval = atoi(num);

			opt_line_number = 1;

		/**
		 * -v, --version::
		 *	Show version and exit.
		 **/
		} else if (!strcmp(opt, "-v") ||
			   !strcmp(opt, "--version")) {
			printf("tig version %s\n", VERSION);
			return -1;

		/**
		 * ref::
		 *	Commit reference, symbolic or raw SHA1 ID.
		 **/
		} else if (opt[0] && opt[0] != '-') {
			string_copy(ref_head, opt);
			string_copy(ref_commit, opt);

		} else {
			die("unknown command '%s'", opt);
		}
	}

	return i;
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
 **/

#define HELP "(d)iff, (l)og, (m)ain, (q)uit, (v)ersion, (h)elp"

struct keymap {
	int alias;
	int request;
};

struct keymap keymap[] = {
	/* Cursor navigation */
	{ KEY_UP,	REQ_MOVE_UP },
	{ 'k',		REQ_MOVE_UP },
	{ KEY_DOWN,	REQ_MOVE_DOWN },
	{ 'j',		REQ_MOVE_DOWN },
	{ KEY_HOME,	REQ_MOVE_FIRST_LINE },
	{ KEY_END,	REQ_MOVE_LAST_LINE },
	{ KEY_NPAGE,	REQ_MOVE_PAGE_DOWN },
	{ KEY_PPAGE,	REQ_MOVE_PAGE_UP },

	/* Scrolling */
	{ KEY_IC,	REQ_SCROLL_LINE_UP },
	{ KEY_DC,	REQ_SCROLL_LINE_DOWN },
	{ 'w',		REQ_SCROLL_PAGE_UP },
	{ 's',		REQ_SCROLL_PAGE_DOWN },

	{ KEY_RETURN,	REQ_SHOW_COMMIT },

	/* View switching */
	{ 'm',		REQ_VIEW_MAIN },
	{ 'd',		REQ_VIEW_DIFF },
	{ 'l',		REQ_VIEW_LOG },
	{ 'h',		REQ_VIEW_HELP },

	/* Misc */
	{ KEY_ESC,	REQ_QUIT },
	{ 'q',		REQ_QUIT },
	{ 'z',		REQ_STOP_LOADING },
	{ 'v',		REQ_SHOW_VERSION },
	{ 'r',		REQ_SCREEN_REDRAW },
	{ 'n',		REQ_TOGGLE_LINE_NUMBERS },

	/* wgetch() with nodelay() enabled returns ERR when there's no input. */
	{ ERR,		REQ_SCREEN_UPDATE },
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
 * Line-oriented content detection.
 *
 *   Line type	   String to match	Foreground	Background	Attributes
 *   ---------     ---------------      ----------      ----------      ---------- */
#define LINE_INFO \
/* Diff markup */ \
LINE(DIFF,	   "diff --git ",	COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(INDEX,	   "index ",		COLOR_BLUE,	COLOR_DEFAULT,	0), \
LINE(DIFF_CHUNK,   "@@",		COLOR_MAGENTA,	COLOR_DEFAULT,	0), \
LINE(DIFF_ADD,	   "+",			COLOR_GREEN,	COLOR_DEFAULT,	0), \
LINE(DIFF_DEL,	   "-",			COLOR_RED,	COLOR_DEFAULT,	0), \
LINE(DIFF_OLDMODE, "old mode ",		COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(DIFF_NEWMODE, "new mode ",		COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(DIFF_COPY,	   "copy ",		COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(DIFF_RENAME,  "rename ",		COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(DIFF_SIM,	   "similarity ",	COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(DIFF_DISSIM,  "dissimilarity ",	COLOR_YELLOW,	COLOR_DEFAULT,	0), \
/* Pretty print commit header */ \
LINE(AUTHOR,	   "Author: ",		COLOR_CYAN,	COLOR_DEFAULT,	0), \
LINE(MERGE,	   "Merge: ",		COLOR_BLUE,	COLOR_DEFAULT,	0), \
LINE(DATE,	   "Date:   ",		COLOR_YELLOW,	COLOR_DEFAULT,	0), \
/* Raw commit header */ \
LINE(COMMIT,	   "commit ",		COLOR_GREEN,	COLOR_DEFAULT,	0), \
LINE(PARENT,	   "parent ",		COLOR_BLUE,	COLOR_DEFAULT,	0), \
LINE(TREE,	   "tree ",		COLOR_BLUE,	COLOR_DEFAULT,	0), \
LINE(AUTHOR_IDENT, "author ",		COLOR_CYAN,	COLOR_DEFAULT,	0), \
LINE(COMMITTER,	   "committer ",	COLOR_MAGENTA,	COLOR_DEFAULT,	0), \
/* Misc */ \
LINE(DIFF_TREE,	   "diff-tree ",	COLOR_BLUE,	COLOR_DEFAULT,	0), \
LINE(SIGNOFF,	   "    Signed-off-by", COLOR_YELLOW,	COLOR_DEFAULT,	0), \
/* UI colors */ \
LINE(DEFAULT,	   "",			COLOR_DEFAULT,	COLOR_DEFAULT,	A_NORMAL), \
LINE(CURSOR,	   "",			COLOR_WHITE,	COLOR_GREEN,	A_BOLD), \
LINE(STATUS,	   "",			COLOR_GREEN,	COLOR_DEFAULT,	0), \
LINE(TITLE,	   "",			COLOR_WHITE,	COLOR_BLUE,	0), \
LINE(MAIN_DATE,    "",			COLOR_BLUE,	COLOR_DEFAULT,	0), \
LINE(MAIN_AUTHOR,  "",			COLOR_GREEN,	COLOR_DEFAULT,	0), \
LINE(MAIN_COMMIT,  "",			COLOR_DEFAULT,	COLOR_DEFAULT,	0), \
LINE(MAIN_DELIM,   "",			COLOR_MAGENTA,	COLOR_DEFAULT,	0),

enum line_type {
#define LINE(type, line, fg, bg, attr) \
	LINE_##type
	LINE_INFO
#undef	LINE
};

struct line_info {
	char *line;		/* The start of line to match. */
	int linelen;		/* Size of string to match. */
	int fg, bg, attr;	/* Color and text attributes for the lines. */
};

static struct line_info line_info[] = {
#define LINE(type, line, fg, bg, attr) \
	{ (line), STRING_SIZE(line), (fg), (bg), (attr) }
	LINE_INFO
#undef	LINE
};

static enum line_type
get_line_type(char *line)
{
	int linelen = strlen(line);
	enum line_type type;

	for (type = 0; type < ARRAY_SIZE(line_info); type++)
		/* Case insensitive search matches Signed-off-by lines better. */
		if (linelen >= line_info[type].linelen &&
		    !strncasecmp(line_info[type].line, line, line_info[type].linelen))
			return type;

	return LINE_DEFAULT;
}

static inline int
get_line_attr(enum line_type type)
{
	assert(type < ARRAY_SIZE(line_info));
	return COLOR_PAIR(type) | line_info[type].attr;
}

static void
init_colors(void)
{
	int default_bg = COLOR_BLACK;
	int default_fg = COLOR_WHITE;
	enum line_type type;

	start_color();

	if (use_default_colors() != ERR) {
		default_bg = -1;
		default_fg = -1;
	}

	for (type = 0; type < ARRAY_SIZE(line_info); type++) {
		struct line_info *info = &line_info[type];
		int bg = info->bg == COLOR_DEFAULT ? default_bg : info->bg;
		int fg = info->fg == COLOR_DEFAULT ? default_fg : info->fg;

		init_pair(type, fg, bg);
	}
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

	char cmdbuf[SIZEOF_CMD];

	WINDOW *win;
	WINDOW *title;
	int height, width;

	/* Navigation */
	unsigned long offset;	/* Offset of the window top */
	unsigned long lineno;	/* Current line number */

	/* Buffering */
	unsigned long lines;	/* Total number of lines */
	void **line;		/* Line index */

	/* Loading */
	FILE *pipe;
	time_t start_time;
};

static int pager_draw(struct view *view, unsigned int lineno);
static int pager_read(struct view *view, char *line);

static int main_draw(struct view *view, unsigned int lineno);
static int main_read(struct view *view, char *line);

static void update_title(struct view *view);

#define DIFF_CMD \
	"git log --stat -n1 %s ; echo; " \
	"git diff --find-copies-harder -B -C %s^ %s"

#define LOG_CMD	\
	"git log --cc --stat -n100 %s"

#define MAIN_CMD \
	"git log --stat --pretty=raw %s"

#define HELP_CMD \
	"man tig 2> /dev/null"

/* The status window is used for polling keystrokes. */
static WINDOW *status_win;

/* The number of loading views. Controls when nodelay should be in effect when
 * polling user input. */
static unsigned int nloading;

static struct view views[] = {
	{ "main",  MAIN_CMD,   ref_head,    main_read,   main_draw,  sizeof(struct commit) },
	{ "diff",  DIFF_CMD,   ref_commit,  pager_read,  pager_draw, sizeof(char) },
	{ "log",   LOG_CMD,    ref_head,    pager_read,  pager_draw, sizeof(char) },
	{ "help",  HELP_CMD,   ref_head,    pager_read,  pager_draw, sizeof(char) },
};

#define VIEW(req) (&views[(req) - REQ_OFFSET - 1])

/* The display array of active views and the index of the current view. */
static struct view *display[ARRAY_SIZE(views)];
static unsigned int current_view;

#define foreach_view(view, i) \
	for (i = 0; i < sizeof(display) && (view = display[i]); i++)


static void
redraw_view_from(struct view *view, int lineno)
{
	assert(0 <= lineno && lineno < view->height);

	for (; lineno < view->height; lineno++) {
		if (!view->draw(view, lineno))
			break;
	}

	redrawwin(view->win);
	wrefresh(view->win);
}

static void
redraw_view(struct view *view)
{
	wclear(view->win);
	redraw_view_from(view, 0);
}

static struct view *
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
			report("Failed to create %s view", view->name);
			return NULL;
		}
		scrollok(view->win, TRUE);

		view->title = newwin(1, 0, lines - 2, 0);
		if (!view->title) {
			delwin(view->win);
			view->win = NULL;
			report("Failed to create title window");
			return NULL;
		}
		wbkgdset(view->title, get_line_attr(LINE_TITLE));

	}

	getmaxyx(view->win, view->height, view->width);

	return view;
}


/*
 * Navigation
 */

/* Scrolling backend */
static void
do_scroll_view(struct view *view, int lines)
{
	/* The rendering expects the new offset. */
	view->offset += lines;

	assert(0 <= view->offset && view->offset < view->lines);
	assert(lines);

	/* Redraw the whole screen if scrolling is pointless. */
	if (view->height < ABS(lines)) {
		redraw_view(view);

	} else {
		int line = lines > 0 ? view->height - lines : 0;
		int end = line + ABS(lines);

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

	assert(view->offset <= view->lineno && view->lineno < view->lines);

	redrawwin(view->win);
	wrefresh(view->win);
	report("");
}

/* Scroll frontend */
static void
scroll_view(struct view *view, int request)
{
	int lines = 1;

	switch (request) {
	case REQ_SCROLL_PAGE_DOWN:
		lines = view->height;
	case REQ_SCROLL_LINE_DOWN:
		if (view->offset + lines > view->lines)
			lines = view->lines - view->offset;

		if (lines == 0 || view->offset + view->height >= view->lines) {
			report("Already at last line");
			return;
		}
		break;

	case REQ_SCROLL_PAGE_UP:
		lines = view->height;
	case REQ_SCROLL_LINE_UP:
		if (lines > view->offset)
			lines = view->offset;

		if (lines == 0) {
			report("Already at first line");
			return;
		}

		lines = -lines;
		break;
	}

	do_scroll_view(view, lines);
}

/* Cursor moving */
static void
move_view(struct view *view, int request)
{
	int steps;

	switch (request) {
	case REQ_MOVE_FIRST_LINE:
		steps = -view->lineno;
		break;

	case REQ_MOVE_LAST_LINE:
		steps = view->lines - view->lineno - 1;
		break;

	case REQ_MOVE_PAGE_UP:
		steps = view->height > view->lineno
		      ? -view->lineno : -view->height;
		break;

	case REQ_MOVE_PAGE_DOWN:
		steps = view->lineno + view->height >= view->lines
		      ? view->lines - view->lineno - 1 : view->height;
		break;

	case REQ_MOVE_UP:
		steps = -1;
		break;

	case REQ_MOVE_DOWN:
		steps = 1;
		break;
	}

	if (steps <= 0 && view->lineno == 0) {
		report("Already at first line");
		return;

	} else if (steps >= 0 && view->lineno + 1 == view->lines) {
		report("Already at last line");
		return;
	}

	/* Move the current line */
	view->lineno += steps;
	assert(0 <= view->lineno && view->lineno < view->lines);

	/* Repaint the old "current" line if we be scrolling */
	if (ABS(steps) < view->height) {
		int prev_lineno = view->lineno - steps - view->offset;

		wmove(view->win, prev_lineno, 0);
		wclrtoeol(view->win);
		view->draw(view, view->lineno - steps - view->offset);
	}

	/* Check whether the view needs to be scrolled */
	if (view->lineno < view->offset ||
	    view->lineno >= view->offset + view->height) {
		if (steps < 0 && -steps > view->offset) {
			steps = -view->offset;

		} else if (steps > 0) {
			if (view->lineno == view->lines - 1 &&
			    view->lines > view->height) {
				steps = view->lines - view->offset - 1;
				if (steps >= view->height)
					steps -= view->height - 1;
			}
		}

		do_scroll_view(view, steps);
		return;
	}

	/* Draw the current line */
	view->draw(view, view->lineno - view->offset);

	redrawwin(view->win);
	wrefresh(view->win);
	report("");
}


/*
 * Incremental updating
 */

static bool
begin_update(struct view *view)
{
	char *id = view->id;

	if (snprintf(view->cmdbuf, sizeof(view->cmdbuf), view->cmd,
		     id, id, id) < sizeof(view->cmdbuf))
		view->pipe = popen(view->cmdbuf, "r");

	if (!view->pipe)
		return FALSE;

	if (nloading++ == 0)
		nodelay(status_win, TRUE);

	view->offset = 0;
	view->lines  = 0;
	view->lineno = 0;

	if (view->line) {
		int i;

		for (i = 0; i < view->lines; i++)
			if (view->line[i])
				free(view->line[i]);

		free(view->line);
		view->line = NULL;
	}

	view->start_time = time(NULL);

	return TRUE;
}

static void
end_update(struct view *view)
{
	if (nloading-- == 1)
		nodelay(status_win, FALSE);

	pclose(view->pipe);
	view->pipe = NULL;
}

static int
update_view(struct view *view)
{
	char buffer[BUFSIZ];
	char *line;
	void **tmp;
	/* The number of lines to read. If too low it will cause too much
	 * redrawing (and possible flickering), if too high responsiveness
	 * will suffer. */
	int lines = view->height;
	int redraw_from = -1;

	if (!view->pipe)
		return TRUE;

	/* Only redraw if lines are visible. */
	if (view->offset + view->height >= view->lines)
		redraw_from = view->lines - view->offset;

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

	/* CPU hogilicious! */
	update_title(view);

	if (redraw_from >= 0) {
		/* If this is an incremental update, redraw the previous line
		 * since for commits some members could have changed when
		 * loading the main view. */
		if (redraw_from > 0)
			redraw_from--;

		/* Incrementally draw avoids flickering. */
		redraw_view_from(view, redraw_from);
	}

	if (ferror(view->pipe)) {
		report("Failed to read %s: %s", view->cmd, strerror(errno));
		goto end;

	} else if (feof(view->pipe)) {
		time_t secs = time(NULL) - view->start_time;

		if (view == VIEW(REQ_VIEW_HELP)) {
			report("%s", HELP);
			goto end;
		}

		report("Loaded %d lines in %ld second%s", view->lines, secs,
		       secs == 1 ? "" : "s");
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
	struct view *view = VIEW(request);
	struct view *displayed;
	int i;

	if (!view->win && !resize_view(view))
		return prev;

	if (view == prev) {
		foreach_view (displayed, i)
			/* count */ ;

		if (i == 1) {
			report("Already in %s view", view->name);
			return view;
		}

		report("FIXME: Maximize");
		return view;

	} else {
		foreach_view (displayed, i) {
			if (view == displayed) {
				current_view = i;
				/* Blur out the title of the previous view. */
				update_title(prev);
				report("Switching to %s view", view->name);
				return view;
			}
		}

		/* Split to diff view */
		if (i == 1 &&
		    SCALE_SPLIT_VIEW(prev->height) > 3 &&
		    prev == VIEW(REQ_VIEW_MAIN) &&
		    view == VIEW(REQ_VIEW_DIFF)) {
			view->height  = SCALE_SPLIT_VIEW(prev->height) - 1;
			prev->height -= view->height + 1;

			wresize(prev->win, prev->height, prev->width);
			mvwin(prev->title, prev->height, 0);

			wresize(view->win, view->height, view->width);
			mvwin(view->win,   prev->height + 1, 0);
			mvwin(view->title, prev->height + 1 + view->height, 0);
			wrefresh(view->win);
			current_view++;
			update_title(prev);
		}
	}

	/* Continue loading split views in the background. */
	if (prev && prev->pipe && current_view < 1)
		end_update(prev);

	if (begin_update(view)) {
		/* Clear the old view and let the incremental updating refill
		 * the screen. */
		display[current_view] = view;
		wclear(view->win);
		report("Loading...");
	}

	return view;
}


/* Process keystrokes */
static int
view_driver(struct view *view, int key)
{
	int request = get_request(key);
	int i;

	assert(view);

	switch (request) {
	case REQ_MOVE_UP:
	case REQ_MOVE_DOWN:
	case REQ_MOVE_PAGE_UP:
	case REQ_MOVE_PAGE_DOWN:
	case REQ_MOVE_FIRST_LINE:
	case REQ_MOVE_LAST_LINE:
		move_view(view, request);
		break;

	case REQ_SCROLL_LINE_DOWN:
	case REQ_SCROLL_LINE_UP:
	case REQ_SCROLL_PAGE_DOWN:
	case REQ_SCROLL_PAGE_UP:
		scroll_view(view, request);
		break;

	case REQ_VIEW_MAIN:
	case REQ_VIEW_DIFF:
	case REQ_VIEW_LOG:
	case REQ_VIEW_HELP:
		view = switch_view(view, request);
		break;

	case REQ_TOGGLE_LINE_NUMBERS:
		opt_line_number = !opt_line_number;
		redraw_view(view);
		break;

	case REQ_STOP_LOADING:
		foreach_view (view, i)
			if (view->pipe)
				end_update(view);
		break;

	case REQ_SHOW_VERSION:
		report("Version: %s", VERSION);
		return TRUE;

	case REQ_SCREEN_REDRAW:
		redraw_view(view);
		break;

	case REQ_SCREEN_UPDATE:
		doupdate();
		return TRUE;

	case REQ_QUIT:
		return FALSE;

	default:
		/* An unknown key will show most commonly used commands. */
		report("%s", HELP);
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
	int linelen;
	int attr;

	if (view->offset + lineno >= view->lines)
		return FALSE;

	line = view->line[view->offset + lineno];
	type = get_line_type(line);

	if (view->offset + lineno == view->lineno) {
		if (type == LINE_COMMIT)
			string_copy(ref_commit, line + 7);
		type = LINE_CURSOR;
	}

	attr = get_line_attr(type);
	wattrset(view->win, attr);

	linelen = strlen(line);
	linelen = MIN(linelen, view->width);

	if (opt_line_number) {
		unsigned int real_lineno = view->offset + lineno + 1;
		int col = 0;

		if (real_lineno == 1 || (real_lineno % opt_num_interval) == 0)
			mvwprintw(view->win, lineno, 0, "%4d: ", real_lineno);
		else
			mvwaddstr(view->win, lineno, 0, "    : ");

		while (line) {
			if (*line == '\t') {
				waddnstr(view->win, "        ", 8 - (col % 8));
				col += 8 - (col % 8);
				line++;

			} else {
				char *tab = strchr(line, '\t');

				if (tab)
					waddnstr(view->win, line, tab - line);
				else
					waddstr(view->win, line);
				col += tab - line;
				line = tab;
			}
		}
		waddstr(view->win, line);

	} else {
#if 0
		/* NOTE: Code for only highlighting the text on the cursor line.
		 * Kept since I've not yet decided whether to highlight the
		 * entire line or not. --fonseca */
		/* No empty lines makes cursor drawing and clearing implicit. */
		if (!*line)
			line = " ", linelen = 1;
#endif
		mvwaddnstr(view->win, lineno, 0, line, linelen);
	}

	/* Paint the rest of the line if it's the cursor line. */
	if (type == LINE_CURSOR)
		wchgat(view->win, -1, 0, type, NULL);

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
	char buf[DATE_COLS + 1];
	struct commit *commit;
	enum line_type type;
	int cols = 0;
	size_t timelen;

	if (view->offset + lineno >= view->lines)
		return FALSE;

	commit = view->line[view->offset + lineno];
	if (!*commit->author)
		return FALSE;

	if (view->offset + lineno == view->lineno) {
		string_copy(ref_commit, commit->id);
		type = LINE_CURSOR;
	} else {
		type = LINE_MAIN_COMMIT;
	}

	wmove(view->win, lineno, cols);
	wattrset(view->win, get_line_attr(LINE_MAIN_DATE));

	timelen = strftime(buf, sizeof(buf), DATE_FORMAT, &commit->time);
	waddnstr(view->win, buf, timelen);
	waddstr(view->win, " ");

	cols += DATE_COLS;
	wmove(view->win, lineno, cols);
	wattrset(view->win, get_line_attr(LINE_MAIN_AUTHOR));

	if (strlen(commit->author) > 19) {
		waddnstr(view->win, commit->author, 18);
		wattrset(view->win, get_line_attr(LINE_MAIN_DELIM));
		waddch(view->win, '~');
	} else {
		waddstr(view->win, commit->author);
	}

	cols += 20;
	wattrset(view->win, A_NORMAL);
	mvwaddch(view->win, lineno, cols, ACS_LTEE);
	wattrset(view->win, get_line_attr(type));
	mvwaddstr(view->win, lineno, cols + 2, commit->title);
	wattrset(view->win, A_NORMAL);

	return TRUE;
}

/* Reads git log --pretty=raw output and parses it into the commit struct. */
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

		line += STRING_SIZE("commit ");

		view->line[view->lines++] = commit;
		string_copy(commit->id, line);
		break;

	case LINE_AUTHOR_IDENT:
	{
		char *ident = line + STRING_SIZE("author ");
		char *end = strchr(ident, '<');

		if (end) {
			for (; end > ident && isspace(end[-1]); end--) ;
			*end = 0;
		}

		commit = view->line[view->lines - 1];
		string_copy(commit->author, ident);

		/* Parse epoch and timezone */
		if (end) {
			char *secs = strchr(end + 1, '>');
			char *zone;
			time_t time;

			if (!secs || secs[1] != ' ')
				break;

			secs += 2;
			time = (time_t) atol(secs);
			zone = strchr(secs, ' ');
			if (zone && strlen(zone) == STRING_SIZE(" +0700")) {
				long tz;

				zone++;
				tz  = ('0' - zone[1]) * 60 * 60 * 10;
				tz += ('0' - zone[2]) * 60 * 60;
				tz += ('0' - zone[3]) * 60;
				tz += ('0' - zone[4]) * 60;

				if (zone[0] == '-')
					tz = -tz;

				time -= tz;
			}
			gmtime_r(&time, &commit->time);
		}
		break;
	}
	default:
		/* We should only ever end up here if there has already been a
		 * commit line, however, be safe. */
		if (view->lines == 0)
			break;

		/* Fill in the commit title if it has not already been set. */
		commit = view->line[view->lines - 1];
		if (commit->title[0])
			break;

		/* Require titles to start with a non-space character at the
		 * offset used by git log. */
		if (strncmp(line, "    ", 4) ||
		    isspace(line[5]))
			break;

		string_copy(commit->title, line + 4);
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
update_title(struct view *view)
{
	werase(view->title);
	wmove(view->title, 0, 0);

	if (view == &views[current_view])
		wattrset(view->title, A_BOLD);
	else
		wattrset(view->title, A_NORMAL);

	/* [main] ref: 334b506... - line 6 of 4383 (0%) */
	wprintw(view->title, "[%s] ref: %s", view->name, ref_commit);
	if (view->lines) {
		wprintw(view->title, " - line %d of %d (%d%%)",
			view->lineno + 1,
			view->lines,
			(view->lineno + 1) * 100 / view->lines);
	}

	wrefresh(view->title);
}

/* Update status and title window. */
static void
report(const char *msg, ...)
{
	va_list args;

	va_start(args, msg);

	/* Update the title window first, so the cursor ends up in the status
	 * window. */
	update_title(display[current_view]);

	werase(status_win);
	wmove(status_win, 0, 0);
	vwprintw(status_win, msg, args);
	wrefresh(status_win);

	va_end(args);
}

int
main(int argc, char *argv[])
{
	int x, y;
	int request;
	int git_cmd;

	signal(SIGINT, quit);

	git_cmd = parse_options(argc, argv);
	if (git_cmd < 0)
		return 0;
	if (git_cmd < argc) {
		die("Too many options");
	}

	request = opt_request;

	initscr();      /* Initialize the curses library */
	nonl();         /* Tell curses not to do NL->CR/NL on output */
	cbreak();       /* Take input chars one at a time, no wait for \n */
	noecho();       /* Don't echo input */
	leaveok(stdscr, TRUE);

	if (has_colors())
		init_colors();

	getmaxyx(stdscr, y, x);
	status_win = newwin(1, 0, y - 1, 0);
	if (!status_win)
		die("Failed to create status window");

	/* Enable keyboard mapping */
	keypad(status_win, TRUE);
	wbkgdset(status_win, get_line_attr(LINE_STATUS));

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
 * TODO
 * ----
 * Features that should be explored.
 *
 * - Dynamic scaling of line number indentation.
 *
 * - Proper command line handling; ability to take the command that should be
 *   shown. Example:
 *
 *	$ tig log -p
 *
 * - Internal command line (exmode-inspired) which allows to specify what git
 *   log or git diff command to run. Example:
 *
 *	:log -p
 *
 * - Proper resizing support. I am yet to figure out whether catching SIGWINCH
 *   is preferred over using ncurses' built-in support for resizing.
 *
 * - Locale support.
 *
 * COPYRIGHT
 * ---------
 * Copyright (c) Jonas Fonseca <fonseca@diku.dk>, 2006
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * SEE ALSO
 * --------
 * [verse]
 * link:http://www.kernel.org/pub/software/scm/git/docs/[git(7)],
 * link:http://www.kernel.org/pub/software/scm/cogito/docs/[cogito(7)]
 * gitk(1): git repository browser written using tcl/tk,
 * gitview(1): git repository browser written using python/gtk.
 **/
