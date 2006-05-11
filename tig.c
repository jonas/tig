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
 * tig [options] [--] [git log options]
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
#include <unistd.h>
#include <time.h>

#include <curses.h>

static void die(const char *err, ...);
static void report(const char *msg, ...);
static void set_nonblocking_input(int boolean);

#define ABS(x)		((x) >= 0  ? (x) : -(x))
#define MIN(x, y)	((x) < (y) ? (x) :  (y))

#define ARRAY_SIZE(x)	(sizeof(x) / sizeof(x[0]))
#define STRING_SIZE(x)	(sizeof(x) - 1)

#define SIZEOF_REF	256	/* Size of symbolic or SHA1 ID. */
#define SIZEOF_CMD	1024	/* Size of command buffer. */

/* This color name can be used to refer to the default term colors. */
#define COLOR_DEFAULT	(-1)

/* The format and size of the date column in the main view. */
#define DATE_FORMAT	"%Y-%m-%d %H:%M"
#define DATE_COLS	STRING_SIZE("2006-04-29 14:21 ")

/* The default interval between line numbers. */
#define NUMBER_INTERVAL	1

#define	SCALE_SPLIT_VIEW(height)	((height) * 2 / 3)

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
	REQ_VIEW_PAGER,

	REQ_ENTER,
	REQ_QUIT,
	REQ_PROMPT,
	REQ_SCREEN_REDRAW,
	REQ_SCREEN_UPDATE,
	REQ_SHOW_VERSION,
	REQ_STOP_LOADING,
	REQ_TOGGLE_LINE_NUMBERS,
	REQ_VIEW_NEXT,

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

/*
 * String helpers
 */

static inline void
string_ncopy(char *dst, char *src, int dstlen)
{
	strncpy(dst, src, dstlen - 1);
	dst[dstlen - 1] = 0;

}

/* Shorthand for safely copying into a fixed buffer. */
#define string_copy(dst, src) \
	string_ncopy(dst, src, sizeof(dst))

/* Shell quoting
 *
 * NOTE: The following is a slightly modified copy of the git project's shell
 * quoting routines found in the quote.c file.
 *
 * Help to copy the thing properly quoted for the shell safety.  any single
 * quote is replaced with '\'', any exclamation point is replaced with '\!',
 * and the whole thing is enclosed in a
 *
 * E.g.
 *  original     sq_quote     result
 *  name     ==> name      ==> 'name'
 *  a b      ==> a b       ==> 'a b'
 *  a'b      ==> a'\''b    ==> 'a'\''b'
 *  a!b      ==> a'\!'b    ==> 'a'\!'b'
 */

static size_t
sq_quote(char buf[SIZEOF_CMD], size_t bufsize, const char *src)
{
	char c;

#define BUFPUT(x) ( (bufsize < SIZEOF_CMD) && (buf[bufsize++] = (x)) )

	BUFPUT('\'');
	while ((c = *src++)) {
		if (c == '\'' || c == '!') {
			BUFPUT('\'');
			BUFPUT('\\');
			BUFPUT(c);
			BUFPUT('\'');
		} else {
			BUFPUT(c);
		}
	}
	BUFPUT('\'');

	return bufsize;
}


/**
 * OPTIONS
 * -------
 **/

