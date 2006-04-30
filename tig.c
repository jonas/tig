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
#include <form.h>

static void die(const char *err, ...);
static void report(const char *msg, ...);

/* Some ascii-shorthands that fit into the ncurses namespace. */
#define KEY_TAB		9
#define KEY_ESC		27
#define KEY_DEL		127

/* View requests */
/* REQ_* values from form.h is used as a basis for user actions. */
enum request {
	/* Offset new values relative to MAX_COMMAND from form.h. */
	REQ_OFFSET = MAX_COMMAND,

	/* XXX: Keep the view request first and in sync with views[]. */
	REQ_DIFF,
	REQ_LOG,
	REQ_MAIN,

	REQ_QUIT,
	REQ_VERSION,
	REQ_STOP,
	REQ_UPDATE,
	REQ_REDRAW,
	REQ_FIRST_LINE,
	REQ_LAST_LINE,
	REQ_LINE_NUMBER,
};

/* The request are used for adressing the view array. */
#define VIEW_OFFSET(r)	((r) - REQ_OFFSET - 1)

/* Size of symbolic or SHA1 ID. */
#define SIZEOF_REF	256

/* This color name can be used to refer to the default term colors. */
#define COLOR_DEFAULT	(-1)

/* The format and size of the date column in the main view. */
#define DATE_FORMAT	"%Y-%m-%d %H:%M"
#define DATE_COLS	(STRING_SIZE("2006-04-29 14:21") + 1)

/* The interval between line numbers. */
#define NUMBER_INTERVAL	5

#define ABS(x)		((x) >= 0 ? (x) : -(x))
#define MIN(x, y)	((x) < (y) ? (x) : (y))

#define ARRAY_SIZE(x)	(sizeof(x) / sizeof(x[0]))
#define STRING_SIZE(x)	(sizeof(x) - 1)

struct commit {
	char id[41];
	char title[75];
	char author[75];
	struct tm time;
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

static int opt_line_number;
static int opt_request = REQ_MAIN;

char head_id[SIZEOF_REF] = "HEAD";
char commit_id[SIZEOF_REF] = "HEAD";

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
		 **/
		if (!strcmp(opt, "log")) {
			opt_request = REQ_LOG;
			return i;

		/**
		 * diff [options]::
		 *	git diff options.
		 **/
		} else if (!strcmp(opt, "diff")) {
			opt_request = REQ_DIFF;
			return i;

		/**
		 * -l::
		 *	Start up in log view.
		 **/
		} else if (!strcmp(opt, "-l")) {
			opt_request = REQ_LOG;

		/**
		 * -d::
		 *	Start up in diff view.
		 **/
		} else if (!strcmp(opt, "-d")) {
			opt_request = REQ_DIFF;

		/**
		 * -n, --line-number::
		 *	Prefix line numbers in log and diff view.
		 **/
		} else if (!strcmp(opt, "-n") ||
		           !strcmp(opt, "--line-number")) {
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
			string_copy(head_id, opt);
			string_copy(commit_id, opt);

		} else {
			die("unknown command '%s'", opt);
		}
	}

	return i;
}


/*
 * Line-oriented content detection.
 */

