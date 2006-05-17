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
 * tig [options] show [git show options]
 * tig [options] <    [git command output]
 *
 * DESCRIPTION
 * -----------
 * Browse changes in a git repository. Additionally, tig(1) can also act
 * as a pager for output of various git commands.
 *
 * When browsing repositories, tig(1) uses the underlying git commands
 * to present the user with various views, such as summarized commit log
 * and showing the commit with the log message, diffstat, and the diff.
 *
 * Using tig(1) as a pager, it will display input from stdin and try
 * to colorize it.
 **/

#ifndef	VERSION
#define VERSION	"tig-0.3"
#endif

#ifndef DEBUG
#define NDEBUG
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
static void set_nonblocking_input(bool loading);

#define ABS(x)		((x) >= 0  ? (x) : -(x))
#define MIN(x, y)	((x) < (y) ? (x) :  (y))

#define ARRAY_SIZE(x)	(sizeof(x) / sizeof(x[0]))
#define STRING_SIZE(x)	(sizeof(x) - 1)

#define SIZEOF_REF	256	/* Size of symbolic or SHA1 ID. */
#define SIZEOF_CMD	1024	/* Size of command buffer. */

/* This color name can be used to refer to the default term colors. */
#define COLOR_DEFAULT	(-1)

#define TIG_HELP	"(d)iff, (l)og, (m)ain, (q)uit, (h)elp, (Enter) show diff"

/* The format and size of the date column in the main view. */
#define DATE_FORMAT	"%Y-%m-%d %H:%M"
#define DATE_COLS	STRING_SIZE("2006-04-29 14:21 ")

/* The default interval between line numbers. */
#define NUMBER_INTERVAL	1

#define TABSIZE		8

#define	SCALE_SPLIT_VIEW(height)	((height) * 2 / 3)

/* Some ascii-shorthands fitted into the ncurses namespace. */
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
	REQ_SCREEN_RESIZE,
	REQ_SCREEN_UPDATE,
	REQ_SHOW_VERSION,
	REQ_STOP_LOADING,
	REQ_TOGGLE_LINE_NUMBERS,
	REQ_VIEW_NEXT,

	REQ_MOVE_UP,
	REQ_MOVE_UP_ENTER,
	REQ_MOVE_DOWN,
	REQ_MOVE_DOWN_ENTER,
	REQ_MOVE_PAGE_UP,
	REQ_MOVE_PAGE_DOWN,
	REQ_MOVE_FIRST_LINE,
	REQ_MOVE_LAST_LINE,

	REQ_SCROLL_LINE_UP,
	REQ_SCROLL_LINE_DOWN,
	REQ_SCROLL_PAGE_UP,
	REQ_SCROLL_PAGE_DOWN,
};

struct ref {
	char *name;		/* Ref name; tag or head names are shortened. */
	char id[41];		/* Commit SHA1 ID */
	unsigned int tag:1;	/* Is it a tag? */
	unsigned int next:1;	/* For ref lists: are there more refs? */
};

struct commit {
	char id[41];		/* SHA1 ID. */
	char title[75];		/* The first line of the commit message. */
	char author[75];	/* The author of the commit. */
	struct tm time;		/* Date from the author ident. */
	struct ref **refs;	/* Repository references; tags & branch heads. */
};


/*
 * String helpers
 */

static inline void
string_ncopy(char *dst, const char *src, int dstlen)
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

#define BUFPUT(x) do { if (bufsize < SIZEOF_CMD) buf[bufsize++] = (x); } while (0)

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

/* Option and state variables. */
static bool opt_line_number	= FALSE;
static int opt_num_interval	= NUMBER_INTERVAL;
static int opt_tab_size		= TABSIZE;
static enum request opt_request = REQ_VIEW_MAIN;
static char opt_cmd[SIZEOF_CMD]	= "";
static FILE *opt_pipe		= NULL;