static int opt_line_number	= FALSE;
static int opt_num_interval	= NUMBER_INTERVAL;
static enum request opt_request = REQ_VIEW_MAIN;
static char opt_cmd[SIZEOF_CMD]	= "";
static FILE *opt_pipe		= NULL;

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
			break;
		}

		/**
		 * -l::
		 *	Start up in log view.
		 **/
		if (!strcmp(opt, "-l")) {
			opt_request = REQ_VIEW_LOG;
			continue;
		}

		/**
		 * -d::
		 *	Start up in diff view.
		 **/
		if (!strcmp(opt, "-d")) {
			opt_request = REQ_VIEW_DIFF;
			continue;
		}

		/**
		 * -n[INTERVAL], --line-number[=INTERVAL]::
		 *	Prefix line numbers in log and diff view.
		 *	Optionally, with interval different than each line.
		 **/
		if (!strncmp(opt, "-n", 2) ||
		           !strncmp(opt, "--line-number", 13)) {
			char *num = opt;

			if (opt[1] == 'n') {
				num = opt + 2;

			} else if (opt[STRING_SIZE("--line-number")] == '=') {
				num = opt + STRING_SIZE("--line-number=");
			}

			if (isdigit(*num))
				opt_num_interval = atoi(num);

			opt_line_number = TRUE;
			continue;
		}

		/**
		 * -v, --version::
		 *	Show version and exit.
		 **/
		if (!strcmp(opt, "-v") ||
			   !strcmp(opt, "--version")) {
			printf("tig version %s\n", VERSION);
			return -1;
		}

		/**
		 * \--::
		 *	End of tig(1) options. Useful when specifying commands
		 *	for the main view. Example:
		 *
		 *		$ tig -- --since=1.month
		 **/
		if (!strcmp(opt, "--")) {
			i++;
			break;
		}

		 /* Make stuff like:
		 *
		 *	$ tig tag-1.0..HEAD
		 *
		 * work.
		 */
		if (opt[0] && opt[0] != '-')
			break;

		die("unknown command '%s'", opt);
	}

	/**
	 * Pager mode
	 * ~~~~~~~~~~
	 * If stdin is a pipe, any log or diff options will be ignored and the
	 * pager view will be opened loading data from stdin. The pager mode
	 * can be used for colorizing output from various git commands.
	 *
	 * Example on how to colorize the output of git-show(1):
	 *
	 *	$ git show | tig
	 **/
	if (!isatty(STDIN_FILENO)) {
		opt_request = REQ_VIEW_PAGER;
		opt_pipe = stdin;

	} else if (i < argc) {
		size_t buf_size;

		/* XXX: This is vulnerable to the user overriding options
		 * required for the main view parser. */
		if (opt_request == REQ_VIEW_MAIN)
			string_copy(opt_cmd, "git log --stat --pretty=raw");
		else
			string_copy(opt_cmd, "git");
		buf_size = strlen(opt_cmd);

		while (buf_size < sizeof(opt_cmd) && i < argc) {
			opt_cmd[buf_size++] = ' ';
			buf_size = sq_quote(opt_cmd, buf_size, argv[i++]);
		}

		if (buf_size >= sizeof(opt_cmd))
			die("command too long");

		opt_cmd[buf_size] = 0;

	}

	return i;
}


/**
 * KEYS
 * ----
 **/

#define HELP "(d)iff, (l)og, (m)ain, (q)uit, (v)ersion, (h)elp"

struct keymap {
	int alias;
	int request;
};

struct keymap keymap[] = {
	/**
	 * View switching
	 * ~~~~~~~~~~~~~~
	 * d::
	 *	Switch to diff view.
	 * l::
	 *	Switch to log view.
	 * m::
	 *	Switch to main view.
	 * h::
	 *	Show man page.
	 * Return::
	 *	If in main view split the view
	 *	and show the diff in the bottom view.
	 * Tab::
	 *	Switch to next view.
	 **/
	{ 'm',		REQ_VIEW_MAIN },
	{ 'd',		REQ_VIEW_DIFF },
	{ 'l',		REQ_VIEW_LOG },
	{ 'h',		REQ_VIEW_HELP },

	{ KEY_TAB,	REQ_VIEW_NEXT },
	{ KEY_RETURN,	REQ_ENTER },

	/**
	 * Cursor navigation
	 * ~~~~~~~~~~~~~~~~~
	 * Up, k::
	 *	Move curser one line up.
	 * Down, j::
	 *	Move cursor one line down.
	 * Page Up::
	 *	Move curser one page up.
	 * Page Down::
	 *	Move cursor one page down.
	 * Home::
	 *	Jump to first line.
	 * End::
	 *	Jump to last line.
	 **/
	{ KEY_UP,	REQ_MOVE_UP },
	{ 'k',		REQ_MOVE_UP },
	{ KEY_DOWN,	REQ_MOVE_DOWN },
	{ 'j',		REQ_MOVE_DOWN },
	{ KEY_HOME,	REQ_MOVE_FIRST_LINE },
	{ KEY_END,	REQ_MOVE_LAST_LINE },
	{ KEY_NPAGE,	REQ_MOVE_PAGE_DOWN },
	{ KEY_PPAGE,	REQ_MOVE_PAGE_UP },