enum line_type {
	LINE_DEFAULT,
	LINE_AUTHOR,
	LINE_AUTHOR_IDENT,
	LINE_COMMIT,
	LINE_COMMITTER,
	LINE_CURSOR,
	LINE_DATE,
	LINE_DIFF,
	LINE_DIFF_ADD,
	LINE_DIFF_CHUNK,
	LINE_DIFF_COPY,
	LINE_DIFF_DEL,
	LINE_DIFF_DISSIM,
	LINE_DIFF_NEWMODE,
	LINE_DIFF_OLDMODE,
	LINE_DIFF_RENAME,
	LINE_DIFF_SIM,
	LINE_DIFF_TREE,
	LINE_INDEX,
	LINE_MAIN_AUTHOR,
	LINE_MAIN_COMMIT,
	LINE_MAIN_DATE,
	LINE_MAIN_DELIM,
	LINE_MERGE,
	LINE_PARENT,
	LINE_SIGNOFF,
	LINE_STATUS,
	LINE_TITLE,
	LINE_TREE,
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
	{ LINE_##type, (line), STRING_SIZE(line), (fg), (bg), (attr) }

static struct line_info line_info[] = {
	/* Diff markup */
	LINE(DIFF,	   "diff --git ",	COLOR_YELLOW,	COLOR_DEFAULT,	0),
	LINE(INDEX,	   "index ",		COLOR_BLUE,	COLOR_DEFAULT,	0),
	LINE(DIFF_CHUNK,   "@@",		COLOR_MAGENTA,	COLOR_DEFAULT,	0),
	LINE(DIFF_ADD,	   "+",			COLOR_GREEN,	COLOR_DEFAULT,	0),
	LINE(DIFF_DEL,	   "-",			COLOR_RED,	COLOR_DEFAULT,	0),
	LINE(DIFF_OLDMODE, "old mode ",		COLOR_YELLOW,	COLOR_DEFAULT,	0),
	LINE(DIFF_NEWMODE, "new mode ",		COLOR_YELLOW,	COLOR_DEFAULT,	0),
	LINE(DIFF_COPY,	   "copy ",		COLOR_YELLOW,	COLOR_DEFAULT,	0),
	LINE(DIFF_RENAME,  "rename ",		COLOR_YELLOW,	COLOR_DEFAULT,	0),
	LINE(DIFF_SIM,	   "similarity ",	COLOR_YELLOW,	COLOR_DEFAULT,	0),
	LINE(DIFF_DISSIM,  "dissimilarity ",	COLOR_YELLOW,	COLOR_DEFAULT,	0),

	/* Pretty print commit header */
	LINE(AUTHOR,	   "Author: ",		COLOR_CYAN,	COLOR_DEFAULT,	0),
	LINE(MERGE,	   "Merge: ",		COLOR_BLUE,	COLOR_DEFAULT,	0),
	LINE(DATE,	   "Date:   ",		COLOR_YELLOW,	COLOR_DEFAULT,	0),

	/* Raw commit header */
	LINE(COMMIT,	   "commit ",		COLOR_GREEN,	COLOR_DEFAULT,	0),
	LINE(PARENT,	   "parent ",		COLOR_BLUE,	COLOR_DEFAULT,	0),
	LINE(TREE,	   "tree ",		COLOR_BLUE,	COLOR_DEFAULT,	0),
	LINE(AUTHOR_IDENT, "author ",		COLOR_CYAN,	COLOR_DEFAULT,	0),
	LINE(COMMITTER,	   "committer ",	COLOR_MAGENTA,	COLOR_DEFAULT,	0),

	/* Misc */
	LINE(DIFF_TREE,	   "diff-tree ",	COLOR_BLUE,	COLOR_DEFAULT,	0),
	LINE(SIGNOFF,	   "    Signed-off-by", COLOR_YELLOW,	COLOR_DEFAULT,	0),

	/* UI colors */
	LINE(DEFAULT,	   "",	COLOR_DEFAULT,	COLOR_DEFAULT,	A_NORMAL),
	LINE(CURSOR,	   "",	COLOR_WHITE,	COLOR_GREEN,	A_BOLD),
	LINE(STATUS,	   "",	COLOR_GREEN,	COLOR_DEFAULT,	0),
	LINE(TITLE,	   "",	COLOR_YELLOW,	COLOR_BLUE,	A_BOLD),
	LINE(MAIN_DATE,    "",	COLOR_BLUE,	COLOR_DEFAULT,	0),
	LINE(MAIN_AUTHOR,  "",	COLOR_GREEN,	COLOR_DEFAULT,	0),
	LINE(MAIN_COMMIT,  "",	COLOR_DEFAULT,	COLOR_DEFAULT,	0),
	LINE(MAIN_DELIM,   "",	COLOR_MAGENTA,	COLOR_DEFAULT,	0),
};

static struct line_info *
get_line_info(char *line)
{
	int linelen = strlen(line);
	int i;

	for (i = 0; i < ARRAY_SIZE(line_info); i++) {
		if (linelen < line_info[i].linelen
		    || strncasecmp(line_info[i].line, line, line_info[i].linelen))
			continue;

		return &line_info[i];
	}

	return NULL;
}

static enum line_type
get_line_type(char *line)
{
	struct line_info *info = get_line_info(line);

	return info ? info->type : LINE_DEFAULT;
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
	int default_bg = COLOR_BLACK;
	int default_fg = COLOR_WHITE;
	int i;

	start_color();

	if (use_default_colors() != ERR) {
		default_bg = -1;
		default_fg = -1;
	}

	for (i = 0; i < ARRAY_SIZE(line_info); i++) {
		struct line_info *info = &line_info[i];
		int bg = info->bg == COLOR_DEFAULT ? default_bg : info->bg;
		int fg = info->fg == COLOR_DEFAULT ? default_fg : info->fg;

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

	/* View switching */
	{ 'd',		REQ_DIFF },
	{ 'l',		REQ_LOG },
	{ 'm',		REQ_MAIN },

	/* Line number toggling */
	{ 'n',		REQ_LINE_NUMBER },
	/* No input from wgetch() with nodelay() enabled. */
	{ ERR,		REQ_UPDATE },

	/* Misc */
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

/* The status window is used for polling keystrokes. */
static WINDOW *status_win;
static WINDOW *title_win;

/* The number of loading views. Controls when nodelay should be in effect when
 * polling user input. */
static unsigned int nloading;

static struct view views[] = {
	{ "diff",  DIFF_CMD,   commit_id,  pager_read,  pager_draw, sizeof(char) },
	{ "log",   LOG_CMD,    head_id,    pager_read,  pager_draw, sizeof(char) },
	{ "main",  MAIN_CMD,   head_id,    main_read,   main_draw,  sizeof(struct commit) },
};

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

/* FIXME: Fix percentage. */
static void
report_position(struct view *view, int all)
{
	report(all ? "line %d of %d (%d%%)"
		     : "line %d of %d",
	       view->lineno + 1,
	       view->lines,
	       view->lines ? (view->lineno + 1) * 100 / view->lines : 0);
}


static void
move_view(struct view *view, int lines)
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

	if (steps <= 0 && view->lineno == 0) {
		report("already at first line");
		return;

	} else if (steps >= 0 && view->lineno + 1 == view->lines) {
		report("already at last line");
		return;
	}

	/* Move the current line */
	view->lineno += steps;
	assert(0 <= view->lineno && view->lineno < view->lines);

	/* Repaint the old "current" line if we be scrolling */
	if (ABS(steps) < view->height)
		view->draw(view, view->lineno - steps - view->offset);

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

		move_view(view, steps);
		return;
	}

	/* Draw the current line */
	view->draw(view, view->lineno - view->offset);

	redrawwin(view->win);
	wrefresh(view->win);

	report_position(view, view->height);
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

	if (redraw_from >= 0) {
		/* If this is an incremental update, redraw the previous line
		 * since for commits some members could have changed. */
		if (redraw_from > 0)
			redraw_from--;

		/* Incrementally draw avoids flickering. */
		redraw_view_from(view, redraw_from);
	}

	if (ferror(view->pipe)) {
		report("failed to read %s: %s", view->cmd, strerror(errno));
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
	struct view *view = &views[VIEW_OFFSET(request)];
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
		if (!view->cmd) {
			report("%s", HELP);
		} else {
			/* Clear the old view and let the incremental updating
			 * refill the screen. */
			wclear(view->win);
			report("loading...");
		}
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

	case REQ_LINE_NUMBER:
		opt_line_number = !opt_line_number;
		redraw_view(view);
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
	int linelen;
	int attr;

	if (view->offset + lineno >= view->lines)
		return FALSE;

	line = view->line[view->offset + lineno];
	type = get_line_type(line);

	if (view->offset + lineno == view->lineno) {
		if (type == LINE_COMMIT)
			string_copy(commit_id, line + 7);
		type = LINE_CURSOR;
	}

	attr = get_line_attr(type);
	wattrset(view->win, attr);

	linelen = strlen(line);
	linelen = MIN(linelen, view->width);

	if (opt_line_number) {
		unsigned int real_lineno = view->offset + lineno + 1;
		int col = 1;

		if (real_lineno == 1 || (real_lineno % NUMBER_INTERVAL) == 0)
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
		/* No empty lines makes cursor drawing and clearing implicit. */
		if (!*line)
			line = " ", linelen = 1;
		mvwaddnstr(view->win, lineno, 0, line, linelen);
	}

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
	char buf[21];
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
		string_copy(commit_id, commit->id);
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
		/* Fill in the commit title */
		commit = view->line[view->lines - 1];
		if (commit->title[0] ||
		    strncmp(line, "    ", 4) ||
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

	werase(title_win);
	wmove(title_win, 0, 0);
	wprintw(title_win, "commit %s", commit_id);
	wrefresh(title_win);

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
		die("opts");
	}

	request = opt_request;

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
 * BUGS
 * ----
 * Known bugs and problems:
 *
 * Redrawing of the main view while loading::
 *	If only part of a commit has been parsed not all fields will be visible
 *	or even redrawn when the whole commit have loaded. This can be
 *	triggered when continuously moving to the last line. Use 'r' to redraw
 *	the whole screen.
 *
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
 * Copyright (c) Jonas Fonseca, 2006
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