/* Returns the index of log or diff command or -1 to exit. */
static bool
parse_options(int argc, char *argv[])
{
	int i;

	for (i = 1; i < argc; i++) {
		char *opt = argv[i];

		/**
		 * -l::
		 *	Start up in log view using the internal log command.
		 **/
		if (!strcmp(opt, "-l")) {
			opt_request = REQ_VIEW_LOG;
			continue;
		}

		/**
		 * -d::
		 *	Start up in diff view using the internal diff command.
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
		 * -t[NSPACES], --tab-size[=NSPACES]::
		 *	Set the number of spaces tabs should be expanded to.
		 **/
		if (!strncmp(opt, "-t", 2) ||
		    !strncmp(opt, "--tab-size", 10)) {
			char *num = opt;

			if (opt[1] == 't') {
				num = opt + 2;

			} else if (opt[STRING_SIZE("--tab-size")] == '=') {
				num = opt + STRING_SIZE("--tab-size=");
			}

			if (isdigit(*num))
				opt_tab_size = MIN(atoi(num), TABSIZE);
			continue;
		}

		/**
		 * -v, --version::
		 *	Show version and exit.
		 **/
		if (!strcmp(opt, "-v") ||
		    !strcmp(opt, "--version")) {
			printf("tig version %s\n", VERSION);
			return FALSE;
		}

		/**
		 * \--::
		 *	End of tig(1) options. Useful when specifying command
		 *	options for the main view. Example:
		 *
		 *		$ tig -- --since=1.month
		 **/
		if (!strcmp(opt, "--")) {
			i++;
			break;
		}

		/**
		 * log [git log options]::
		 *	Open log view using the given git log options.
		 *
		 * diff [git diff options]::
		 *	Open diff view using the given git diff options.
		 *
		 * show [git show options]::
		 *	Open diff view using the given git show options.
		 **/
		if (!strcmp(opt, "log") ||
		    !strcmp(opt, "diff") ||
		    !strcmp(opt, "show")) {
			opt_request = opt[0] == 'l'
				    ? REQ_VIEW_LOG : REQ_VIEW_DIFF;
			break;
		}

		/**
		 * [git log options]::
		 *	tig(1) will stop the option parsing when the first
		 *	command line parameter not starting with "-" is
		 *	encountered. All options including this one will be
		 *	passed to git log when loading the main view.
		 *	This makes it possible to say:
		 *
		 *	$ tig tag-1.0..HEAD
		 **/
		if (opt[0] && opt[0] != '-')
			break;

		die("unknown command '%s'", opt);
	}

	if (!isatty(STDIN_FILENO)) {
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
		opt_request = REQ_VIEW_PAGER;
		opt_pipe = stdin;

	} else if (i < argc) {
		size_t buf_size;

		/**
		 * Git command options
		 * ~~~~~~~~~~~~~~~~~~~
		 * All git command options specified on the command line will
		 * be passed to the given command and all will be shell quoted
		 * before they are passed to the shell.
		 *
		 * NOTE: If you specify options for the main view, you should
		 * not use the `--pretty` option as this option will be set
		 * automatically to the format expected by the main view.
		 *
		 * Example on how to open the log view and show both author and
		 * committer information:
		 *
		 *	$ tig log --pretty=fuller
		 *
		 * See the <<refspec, "Specifying revisions">> section below
		 * for an introduction to revision options supported by the git
		 * commands. For details on specific git command options, refer
		 * to the man page of the command in question.
		 **/

		if (opt_request == REQ_VIEW_MAIN)
			/* XXX: This is vulnerable to the user overriding
			 * options required for the main view parser. */
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

	return TRUE;
}


/*
 * Line-oriented content detection.
 */

#define LINE_INFO \
/*   Line type	   String to match	Foreground	Background	Attributes
 *   ---------     ---------------      ----------      ----------      ---------- */ \
/* Diff markup */ \
LINE(DIFF,	   "diff --git ",	COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(DIFF_INDEX,   "index ",		COLOR_BLUE,	COLOR_DEFAULT,	0), \
LINE(DIFF_CHUNK,   "@@",		COLOR_MAGENTA,	COLOR_DEFAULT,	0), \
LINE(DIFF_ADD,	   "+",			COLOR_GREEN,	COLOR_DEFAULT,	0), \
LINE(DIFF_DEL,	   "-",			COLOR_RED,	COLOR_DEFAULT,	0), \
LINE(DIFF_OLDMODE, "old file mode ",	COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(DIFF_NEWMODE, "new file mode ",	COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(DIFF_COPY,	   "copy ",		COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(DIFF_RENAME,  "rename ",		COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(DIFF_SIM,	   "similarity ",	COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(DIFF_DISSIM,  "dissimilarity ",	COLOR_YELLOW,	COLOR_DEFAULT,	0), \
/* Pretty print commit header */ \
LINE(PP_AUTHOR,	   "Author: ",		COLOR_CYAN,	COLOR_DEFAULT,	0), \
LINE(PP_COMMIT,	   "Commit: ",		COLOR_MAGENTA,	COLOR_DEFAULT,	0), \
LINE(PP_MERGE,	   "Merge: ",		COLOR_BLUE,	COLOR_DEFAULT,	0), \
LINE(PP_DATE,	   "Date:   ",		COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(PP_ADATE,	   "AuthorDate: ",	COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(PP_CDATE,	   "CommitDate: ",	COLOR_YELLOW,	COLOR_DEFAULT,	0), \
/* Raw commit header */ \
LINE(COMMIT,	   "commit ",		COLOR_GREEN,	COLOR_DEFAULT,	0), \
LINE(PARENT,	   "parent ",		COLOR_BLUE,	COLOR_DEFAULT,	0), \
LINE(TREE,	   "tree ",		COLOR_BLUE,	COLOR_DEFAULT,	0), \
LINE(AUTHOR,	   "author ",		COLOR_CYAN,	COLOR_DEFAULT,	0), \
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
LINE(MAIN_DELIM,   "",			COLOR_MAGENTA,	COLOR_DEFAULT,	0), \
LINE(MAIN_TAG,     "",			COLOR_MAGENTA,	COLOR_DEFAULT,	A_BOLD), \
LINE(MAIN_REF,     "",			COLOR_CYAN,	COLOR_DEFAULT,	A_BOLD),

enum line_type {
#define LINE(type, line, fg, bg, attr) \
	LINE_##type
	LINE_INFO
#undef	LINE
};

struct line_info {
	const char *line;	/* The start of line to match. */
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


/**
 * ENVIRONMENT VARIABLES
 * ---------------------
 * Several options related to the interface with git can be configured
 * via environment options.
 *
 * Repository references
 * ~~~~~~~~~~~~~~~~~~~~~
 * Commits that are referenced by tags and branch heads will be marked
 * by the reference name surrounded by '[' and ']':
 *
 *	2006-03-26 19:42 Petr Baudis         | [cogito-0.17.1] Cogito 0.17.1
 *
 * If you want to filter out certain directories under `.git/refs/`, say
 * `tmp` you can do it by setting the following variable:
 *
 *	$ TIG_LS_REMOTE="git ls-remote . | sed /\/tmp\//d" tig
 *
 * Or set the variable permanently in your environment.
 *
 * TIG_LS_REMOTE::
 *	Set command for retrieving all repository references. The command
 *	should output data in the same format as git-ls-remote(1).
 **/

#define TIG_LS_REMOTE \
	"git ls-remote . 2>/dev/null"

/**
 * [[view-commands]]
 * View commands
 * ~~~~~~~~~~~~~
 * It is possible to alter which commands are used for the different views.
 * If for example you prefer commits in the main view to be sorted by date
 * and only show 500 commits, use:
 *
 *	$ TIG_MAIN_CMD="git log --date-order -n500 --pretty=raw %s" tig
 *
 * Or set the variable permanently in your environment.
 *
 * Notice, how `%s` is used to specify the commit reference. There can
 * be a maximum of 5 `%s` ref specifications.
 *
 * TIG_DIFF_CMD::
 *	The command used for the diff view. By default, git show is used
 *	as a backend.
 *
 * TIG_LOG_CMD::
 *	The command used for the log view. If you prefer to have both
 *	author and committer shown in the log view be sure to pass
 *	`--pretty=fuller` to git log.
 *
 * TIG_MAIN_CMD::
 *	The command used for the main view. Note, you must always specify
 *	the option: `--pretty=raw` since the main view parser expects to
 *	read that format.
 **/

#define TIG_DIFF_CMD \
	"git show --patch-with-stat --find-copies-harder -B -C %s"

#define TIG_LOG_CMD	\
	"git log --cc --stat -n100 %s"

#define TIG_MAIN_CMD \
	"git log --topo-order --stat --pretty=raw %s"

/* ... silently ignore that the following are also exported. */

#define TIG_HELP_CMD \
	"man tig 2>/dev/null"

#define TIG_PAGER_CMD \
	""

/**
 * The viewer
 * ----------
 *
 * tig(1) presents various 'views' of a repository. Each view is based on output
 * from an external command, most often 'git log', 'git diff', or 'git show'.
 *
 * The main view::
 *	Is the default view, and it shows a one line summary of each commit
 *	in the chosen list of revisions. The summary includes commit date,
 *	author, and the first line of the log message. Additionally, any
 *	repository references, such as tags, will be shown.
 *
 * The log view::
 *	Presents a more rich view of the revision log showing the whole log
 *	message and the diffstat.
 *
 * The diff view::
 *	Shows either the diff of the current working tree, that is, what
 *	has changed since the last commit, or the commit diff complete
 *	with log message, diffstat and diff.
 *
 * The pager view::
 *	Is used for displaying both input from stdin and output from git
 *	commands entered in the internal prompt.
 *
 * The help view::
 *	Displays the information from the tig(1) man page. For the help view
 *	to work you need to have the tig(1) man page installed.
 **/

struct view {
	const char *name;	/* View name */
	const char *cmd_fmt;	/* Default command line format */
	const char *cmd_env;	/* Command line set via environment */
	const char *id;		/* Points to either of ref_{head,commit} */
	size_t objsize;		/* Size of objects in the line index */

	struct view_ops {
		/* What type of content being displayed. Used in the
		 * title bar. */
		const char *type;
		/* Draw one line; @lineno must be < view->height. */
		bool (*draw)(struct view *view, unsigned int lineno);
		/* Read one line; updates view->line. */
		bool (*read)(struct view *view, char *line);
		/* Depending on view, change display based on current line. */
		bool (*enter)(struct view *view);
	} *ops;

	char cmd[SIZEOF_CMD];	/* Command buffer */
	char ref[SIZEOF_REF];	/* Hovered commit reference */
	char vid[SIZEOF_REF];	/* View ID. Set to id member when updating. */

	int height, width;	/* The width and height of the main window */
	WINDOW *win;		/* The main window */
	WINDOW *title;		/* The title window living below the main window */

	/* Navigation */
	unsigned long offset;	/* Offset of the window top */
	unsigned long lineno;	/* Current line number */

	/* Buffering */
	unsigned long lines;	/* Total number of lines */
	void **line;		/* Line index; each line contains user data */
	unsigned int digits;	/* Number of digits in the lines member. */

	/* Loading */
	FILE *pipe;
	time_t start_time;
};

static struct view_ops pager_ops;
static struct view_ops main_ops;

static char ref_head[SIZEOF_REF]	= "HEAD";
static char ref_commit[SIZEOF_REF]	= "HEAD";

#define VIEW_STR(name, cmd, env, ref, objsize, ops) \
	{ name, cmd, #env, ref, objsize, ops }

#define VIEW_(id, name, ops, ref, objsize) \
	VIEW_STR(name, TIG_##id##_CMD,  TIG_##id##_CMD, ref, objsize, ops)

static struct view views[] = {
	VIEW_(MAIN,  "main",  &main_ops,  ref_head,   sizeof(struct commit)),
	VIEW_(DIFF,  "diff",  &pager_ops, ref_commit, sizeof(char)),
	VIEW_(LOG,   "log",   &pager_ops, ref_head,   sizeof(char)),
	VIEW_(HELP,  "help",  &pager_ops, ref_head,   sizeof(char)),
	VIEW_(PAGER, "pager", &pager_ops, "static",   sizeof(char)),
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
		/* Keep the size of the all view windows one lager than is
		 * required. This makes current line management easier when the
		 * cursor will go outside the window. */
		if (!view->win) {
			view->win = newwin(view->height + 1, 0, offset, 0);
			if (!view->win)
				die("Failed to create %s view", view->name);

			scrollok(view->win, TRUE);

			view->title = newwin(1, 0, offset + view->height, 0);
			if (!view->title)
				die("Failed to create title window");

		} else {
			wresize(view->win, view->height + 1, view->width);
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
		wprintw(view->title, "[%s] %s", view->name, view->ref);
	else
		wprintw(view->title, "[%s]", view->name);

	if (view->lines) {
		wprintw(view->title, " - %s %d of %d (%d%%)",
			view->ops->type,
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
		if (view->lineno == view->offset + view->height) {
			/* Clear the hidden line so it doesn't show if the view
			 * is scrolled up. */
			wmove(view->win, view->height, 0);
			wclrtoeol(view->win);
		}
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
			report("Cannot scroll beyond the last line");
			return;
		}
		break;

	case REQ_SCROLL_PAGE_UP:
		lines = view->height;
	case REQ_SCROLL_LINE_UP:
		if (lines > view->offset)
			lines = view->offset;

		if (lines == 0) {
			report("Cannot scroll beyond the first line");
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
	case REQ_MOVE_UP_ENTER:
		steps = -1;
		break;

	case REQ_MOVE_DOWN:
	case REQ_MOVE_DOWN_ENTER:
		steps = 1;
		break;

	default:
		die("request %d not handled in switch", request);
	}

	if (steps <= 0 && view->lineno == 0) {
		report("Cannot move beyond the first line");
		return;

	} else if (steps >= 0 && view->lineno + 1 >= view->lines) {
		report("Cannot move beyond the last line");
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
	const char *id = view->id;

	if (opt_cmd[0]) {
		string_copy(view->cmd, opt_cmd);
		opt_cmd[0] = 0;
		/* When running random commands, the view ref could have become
		 * invalid so clear it. */
		view->ref[0] = 0;
	} else {
		const char *format = view->cmd_env ? view->cmd_env : view->cmd_fmt;

		if (snprintf(view->cmd, sizeof(view->cmd), format,
			     id, id, id, id, id) >= sizeof(view->cmd))
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
	if (view->pipe == stdin)
		fclose(view->pipe);
	else
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
	unsigned long lines = view->height;
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
		int linelen = strlen(line);

		if (linelen)
			line[linelen - 1] = 0;

		if (!view->ops->read(view, line))
			goto alloc_error;

		if (lines-- == 1)
			break;
	}

	{
		int digits;

		lines = view->lines;
		for (digits = 0; lines; digits++)
			lines /= 10;

		/* Keep the displayed view in sync with line number scaling. */
		if (digits != view->digits) {
			view->digits = digits;
			redraw_from = 0;
		}
	}

	if (redraw_from >= 0) {
		/* If this is an incremental update, redraw the previous line
		 * since for commits some members could have changed when
		 * loading the main view. */
		if (redraw_from > 0)
			redraw_from--;

		/* Incrementally draw avoids flickering. */
		redraw_view_from(view, redraw_from);
	}

	/* Update the title _after_ the redraw so that if the redraw picks up a
	 * commit reference in view->ref it'll be available here. */
	update_view_title(view);

	if (ferror(view->pipe)) {
		report("Failed to read: %s", strerror(errno));
		goto end;

	} else if (feof(view->pipe)) {
		time_t secs = time(NULL) - view->start_time;

		if (view == VIEW(REQ_VIEW_HELP)) {
			const char *msg = TIG_HELP;

			if (view->lines == 0) {
				/* Slightly ugly, but abusing view->ref keeps
				 * the error message. */
				string_copy(view->ref, "No help available");
				msg = "The tig(1) manpage is not installed";
			}

			report("%s", msg);
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
		    !strcmp(view->vid, prev->vid)) {
			current_view = nviews;
			/* Blur out the title of the previous view. */
			update_view_title(prev);
			report("");
			return;
		}
	}

	if (view == prev && nviews == 1 && !reload) {
		report("Already in %s view", view->name);
		return;
	}

	if ((reload || strcmp(view->vid, view->id)) &&
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

	if (split && prev->lineno - prev->offset >= prev->height) {
		/* Take the title line into account. */
		int lines = prev->lineno - prev->offset - prev->height + 1;

		/* Scroll the view that was split if the current line is
		 * outside the new limited view. */
		do_scroll_view(prev, lines);
	}

	if (prev && view != prev) {
		/* "Blur" the previous view. */
		if (!backgrounded)
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
		if (view == VIEW(REQ_VIEW_HELP))
			report("%s", TIG_HELP);
		else
			report("");
	}

	/* If the view is backgrounded the above calls to report()
	 * won't redraw the view title. */
	if (backgrounded)
		update_view_title(view);
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

	case REQ_MOVE_UP_ENTER:
	case REQ_MOVE_DOWN_ENTER:
		move_view(view, request);
		/* Fall-through */

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
		report("");
		break;
	}
	case REQ_TOGGLE_LINE_NUMBERS:
		opt_line_number = !opt_line_number;
		redraw_view(view);
		update_view_title(view);
		break;

	case REQ_PROMPT:
		/* Always reload^Wrerun commands from the prompt. */
		open_view(view, opt_request, OPEN_RELOAD);
		break;

	case REQ_STOP_LOADING:
		foreach_view (view, i) {
			if (view->pipe)
				report("Stopped loaded the %s view", view->name),
			end_update(view);
		}
		break;

	case REQ_SHOW_VERSION:
		report("Version: %s", VERSION);
		return TRUE;

	case REQ_SCREEN_RESIZE:
		resize_display();
		/* Fall-through */
	case REQ_SCREEN_REDRAW:
		foreach_view (view, i) {
			redraw_view(view);
			update_view_title(view);
		}
		break;

	case REQ_SCREEN_UPDATE:
		doupdate();
		return TRUE;

	case REQ_QUIT:
		return FALSE;

	default:
		/* An unknown key will show most commonly used commands. */
		report("Unknown key, press 'h' for help");
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

	wmove(view->win, lineno, 0);

	if (view->offset + lineno == view->lineno) {
		if (type == LINE_COMMIT) {
			string_copy(view->ref, line + 7);
			string_copy(ref_commit, view->ref);
		}

		type = LINE_CURSOR;
		wchgat(view->win, -1, 0, type, NULL);
	}

	attr = get_line_attr(type);
	wattrset(view->win, attr);

	linelen = strlen(line);

	if (opt_line_number || opt_tab_size < TABSIZE) {
		static char spaces[] = "                    ";
		int col_offset = 0, col = 0;

		if (opt_line_number) {
			unsigned long real_lineno = view->offset + lineno + 1;

			if (real_lineno == 1 ||
			    (real_lineno % opt_num_interval) == 0) {
				wprintw(view->win, "%.*d", view->digits, real_lineno);

			} else {
				waddnstr(view->win, spaces,
					 MIN(view->digits, STRING_SIZE(spaces)));
			}
			waddstr(view->win, ": ");
			col_offset = view->digits + 2;
		}

		while (line && col_offset + col < view->width) {
			int cols_max = view->width - col_offset - col;
			char *text = line;
			int cols;

			if (*line == '\t') {
				assert(sizeof(spaces) > TABSIZE);
				line++;
				text = spaces;
				cols = opt_tab_size - (col % opt_tab_size);

			} else {
				line = strchr(line, '\t');
				cols = line ? line - text : strlen(text);
			}

			waddnstr(view->win, text, MIN(cols, cols_max));
			col += cols;
		}

	} else {
		int col = 0, pos = 0;

		for (; pos < linelen && col < view->width; pos++, col++)
			if (line[pos] == '\t')
				col += TABSIZE - (col % TABSIZE) - 1;

		waddnstr(view->win, line, pos);
	}

	return TRUE;
}

static bool
pager_read(struct view *view, char *line)
{
	/* Compress empty lines in the help view. */
	if (view == VIEW(REQ_VIEW_HELP) &&
	    !*line &&
	    view->lines &&
	    !*((char *) view->line[view->lines - 1]))
		return TRUE;

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
		if (view == VIEW(REQ_VIEW_LOG))
			open_view(view, REQ_VIEW_DIFF, OPEN_SPLIT | OPEN_BACKGROUNDED);
		else
			open_view(view, REQ_VIEW_DIFF, OPEN_DEFAULT);
	}

	return TRUE;
}

static struct view_ops pager_ops = {
	"line",
	pager_draw,
	pager_read,
	pager_enter,
};


static struct ref **get_refs(char *id);

static bool
main_draw(struct view *view, unsigned int lineno)
{
	char buf[DATE_COLS + 1];
	struct commit *commit;
	enum line_type type;
	int col = 0;
	size_t timelen;

	if (view->offset + lineno >= view->lines)
		return FALSE;

	commit = view->line[view->offset + lineno];
	if (!*commit->author)
		return FALSE;

	wmove(view->win, lineno, col);

	if (view->offset + lineno == view->lineno) {
		string_copy(view->ref, commit->id);
		string_copy(ref_commit, view->ref);
		type = LINE_CURSOR;
		wattrset(view->win, get_line_attr(type));
		wchgat(view->win, -1, 0, type, NULL);

	} else {
		type = LINE_MAIN_COMMIT;
		wattrset(view->win, get_line_attr(LINE_MAIN_DATE));
	}

	timelen = strftime(buf, sizeof(buf), DATE_FORMAT, &commit->time);
	waddnstr(view->win, buf, timelen);
	waddstr(view->win, " ");

	col += DATE_COLS;
	wmove(view->win, lineno, col);
	if (type != LINE_CURSOR)
		wattrset(view->win, get_line_attr(LINE_MAIN_AUTHOR));

	if (strlen(commit->author) > 19) {
		waddnstr(view->win, commit->author, 18);
		if (type != LINE_CURSOR)
			wattrset(view->win, get_line_attr(LINE_MAIN_DELIM));
		waddch(view->win, '~');
	} else {
		waddstr(view->win, commit->author);
	}

	col += 20;
	if (type != LINE_CURSOR)
		wattrset(view->win, A_NORMAL);

	mvwaddch(view->win, lineno, col, ACS_LTEE);
	wmove(view->win, lineno, col + 2);
	col += 2;

	if (commit->refs) {
		size_t i = 0;

		do {
			if (type == LINE_CURSOR)
				;
			else if (commit->refs[i]->tag)
				wattrset(view->win, get_line_attr(LINE_MAIN_TAG));
			else
				wattrset(view->win, get_line_attr(LINE_MAIN_REF));
			waddstr(view->win, "[");
			waddstr(view->win, commit->refs[i]->name);
			waddstr(view->win, "]");
			if (type != LINE_CURSOR)
				wattrset(view->win, A_NORMAL);
			waddstr(view->win, " ");
			col += strlen(commit->refs[i]->name) + STRING_SIZE("[] ");
		} while (commit->refs[i++]->next);
	}

	if (type != LINE_CURSOR)
		wattrset(view->win, get_line_attr(type));

	{
		int titlelen = strlen(commit->title);

		if (col + titlelen > view->width)
			titlelen = view->width - col;

		waddnstr(view->win, commit->title, titlelen);
	}

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
		commit->refs = get_refs(commit->id);
		break;

	case LINE_AUTHOR:
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
		/* FIXME: More gracefull handling of titles; append "..." to
		 * shortened titles, etc. */
		if (strncmp(line, "    ", 4) ||
		    isspace(line[4]))
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
	"commit",
	main_draw,
	main_read,
	main_enter,
};


/**
 * KEYS
 * ----
 * Below the default key bindings are shown.
 **/

struct keymap {
	int alias;
	int request;
};

static struct keymap keymap[] = {
	/**
	 * View switching
	 * ~~~~~~~~~~~~~~
	 * m::
	 *	Switch to main view.
	 * d::
	 *	Switch to diff view.
	 * l::
	 *	Switch to log view.
	 * p::
	 *	Switch to pager view.
	 * h::
	 *	Show man page.
	 * Return::
	 *	If on a commit line show the commit diff. Additionally, if in
	 *	main or log view this will split the view. To open the commit
	 *	diff in full size view either use 'd' or press Return twice.
	 * Tab::
	 *	Switch to next view.
	 **/
	{ 'm',		REQ_VIEW_MAIN },
	{ 'd',		REQ_VIEW_DIFF },
	{ 'l',		REQ_VIEW_LOG },
	{ 'p',		REQ_VIEW_PAGER },
	{ 'h',		REQ_VIEW_HELP },

	{ KEY_TAB,	REQ_VIEW_NEXT },
	{ KEY_RETURN,	REQ_ENTER },

	/**
	 * Cursor navigation
	 * ~~~~~~~~~~~~~~~~~
	 * Up::
	 *	Move cursor one line up.
	 * Down::
	 *	Move cursor one line down.
	 * k::
	 *	Move cursor one line up and enter. When used in the main view
	 *	this will always show the diff of the current commit in the
	 *	split diff view.
	 * j::
	 *	Move cursor one line down and enter.
	 * PgUp::
	 *	Move cursor one page up.
	 * PgDown::
	 *	Move cursor one page down.
	 * Home::
	 *	Jump to first line.
	 * End::
	 *	Jump to last line.
	 **/
	{ KEY_UP,	REQ_MOVE_UP },
	{ KEY_DOWN,	REQ_MOVE_DOWN },
	{ 'k',		REQ_MOVE_UP_ENTER },
	{ 'j',		REQ_MOVE_DOWN_ENTER },
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
	 * q::
	 *	Quit
	 * r::
	 *	Redraw screen.
	 * z::
	 *	Stop all background loading. This can be useful if you use
	 *	tig(1) in a repository with a long history without limiting
	 *	the revision log.
	 * v::
	 *	Show version.
	 * n::
	 *	Toggle line numbers on/off.
	 * ':'::
	 *	Open prompt. This allows you to specify what git command
	 *	to run. Example:
	 *
	 *	:log -p
	 **/
	{ 'q',		REQ_QUIT },
	{ 'z',		REQ_STOP_LOADING },
	{ 'v',		REQ_SHOW_VERSION },
	{ 'r',		REQ_SCREEN_REDRAW },
	{ 'n',		REQ_TOGGLE_LINE_NUMBERS },
	{ ':',		REQ_PROMPT },

	/* wgetch() with nodelay() enabled returns ERR when there's no input. */
	{ ERR,		REQ_SCREEN_UPDATE },

	/* Use the ncurses SIGWINCH handler. */
	{ KEY_RESIZE,	REQ_SCREEN_RESIZE },
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
 * Status management
 */

/* Whether or not the curses interface has been initialized. */
bool cursed = FALSE;

/* The status window is used for polling keystrokes. */
static WINDOW *status_win;

/* Update status and title window. */
static void
report(const char *msg, ...)
{
	static bool empty = TRUE;
	struct view *view = display[current_view];

	if (!empty || *msg) {
		va_list args;

		va_start(args, msg);

		werase(status_win);
		wmove(status_win, 0, 0);
		if (*msg) {
			vwprintw(status_win, msg, args);
			empty = FALSE;
		} else {
			empty = TRUE;
		}
		wrefresh(status_win);

		va_end(args);
	}

	update_view_title(view);

	/* Move the cursor to the right-most column of the cursor line.
	 *
	 * XXX: This could turn out to be a bit expensive, but it ensures that
	 * the cursor does not jump around. */
	if (view->lines) {
		wmove(view->win, view->lineno - view->offset, view->width - 1);
		wrefresh(view->win);
	}
}

/* Controls when nodelay should be in effect when polling user input. */
static void
set_nonblocking_input(bool loading)
{
	static unsigned int loading_views;

	if ((loading == FALSE && loading_views-- == 1) ||
	    (loading == TRUE  && loading_views++ == 0))
		nodelay(status_win, loading);
}

static void
init_display(void)
{
	int x, y;

	/* Initialize the curses library */
	if (isatty(STDIN_FILENO)) {
		cursed = !!initscr();
	} else {
		/* Leave stdin and stdout alone when acting as a pager. */
		FILE *io = fopen("/dev/tty", "r+");

		cursed = !!newterm(NULL, io, io);
	}

	if (!cursed)
		die("Failed to initialize curses");

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
 * Repository references
 */

static struct ref *refs;
static size_t refs_size;

static struct ref **
get_refs(char *id)
{
	struct ref **id_refs = NULL;
	size_t id_refs_size = 0;
	size_t i;

	for (i = 0; i < refs_size; i++) {
		struct ref **tmp;

		if (strcmp(id, refs[i].id))
			continue;

		tmp = realloc(id_refs, (id_refs_size + 1) * sizeof(*id_refs));
		if (!tmp) {
			if (id_refs)
				free(id_refs);
			return NULL;
		}

		id_refs = tmp;
		if (id_refs_size > 0)
			id_refs[id_refs_size - 1]->next = 1;
		id_refs[id_refs_size] = &refs[i];

		/* XXX: The properties of the commit chains ensures that we can
		 * safely modify the shared ref. The repo references will
		 * always be similar for the same id. */
		id_refs[id_refs_size]->next = 0;
		id_refs_size++;
	}

	return id_refs;
}

static int
load_refs(void)
{
	const char *cmd_env = getenv("TIG_LS_REMOTE");
	const char *cmd = cmd_env && *cmd_env ? cmd_env : TIG_LS_REMOTE;
	FILE *pipe = popen(cmd, "r");
	char buffer[BUFSIZ];
	char *line;

	if (!pipe)
		return ERR;

	while ((line = fgets(buffer, sizeof(buffer), pipe))) {
		char *name = strchr(line, '\t');
		struct ref *ref;
		int namelen;
		bool tag = FALSE;
		bool tag_commit = FALSE;

		if (!name)
			continue;

		*name++ = 0;
		namelen = strlen(name) - 1;

		/* Commits referenced by tags has "^{}" appended. */
		if (name[namelen - 1] == '}') {
			while (namelen > 0 && name[namelen] != '^')
				namelen--;
			if (namelen > 0)
				tag_commit = TRUE;
		}
		name[namelen] = 0;

		if (!strncmp(name, "refs/tags/", STRING_SIZE("refs/tags/"))) {
			if (!tag_commit)
				continue;
			name += STRING_SIZE("refs/tags/");
			tag = TRUE;

		} else if (!strncmp(name, "refs/heads/", STRING_SIZE("refs/heads/"))) {
			name += STRING_SIZE("refs/heads/");

		} else if (!strcmp(name, "HEAD")) {
			continue;
		}

		refs = realloc(refs, sizeof(*refs) * (refs_size + 1));
		if (!refs)
			return ERR;

		ref = &refs[refs_size++];
		ref->tag = tag;
		ref->name = strdup(name);
		if (!ref->name)
			return ERR;

		string_copy(ref->id, line);
	}

	if (ferror(pipe))
		return ERR;

	pclose(pipe);

	if (refs_size == 0)
		die("Not a git repository");

	return OK;
}

/*
 * Main
 */

#if __GNUC__ >= 3
#define __NORETURN __attribute__((__noreturn__))
#else
#define __NORETURN
#endif

static void __NORETURN
quit(int sig)
{
	/* XXX: Restore tty modes and let the OS cleanup the rest! */
	if (cursed)
		endwin();
	exit(0);
}

static void __NORETURN
die(const char *err, ...)
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
	struct view *view;
	enum request request;
	size_t i;

	signal(SIGINT, quit);

	if (!parse_options(argc, argv))
		return 0;

	if (load_refs() == ERR)
		die("Failed to load refs.");

	for (i = 0; i < ARRAY_SIZE(views) && (view = &views[i]); i++)
		view->cmd_env = getenv(view->cmd_env);

	request = opt_request;

	init_display();

	while (view_driver(display[current_view], request)) {
		int key;
		int i;

		foreach_view (view, i)
			update_view(view);

		/* Refresh, accept single keystroke of input */
		key = wgetch(status_win);
		request = get_request(key);

		/* Some low-level request handling. This keeps access to
		 * status_win restricted. */
		switch (request) {
		case REQ_PROMPT:
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
			break;

		case REQ_SCREEN_RESIZE:
		{
			int height, width;

			getmaxyx(stdscr, height, width);

			/* Resize the status view and let the view driver take
			 * care of resizing the displayed views. */
			wresize(status_win, 1, width);
			mvwin(status_win, height - 1, 0);
			wrefresh(status_win);
			break;
		}
		default:
			break;
		}
	}

	quit(0);

	return 0;
}

/**
 * [[refspec]]
 * Revision specification
 * ----------------------
 * This section describes various ways to specify what revisions to display
 * or otherwise limit the view to. tig(1) does not itself parse the described
 * revision options so refer to the relevant git man pages for futher
 * information. Relevant man pages besides git-log(1) are git-diff(1) and
 * git-rev-list(1).
 *
 * You can tune the interaction with git by making use of the options
 * explained in this section. For example, by configuring the environment
 * variables described in the  <<view-commands, "View commands">> section.
 *
 * Limit by path name
 * ~~~~~~~~~~~~~~~~~~
 * If you are interested only in those revisions that made changes to a
 * specific file (or even several files) list the files like this:
 *
 *	$ tig log Makefile README
 *
 * To avoid ambiguity with repository references such as tag name, be sure
 * to separate file names from other git options using "\--". So if you
 * have a file named 'master' it will clash with the reference named
 * 'master', and thus you will have to use:
 *
 *	$ tig log -- master
 *
 * NOTE: For the main view, avoiding ambiguity will in some cases require
 * you to specify two "\--" options. The first will make tig(1) stop
 * option processing and the latter will be passed to git log.
 *
 * Limit by date or number
 * ~~~~~~~~~~~~~~~~~~~~~~~
 * To speed up interaction with git, you can limit the amount of commits
 * to show both for the log and main view. Either limit by date using
 * e.g. `--since=1.month` or limit by the number of commits using `-n400`.
 *
 * If you are only interested in changed that happened between two dates
 * you can use:
 *
 *	$ tig -- --after="May 5th" --before="2006-05-16 15:44"
 *
 * NOTE: If you want to avoid having to quote dates containing spaces you
 * can use "." instead, e.g. `--after=May.5th`.
 *
 * Limiting by commit ranges
 * ~~~~~~~~~~~~~~~~~~~~~~~~~
 * Alternatively, commits can be limited to a specific range, such as
 * "all commits between 'tag-1.0' and 'tag-2.0'". For example:
 *
 *	$ tig log tag-1.0..tag-2.0
 *
 * This way of commit limiting makes it trivial to only browse the commits
 * which haven't been pushed to a remote branch. Assuming 'origin' is your
 * upstream remote branch, using:
 *
 *	$ tig log origin..HEAD
 *
 * will list what will be pushed to the remote branch. Optionally, the ending
 * 'HEAD' can be left out since it is implied.
 *
 * Limiting by reachability
 * ~~~~~~~~~~~~~~~~~~~~~~~~
 * Git interprets the range specifier "tag-1.0..tag-2.0" as
 * "all commits reachable from 'tag-2.0' but not from 'tag-1.0'".
 * Where reachability refers to what commits are ancestors (or part of the
 * history) of the branch or tagged revision in question.
 *
 * If you prefer to specify which commit to preview in this way use the
 * following:
 *
 *	$ tig log tag-2.0 ^tag-1.0
 *
 * You can think of '^' as a negation operator. Using this alternate syntax,
 * it is possible to further prune commits by specifying multiple branch
 * cut offs.
 *
 * Combining revisions specification
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * Revisions options can to some degree be combined, which makes it possible
 * to say "show at most 20 commits from within the last month that changed
 * files under the Documentation/ directory."
 *
 *	$ tig -- --since=1.month -n20 -- Documentation/
 *
 * Examining all repository references
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * In some cases, it can be useful to query changes across all references
 * in a repository. An example is to ask "did any line of development in
 * this repository change a particular file within the last week". This
 * can be accomplished using:
 *
 *	$ tig -- --all --since=1.week -- Makefile
 *
 * BUGS
 * ----
 * Known bugs and problems:
 *
 * - If the screen width is very small the main view can draw
 *   outside the current view causing bad wrapping. Same goes
 *   for title and status windows.
 *
 * TODO
 * ----
 * Features that should be explored.
 *
 * - Searching.
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
 * qgit(1): git repository browser written using c++/Qt,
 * gitview(1): git repository browser written using python/gtk.
 **/