	/**
	 * Scrolling
	 * ~~~~~~~~~
	 * Insert::
	 *	Scroll view one line up.
	 * Delete::
	 *	Scroll view one line down.
	 * w::
	 *	Scroll view one page up.
	 * s::
	 *	Scroll view one page down.
	 **/
	{ KEY_IC,	REQ_SCROLL_LINE_UP },
	{ KEY_DC,	REQ_SCROLL_LINE_DOWN },
	{ 'w',		REQ_SCROLL_PAGE_UP },
	{ 's',		REQ_SCROLL_PAGE_DOWN },

	/**
	 * Misc
	 * ~~~~
	 * q, Escape::
	 *	Quit
	 * r::
	 *	Redraw screen.
	 * z::
	 *	Stop all background loading.
	 * v::
	 *	Show version.
	 * n::
	 *	Toggle line numbers on/off.
	 * ':'::
	 *	Open prompt.
	 **/
	{ KEY_ESC,	REQ_QUIT },
	{ 'q',		REQ_QUIT },
	{ 'z',		REQ_STOP_LOADING },
	{ 'v',		REQ_SHOW_VERSION },
	{ 'r',		REQ_SCREEN_REDRAW },
	{ 'n',		REQ_TOGGLE_LINE_NUMBERS },
	{ ':',		REQ_PROMPT },

	/* wgetch() with nodelay() enabled returns ERR when there's no input. */
	{ ERR,		REQ_SCREEN_UPDATE },
};

static enum request
get_request(int key)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(keymap); i++)
		if (keymap[i].alias == key)
			return keymap[i].request;

	return (enum request) key;
}


/*
 * Line-oriented content detection.
 */

#define LINE_INFO \
/*   Line type	   String to match	Foreground	Background	Attributes
 *   ---------     ---------------      ----------      ----------      ---------- */ \
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
LINE(PP_AUTHOR,	   "Author: ",		COLOR_CYAN,	COLOR_DEFAULT,	0), \
LINE(PP_MERGE,	   "Merge: ",		COLOR_BLUE,	COLOR_DEFAULT,	0), \
LINE(PP_DATE,	   "Date:   ",		COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(PP_COMMIT,	   "Commit: ",		COLOR_GREEN,	COLOR_DEFAULT,	0), \
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
LINE(TITLE_BLUR,   "",			COLOR_WHITE,	COLOR_BLUE,	0), \
LINE(TITLE_FOCUS,  "",			COLOR_WHITE,	COLOR_BLUE,	A_BOLD), \
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
	const char *name;	/* View name */
	const char *cmdfmt;	/* Default command line format */
	char *id;		/* Points to either of ref_{head,commit} */
	size_t objsize;		/* Size of objects in the line index */

	struct view_ops {
		bool (*draw)(struct view *view, unsigned int lineno);
		bool (*read)(struct view *view, char *line);
		bool (*enter)(struct view *view);
	} *ops;

	char cmd[SIZEOF_CMD];	/* Command buffer */
	char ref[SIZEOF_REF];	/* Hovered commit reference */
	char vid[SIZEOF_REF];	/* View ID. Set to id member when updating. */

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

static struct view_ops pager_ops;
static struct view_ops main_ops;

#define DIFF_CMD \
	"git log --stat -n1 %s ; echo; " \
	"git diff --find-copies-harder -B -C %s^ %s"

#define LOG_CMD	\
	"git log --cc --stat -n100 %s"

#define MAIN_CMD \
	"git log --stat --pretty=raw %s"

#define HELP_CMD \
	"man tig 2> /dev/null"

char ref_head[SIZEOF_REF]	= "HEAD";
char ref_commit[SIZEOF_REF]	= "HEAD";

static struct view views[] = {
	{ "main",  MAIN_CMD,  ref_head,   sizeof(struct commit), &main_ops },
	{ "diff",  DIFF_CMD,  ref_commit, sizeof(char),		 &pager_ops },
	{ "log",   LOG_CMD,   ref_head,   sizeof(char),		 &pager_ops },
	{ "help",  HELP_CMD,  ref_head,   sizeof(char),		 &pager_ops },
	{ "pager", "cat",     ref_head,   sizeof(char),		 &pager_ops },
};

#define VIEW(req) (&views[(req) - REQ_OFFSET - 1])

/* The display array of active views and the index of the current view. */
static struct view *display[2];
static unsigned int current_view;

#define foreach_view(view, i) \
	for (i = 0; i < ARRAY_SIZE(display) && (view = display[i]); i++)


static void
redraw_view_from(struct view *view, int lineno)
{
	assert(0 <= lineno && lineno < view->height);

	for (; lineno < view->height; lineno++) {
		if (!view->ops->draw(view, lineno))
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

static void
resize_display(void)
{
	int offset, i;
	struct view *base = display[0];
	struct view *view = display[1] ? display[1] : display[0];

	/* Setup window dimensions */

	getmaxyx(stdscr, base->height, base->width);

	/* Make room for the status window. */
	base->height -= 1;

	if (view != base) {
		/* Horizontal split. */
		view->width   = base->width;
		view->height  = SCALE_SPLIT_VIEW(base->height);
		base->height -= view->height;

		/* Make room for the title bar. */
		view->height -= 1;
	}

	/* Make room for the title bar. */
	base->height -= 1;

	offset = 0;

	foreach_view (view, i) {
		if (!view->win) {
			view->win = newwin(view->height, 0, offset, 0);
			if (!view->win)
				die("Failed to create %s view", view->name);

			scrollok(view->win, TRUE);

			view->title = newwin(1, 0, offset + view->height, 0);
			if (!view->title)
				die("Failed to create title window");

		} else {
			wresize(view->win, view->height, view->width);
			mvwin(view->win,   offset, 0);
			mvwin(view->title, offset + view->height, 0);
			wrefresh(view->win);
		}

		offset += view->height + 1;
	}
}

static void
update_view_title(struct view *view)
{
	if (view == display[current_view])
		wbkgdset(view->title, get_line_attr(LINE_TITLE_FOCUS));
	else
		wbkgdset(view->title, get_line_attr(LINE_TITLE_BLUR));

	werase(view->title);
	wmove(view->title, 0, 0);

	/* [main] ref: 334b506... - commit 6 of 4383 (0%) */

	if (*view->ref)
		wprintw(view->title, "[%s] ref: %s", view->name, view->ref);
	else
		wprintw(view->title, "[%s]", view->name);

	if (view->lines) {
		char *type = view == VIEW(REQ_VIEW_MAIN) ? "commit" : "line";

		wprintw(view->title, " - %s %d of %d (%d%%)",
			type,
			view->lineno + 1,
			view->lines,
			(view->lineno + 1) * 100 / view->lines);
	}

	wrefresh(view->title);
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
			if (!view->ops->draw(view, line))
				break;
		}
	}

	/* Move current line into the view. */
	if (view->lineno < view->offset) {
		view->lineno = view->offset;
		view->ops->draw(view, 0);

	} else if (view->lineno >= view->offset + view->height) {
		view->lineno = view->offset + view->height - 1;
		view->ops->draw(view, view->lineno - view->offset);
	}

	assert(view->offset <= view->lineno && view->lineno < view->lines);

	redrawwin(view->win);
	wrefresh(view->win);
	report("");
}

/* Scroll frontend */
static void
scroll_view(struct view *view, enum request request)
{
	int lines = 1;

	switch (request) {
	case REQ_SCROLL_PAGE_DOWN:
		lines = view->height;
	case REQ_SCROLL_LINE_DOWN:
		if (view->offset + lines > view->lines)
			lines = view->lines - view->offset;

		if (lines == 0 || view->offset + view->height >= view->lines) {
			report("Already on last line");
			return;
		}
		break;

	case REQ_SCROLL_PAGE_UP:
		lines = view->height;
	case REQ_SCROLL_LINE_UP:
		if (lines > view->offset)
			lines = view->offset;

		if (lines == 0) {
			report("Already on first line");
			return;
		}

		lines = -lines;
		break;

	default:
		die("request %d not handled in switch", request);
	}

	do_scroll_view(view, lines);
}

/* Cursor moving */
static void
move_view(struct view *view, enum request request)
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

	default:
		die("request %d not handled in switch", request);
	}

	if (steps <= 0 && view->lineno == 0) {
		report("Already on first line");
		return;

	} else if (steps >= 0 && view->lineno + 1 >= view->lines) {
		report("Already on last line");
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
		view->ops->draw(view, prev_lineno);
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
	view->ops->draw(view, view->lineno - view->offset);

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

	if (opt_cmd[0]) {
		string_copy(view->cmd, opt_cmd);
		opt_cmd[0] = 0;
	} else {
		if (snprintf(view->cmd, sizeof(view->cmd), view->cmdfmt,
			     id, id, id) >= sizeof(view->cmd))
			return FALSE;
	}

	/* Special case for the pager view. */
	if (opt_pipe) {
		view->pipe = opt_pipe;
		opt_pipe = NULL;
	} else {
		view->pipe = popen(view->cmd, "r");
	}

	if (!view->pipe)
		return FALSE;

	set_nonblocking_input(TRUE);

	view->offset = 0;
	view->lines  = 0;
	view->lineno = 0;
	string_copy(view->vid, id);

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
	if (!view->pipe)
		return;
	set_nonblocking_input(FALSE);
	pclose(view->pipe);
	view->pipe = NULL;
}

static bool
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

		if (!view->ops->read(view, line))
			goto alloc_error;

		if (lines-- == 1)
			break;
	}

	/* CPU hogilicious! */
	update_view_title(view);

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
		report("Failed to read: %s", strerror(errno));
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

enum open_flags {
	OPEN_DEFAULT = 0,	/* Use default view switching. */
	OPEN_SPLIT = 1,		/* Split current view. */
	OPEN_BACKGROUNDED = 2,	/* Backgrounded. */
	OPEN_RELOAD = 4,	/* Reload view even if it is the current. */
};

static void
open_view(struct view *prev, enum request request, enum open_flags flags)
{
	bool backgrounded = !!(flags & OPEN_BACKGROUNDED);
	bool split = !!(flags & OPEN_SPLIT);
	bool reload = !!(flags & OPEN_RELOAD);
	struct view *view = VIEW(request);
	struct view *displayed;
	int nviews;

	/* Cycle between displayed views and count the views. */
	foreach_view (displayed, nviews) {
		if (prev != view &&
		    view == displayed &&
		    !strcmp(view->id, prev->id)) {
			current_view = nviews;
			/* Blur out the title of the previous view. */
			update_view_title(prev);
			report("Switching to %s view", view->name);
			return;
		}
	}

	if (view == prev && nviews == 1 && !reload) {
		report("Already in %s view", view->name);
		return;
	}

	if (strcmp(view->vid, view->id) &&
	    !begin_update(view)) {
		report("Failed to load %s view", view->name);
		return;
	}

	if (split) {
		display[current_view + 1] = view;
		if (!backgrounded)
			current_view++;
	} else {
		/* Maximize the current view. */
		memset(display, 0, sizeof(display));
		current_view = 0;
		display[current_view] = view;
	}

	resize_display();

	if (split && prev->lineno - prev->offset > prev->height) {
		/* Take the title line into account. */
		int lines = prev->lineno - prev->height + 1;

		/* Scroll the view that was split if the current line is
		 * outside the new limited view. */
		do_scroll_view(prev, lines);
	}

	if (prev && view != prev) {
		/* "Blur" the previous view. */
		update_view_title(prev);

		/* Continue loading split views in the background. */
		if (!split)
			end_update(prev);
	}

	if (view->pipe) {
		/* Clear the old view and let the incremental updating refill
		 * the screen. */
		wclear(view->win);
		report("Loading...");
	} else {
		redraw_view(view);
		report("");
	}
}


/*
 * User request switch noodle
 */

static int
view_driver(struct view *view, enum request request)
{
	int i;

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
	case REQ_VIEW_PAGER:
		open_view(view, request, OPEN_DEFAULT);
		break;

	case REQ_ENTER:
		if (!view->lines) {
			report("Nothing to enter");
			break;
		}
		return view->ops->enter(view);

	case REQ_VIEW_NEXT:
	{
		int nviews = display[1] ? 2 : 1;
		int next_view = (current_view + 1) % nviews;

		if (next_view == current_view) {
			report("Only one view is displayed");
			break;
		}

		current_view = next_view;
		/* Blur out the title of the previous view. */
		update_view_title(view);
		report("Switching to %s view", display[current_view]->name);
		break;
	}
	case REQ_TOGGLE_LINE_NUMBERS:
		opt_line_number = !opt_line_number;
		redraw_view(view);
		break;

	case REQ_PROMPT:
		open_view(view, opt_request, OPEN_RELOAD);
		break;

	case REQ_STOP_LOADING:
		foreach_view (view, i) {
			if (view->pipe)
				report("Stopped loaded of %s view", view->name),
			end_update(view);
		}
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
 * View backend handlers
 */

static bool
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
		switch (type) {
		case LINE_COMMIT:
			string_copy(view->ref, line + 7);
			string_copy(ref_commit, view->ref);
			break;
		case LINE_PP_COMMIT:
			string_copy(view->ref, line + 8);
			string_copy(ref_commit, view->ref);
		default:
			break;
		}
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

static bool
pager_read(struct view *view, char *line)
{
	view->line[view->lines] = strdup(line);
	if (!view->line[view->lines])
		return FALSE;

	view->lines++;
	return TRUE;
}

static bool
pager_enter(struct view *view)
{
	char *line = view->line[view->lineno];

	if (get_line_type(line) == LINE_COMMIT) {
		open_view(view, REQ_VIEW_DIFF, OPEN_DEFAULT);
	}

	return TRUE;
}


static struct view_ops pager_ops = {
	pager_draw,
	pager_read,
	pager_enter,
};

static bool
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
		string_copy(view->ref, commit->id);
		string_copy(ref_commit, view->ref);
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
static bool
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

static bool
main_enter(struct view *view)
{
	open_view(view, REQ_VIEW_DIFF, OPEN_SPLIT | OPEN_BACKGROUNDED);
	return TRUE;
}

static struct view_ops main_ops = {
	main_draw,
	main_read,
	main_enter,
};

/*
 * Status management
 */

/* The status window is used for polling keystrokes. */
static WINDOW *status_win;

/* Update status and title window. */
static void
report(const char *msg, ...)
{
	va_list args;

	va_start(args, msg);

	/* Update the title window first, so the cursor ends up in the status
	 * window. */
	update_view_title(display[current_view]);

	werase(status_win);
	wmove(status_win, 0, 0);
	vwprintw(status_win, msg, args);
	wrefresh(status_win);

	va_end(args);
}

/* Controls when nodelay should be in effect when polling user input. */
static void
set_nonblocking_input(int loading)
{
	/* The number of loading views. */
	static unsigned int nloading;

	if (loading == TRUE) {
		if (nloading++ == 0)
			nodelay(status_win, TRUE);
		return;
	}

	if (nloading-- == 1)
		nodelay(status_win, FALSE);
}

static void
init_display(void)
{
	int x, y;

	/* Initialize the curses library */
	if (isatty(STDIN_FILENO)) {
		initscr();
	} else {
		/* Leave stdin and stdout alone when acting as a pager. */
		FILE *io = fopen("/dev/tty", "r+");

		newterm(NULL, io, io);
	}

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

int
main(int argc, char *argv[])
{
	enum request request;
	int git_arg;

	signal(SIGINT, quit);

	git_arg = parse_options(argc, argv);
	if (git_arg < 0)
		return 0;

	request = opt_request;

	init_display();

	while (view_driver(display[current_view], request)) {
		struct view *view;
		int key;
		int i;

		foreach_view (view, i)
			update_view(view);

		/* Refresh, accept single keystroke of input */
		key = wgetch(status_win);
		request = get_request(key);

		if (request == REQ_PROMPT) {
			report(":");
			/* Temporarily switch to line-oriented and echoed
			 * input. */
			nocbreak();
			echo();

			if (wgetnstr(status_win, opt_cmd + 4, sizeof(opt_cmd) - 4) == OK) {
				memcpy(opt_cmd, "git ", 4);
				opt_request = REQ_VIEW_PAGER;
			} else {
				request = ERR;
			}

			noecho();
			cbreak();
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
 * - Internal command line (exmode-inspired) which allows to specify what git
 *   log or git diff command to run. Example:
 *
 *	:log -p
 *
 * - Terminal resizing support. I am yet to figure out whether catching
 *   SIGWINCH is preferred over using ncurses' built-in support for resizing.
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
