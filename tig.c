/* Copyright (c) 2006-2007 Jonas Fonseca <fonseca@diku.dk>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifndef TIG_VERSION
#define TIG_VERSION "unknown-version"
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
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

#include <regex.h>

#include <locale.h>
#include <langinfo.h>
#include <iconv.h>

#include <curses.h>

#if __GNUC__ >= 3
#define __NORETURN __attribute__((__noreturn__))
#else
#define __NORETURN
#endif

static void __NORETURN die(const char *err, ...);
static void report(const char *msg, ...);
static int read_properties(FILE *pipe, const char *separators, int (*read)(char *, size_t, char *, size_t));
static void set_nonblocking_input(bool loading);
static size_t utf8_length(const char *string, size_t max_width, int *coloffset, int *trimmed);

#define ABS(x)		((x) >= 0  ? (x) : -(x))
#define MIN(x, y)	((x) < (y) ? (x) :  (y))

#define ARRAY_SIZE(x)	(sizeof(x) / sizeof(x[0]))
#define STRING_SIZE(x)	(sizeof(x) - 1)

#define SIZEOF_STR	1024	/* Default string size. */
#define SIZEOF_REF	256	/* Size of symbolic or SHA1 ID. */
#define SIZEOF_REV	41	/* Holds a SHA-1 and an ending NUL */

/* Revision graph */

#define REVGRAPH_INIT	'I'
#define REVGRAPH_MERGE	'M'
#define REVGRAPH_BRANCH	'+'
#define REVGRAPH_COMMIT	'*'
#define REVGRAPH_LINE	'|'

#define SIZEOF_REVGRAPH	19	/* Size of revision ancestry graphics. */

/* This color name can be used to refer to the default term colors. */
#define COLOR_DEFAULT	(-1)

#define ICONV_NONE	((iconv_t) -1)
#ifndef ICONV_CONST
#define ICONV_CONST	/* nothing */
#endif

/* The format and size of the date column in the main view. */
#define DATE_FORMAT	"%Y-%m-%d %H:%M"
#define DATE_COLS	STRING_SIZE("2006-04-29 14:21 ")

#define AUTHOR_COLS	20

/* The default interval between line numbers. */
#define NUMBER_INTERVAL	1

#define TABSIZE		8

#define	SCALE_SPLIT_VIEW(height)	((height) * 2 / 3)

#ifndef GIT_CONFIG
#define GIT_CONFIG "git config"
#endif

#define TIG_LS_REMOTE \
	"git ls-remote $(git rev-parse --git-dir) 2>/dev/null"

#define TIG_DIFF_CMD \
	"git show --root --patch-with-stat --find-copies-harder -B -C %s 2>/dev/null"

#define TIG_LOG_CMD	\
	"git log --cc --stat -n100 %s 2>/dev/null"

#define TIG_MAIN_CMD \
	"git log --topo-order --pretty=raw %s 2>/dev/null"

#define TIG_TREE_CMD	\
	"git ls-tree %s %s"

#define TIG_BLOB_CMD	\
	"git cat-file blob %s"

/* XXX: Needs to be defined to the empty string. */
#define TIG_HELP_CMD	""
#define TIG_PAGER_CMD	""
#define TIG_STATUS_CMD	""
#define TIG_STAGE_CMD	""

/* Some ascii-shorthands fitted into the ncurses namespace. */
#define KEY_TAB		'\t'
#define KEY_RETURN	'\r'
#define KEY_ESC		27


struct ref {
	char *name;		/* Ref name; tag or head names are shortened. */
	char id[SIZEOF_REV];	/* Commit SHA1 ID */
	unsigned int tag:1;	/* Is it a tag? */
	unsigned int remote:1;	/* Is it a remote ref? */
	unsigned int next:1;	/* For ref lists: are there more refs? */
};

static struct ref **get_refs(char *id);

struct int_map {
	const char *name;
	int namelen;
	int value;
};

static int
set_from_int_map(struct int_map *map, size_t map_size,
		 int *value, const char *name, int namelen)
{

	int i;

	for (i = 0; i < map_size; i++)
		if (namelen == map[i].namelen &&
		    !strncasecmp(name, map[i].name, namelen)) {
			*value = map[i].value;
			return OK;
		}

	return ERR;
}


/*
 * String helpers
 */

static inline void
string_ncopy_do(char *dst, size_t dstlen, const char *src, size_t srclen)
{
	if (srclen > dstlen - 1)
		srclen = dstlen - 1;

	strncpy(dst, src, srclen);
	dst[srclen] = 0;
}

/* Shorthands for safely copying into a fixed buffer. */

#define string_copy(dst, src) \
	string_ncopy_do(dst, sizeof(dst), src, sizeof(src))

#define string_ncopy(dst, src, srclen) \
	string_ncopy_do(dst, sizeof(dst), src, srclen)

#define string_copy_rev(dst, src) \
	string_ncopy_do(dst, SIZEOF_REV, src, SIZEOF_REV - 1)

#define string_add(dst, from, src) \
	string_ncopy_do(dst + (from), sizeof(dst) - (from), src, sizeof(src))

static char *
chomp_string(char *name)
{
	int namelen;

	while (isspace(*name))
		name++;

	namelen = strlen(name) - 1;
	while (namelen > 0 && isspace(name[namelen]))
		name[namelen--] = 0;

	return name;
}

static bool
string_nformat(char *buf, size_t bufsize, size_t *bufpos, const char *fmt, ...)
{
	va_list args;
	size_t pos = bufpos ? *bufpos : 0;

	va_start(args, fmt);
	pos += vsnprintf(buf + pos, bufsize - pos, fmt, args);
	va_end(args);

	if (bufpos)
		*bufpos = pos;

	return pos >= bufsize ? FALSE : TRUE;
}

#define string_format(buf, fmt, args...) \
	string_nformat(buf, sizeof(buf), NULL, fmt, args)

#define string_format_from(buf, from, fmt, args...) \
	string_nformat(buf, sizeof(buf), from, fmt, args)

static int
string_enum_compare(const char *str1, const char *str2, int len)
{
	size_t i;

#define string_enum_sep(x) ((x) == '-' || (x) == '_' || (x) == '.')

	/* Diff-Header == DIFF_HEADER */
	for (i = 0; i < len; i++) {
		if (toupper(str1[i]) == toupper(str2[i]))
			continue;

		if (string_enum_sep(str1[i]) &&
		    string_enum_sep(str2[i]))
			continue;

		return str1[i] - str2[i];
	}

	return 0;
}

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
sq_quote(char buf[SIZEOF_STR], size_t bufsize, const char *src)
{
	char c;

#define BUFPUT(x) do { if (bufsize < SIZEOF_STR) buf[bufsize++] = (x); } while (0)

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

	if (bufsize < SIZEOF_STR)
		buf[bufsize] = 0;

	return bufsize;
}


/*
 * User requests
 */

#define REQ_INFO \
	/* XXX: Keep the view request first and in sync with views[]. */ \
	REQ_GROUP("View switching") \
	REQ_(VIEW_MAIN,		"Show main view"), \
	REQ_(VIEW_DIFF,		"Show diff view"), \
	REQ_(VIEW_LOG,		"Show log view"), \
	REQ_(VIEW_TREE,		"Show tree view"), \
	REQ_(VIEW_BLOB,		"Show blob view"), \
	REQ_(VIEW_HELP,		"Show help page"), \
	REQ_(VIEW_PAGER,	"Show pager view"), \
	REQ_(VIEW_STATUS,	"Show status view"), \
	REQ_(VIEW_STAGE,	"Show stage view"), \
	\
	REQ_GROUP("View manipulation") \
	REQ_(ENTER,		"Enter current line and scroll"), \
	REQ_(NEXT,		"Move to next"), \
	REQ_(PREVIOUS,		"Move to previous"), \
	REQ_(VIEW_NEXT,		"Move focus to next view"), \
	REQ_(REFRESH,		"Reload and refresh"), \
	REQ_(VIEW_CLOSE,	"Close the current view"), \
	REQ_(QUIT,		"Close all views and quit"), \
	\
	REQ_GROUP("Cursor navigation") \
	REQ_(MOVE_UP,		"Move cursor one line up"), \
	REQ_(MOVE_DOWN,		"Move cursor one line down"), \
	REQ_(MOVE_PAGE_DOWN,	"Move cursor one page down"), \
	REQ_(MOVE_PAGE_UP,	"Move cursor one page up"), \
	REQ_(MOVE_FIRST_LINE,	"Move cursor to first line"), \
	REQ_(MOVE_LAST_LINE,	"Move cursor to last line"), \
	\
	REQ_GROUP("Scrolling") \
	REQ_(SCROLL_LINE_UP,	"Scroll one line up"), \
	REQ_(SCROLL_LINE_DOWN,	"Scroll one line down"), \
	REQ_(SCROLL_PAGE_UP,	"Scroll one page up"), \
	REQ_(SCROLL_PAGE_DOWN,	"Scroll one page down"), \
	\
	REQ_GROUP("Searching") \
	REQ_(SEARCH,		"Search the view"), \
	REQ_(SEARCH_BACK,	"Search backwards in the view"), \
	REQ_(FIND_NEXT,		"Find next search match"), \
	REQ_(FIND_PREV,		"Find previous search match"), \
	\
	REQ_GROUP("Misc") \
	REQ_(NONE,		"Do nothing"), \
	REQ_(PROMPT,		"Bring up the prompt"), \
	REQ_(SCREEN_REDRAW,	"Redraw the screen"), \
	REQ_(SCREEN_RESIZE,	"Resize the screen"), \
	REQ_(SHOW_VERSION,	"Show version information"), \
	REQ_(STOP_LOADING,	"Stop all loading views"), \
	REQ_(TOGGLE_LINENO,	"Toggle line numbers"), \
	REQ_(TOGGLE_REV_GRAPH,	"Toggle revision graph visualization"), \
	REQ_(STATUS_UPDATE,	"Update file status"), \
	REQ_(EDIT,		"Open in editor"), \
	REQ_(CHERRY_PICK,	"Cherry-pick commit to current branch")


/* User action requests. */
enum request {
#define REQ_GROUP(help)
#define REQ_(req, help) REQ_##req

	/* Offset all requests to avoid conflicts with ncurses getch values. */
	REQ_OFFSET = KEY_MAX + 1,
	REQ_INFO,
	REQ_UNKNOWN,

#undef	REQ_GROUP
#undef	REQ_
};

struct request_info {
	enum request request;
	char *name;
	int namelen;
	char *help;
};

static struct request_info req_info[] = {
#define REQ_GROUP(help)	{ 0, NULL, 0, (help) },
#define REQ_(req, help)	{ REQ_##req, (#req), STRING_SIZE(#req), (help) }
	REQ_INFO
#undef	REQ_GROUP
#undef	REQ_
};

static enum request
get_request(const char *name)
{
	int namelen = strlen(name);
	int i;

	for (i = 0; i < ARRAY_SIZE(req_info); i++)
		if (req_info[i].namelen == namelen &&
		    !string_enum_compare(req_info[i].name, name, namelen))
			return req_info[i].request;

	return REQ_UNKNOWN;
}


/*
 * Options
 */

static const char usage[] =
"tig " TIG_VERSION " (" __DATE__ ")\n"
"\n"
"Usage: tig [options]\n"
"   or: tig [options] [--] [git log options]\n"
"   or: tig [options] log  [git log options]\n"
"   or: tig [options] diff [git diff options]\n"
"   or: tig [options] show [git show options]\n"
"   or: tig [options] <    [git command output]\n"
"\n"
"Options:\n"
"  -l                          Start up in log view\n"
"  -d                          Start up in diff view\n"
"  -S                          Start up in status view\n"
"  -n[I], --line-number[=I]    Show line numbers with given interval\n"
"  -b[N], --tab-size[=N]       Set number of spaces for tab expansion\n"
"  --                          Mark end of tig options\n"
"  -v, --version               Show version and exit\n"
"  -h, --help                  Show help message and exit\n";

/* Option and state variables. */
static bool opt_line_number		= FALSE;
static bool opt_rev_graph		= FALSE;
static int opt_num_interval		= NUMBER_INTERVAL;
static int opt_tab_size			= TABSIZE;
static enum request opt_request		= REQ_VIEW_MAIN;
static char opt_cmd[SIZEOF_STR]		= "";
static char opt_path[SIZEOF_STR]	= "";
static FILE *opt_pipe			= NULL;
static char opt_encoding[20]		= "UTF-8";
static bool opt_utf8			= TRUE;
static char opt_codeset[20]		= "UTF-8";
static iconv_t opt_iconv		= ICONV_NONE;
static char opt_search[SIZEOF_STR]	= "";
static char opt_cdup[SIZEOF_STR]	= "";
static char opt_git_dir[SIZEOF_STR]	= "";
static char opt_editor[SIZEOF_STR]	= "";

enum option_type {
	OPT_NONE,
	OPT_INT,
};

static bool
check_option(char *opt, char short_name, char *name, enum option_type type, ...)
{
	va_list args;
	char *value = "";
	int *number;

	if (opt[0] != '-')
		return FALSE;

	if (opt[1] == '-') {
		int namelen = strlen(name);

		opt += 2;

		if (strncmp(opt, name, namelen))
			return FALSE;

		if (opt[namelen] == '=')
			value = opt + namelen + 1;

	} else {
		if (!short_name || opt[1] != short_name)
			return FALSE;
		value = opt + 2;
	}

	va_start(args, type);
	if (type == OPT_INT) {
		number = va_arg(args, int *);
		if (isdigit(*value))
			*number = atoi(value);
	}
	va_end(args);

	return TRUE;
}

/* Returns the index of log or diff command or -1 to exit. */
static bool
parse_options(int argc, char *argv[])
{
	int i;

	for (i = 1; i < argc; i++) {
		char *opt = argv[i];

		if (!strcmp(opt, "log") ||
		    !strcmp(opt, "diff") ||
		    !strcmp(opt, "show")) {
			opt_request = opt[0] == 'l'
				    ? REQ_VIEW_LOG : REQ_VIEW_DIFF;
			break;
		}

		if (opt[0] && opt[0] != '-')
			break;

		if (!strcmp(opt, "-l")) {
			opt_request = REQ_VIEW_LOG;
			continue;
		}

		if (!strcmp(opt, "-d")) {
			opt_request = REQ_VIEW_DIFF;
			continue;
		}

		if (!strcmp(opt, "-S")) {
			opt_request = REQ_VIEW_STATUS;
			continue;
		}

		if (check_option(opt, 'n', "line-number", OPT_INT, &opt_num_interval)) {
			opt_line_number = TRUE;
			continue;
		}

		if (check_option(opt, 'b', "tab-size", OPT_INT, &opt_tab_size)) {
			opt_tab_size = MIN(opt_tab_size, TABSIZE);
			continue;
		}

		if (check_option(opt, 'v', "version", OPT_NONE)) {
			printf("tig version %s\n", TIG_VERSION);
			return FALSE;
		}

		if (check_option(opt, 'h', "help", OPT_NONE)) {
			printf(usage);
			return FALSE;
		}

		if (!strcmp(opt, "--")) {
			i++;
			break;
		}

		die("unknown option '%s'\n\n%s", opt, usage);
	}

	if (!isatty(STDIN_FILENO)) {
		opt_request = REQ_VIEW_PAGER;
		opt_pipe = stdin;

	} else if (i < argc) {
		size_t buf_size;

		if (opt_request == REQ_VIEW_MAIN)
			/* XXX: This is vulnerable to the user overriding
			 * options required for the main view parser. */
			string_copy(opt_cmd, "git log --pretty=raw");
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

	if (*opt_encoding && strcasecmp(opt_encoding, "UTF-8"))
		opt_utf8 = FALSE;

	return TRUE;
}


/*
 * Line-oriented content detection.
 */

#define LINE_INFO \
LINE(DIFF_HEADER,  "diff --git ",	COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(DIFF_CHUNK,   "@@",		COLOR_MAGENTA,	COLOR_DEFAULT,	0), \
LINE(DIFF_ADD,	   "+",			COLOR_GREEN,	COLOR_DEFAULT,	0), \
LINE(DIFF_DEL,	   "-",			COLOR_RED,	COLOR_DEFAULT,	0), \
LINE(DIFF_INDEX,	"index ",	  COLOR_BLUE,	COLOR_DEFAULT,	0), \
LINE(DIFF_OLDMODE,	"old file mode ", COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(DIFF_NEWMODE,	"new file mode ", COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(DIFF_COPY_FROM,	"copy from",	  COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(DIFF_COPY_TO,	"copy to",	  COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(DIFF_RENAME_FROM,	"rename from",	  COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(DIFF_RENAME_TO,	"rename to",	  COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(DIFF_SIMILARITY,   "similarity ",	  COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(DIFF_DISSIMILARITY,"dissimilarity ", COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(DIFF_TREE,		"diff-tree ",	  COLOR_BLUE,	COLOR_DEFAULT,	0), \
LINE(PP_AUTHOR,	   "Author: ",		COLOR_CYAN,	COLOR_DEFAULT,	0), \
LINE(PP_COMMIT,	   "Commit: ",		COLOR_MAGENTA,	COLOR_DEFAULT,	0), \
LINE(PP_MERGE,	   "Merge: ",		COLOR_BLUE,	COLOR_DEFAULT,	0), \
LINE(PP_DATE,	   "Date:   ",		COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(PP_ADATE,	   "AuthorDate: ",	COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(PP_CDATE,	   "CommitDate: ",	COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(PP_REFS,	   "Refs: ",		COLOR_RED,	COLOR_DEFAULT,	0), \
LINE(COMMIT,	   "commit ",		COLOR_GREEN,	COLOR_DEFAULT,	0), \
LINE(PARENT,	   "parent ",		COLOR_BLUE,	COLOR_DEFAULT,	0), \
LINE(TREE,	   "tree ",		COLOR_BLUE,	COLOR_DEFAULT,	0), \
LINE(AUTHOR,	   "author ",		COLOR_CYAN,	COLOR_DEFAULT,	0), \
LINE(COMMITTER,	   "committer ",	COLOR_MAGENTA,	COLOR_DEFAULT,	0), \
LINE(SIGNOFF,	   "    Signed-off-by", COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(ACKED,	   "    Acked-by",	COLOR_YELLOW,	COLOR_DEFAULT,	0), \
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
LINE(MAIN_REMOTE,  "",			COLOR_YELLOW,	COLOR_DEFAULT,	A_BOLD), \
LINE(MAIN_REF,     "",			COLOR_CYAN,	COLOR_DEFAULT,	A_BOLD), \
LINE(TREE_DIR,     "",			COLOR_DEFAULT,	COLOR_DEFAULT,	A_NORMAL), \
LINE(TREE_FILE,    "",			COLOR_DEFAULT,	COLOR_DEFAULT,	A_NORMAL), \
LINE(STAT_SECTION, "",			COLOR_CYAN,	COLOR_DEFAULT,	0), \
LINE(STAT_NONE,    "",			COLOR_DEFAULT,	COLOR_DEFAULT,	0), \
LINE(STAT_STAGED,  "",			COLOR_MAGENTA,	COLOR_DEFAULT,	0), \
LINE(STAT_UNSTAGED,"",			COLOR_MAGENTA,	COLOR_DEFAULT,	0), \
LINE(STAT_UNTRACKED,"",			COLOR_MAGENTA,	COLOR_DEFAULT,	0)

enum line_type {
#define LINE(type, line, fg, bg, attr) \
	LINE_##type
	LINE_INFO
#undef	LINE
};

struct line_info {
	const char *name;	/* Option name. */
	int namelen;		/* Size of option name. */
	const char *line;	/* The start of line to match. */
	int linelen;		/* Size of string to match. */
	int fg, bg, attr;	/* Color and text attributes for the lines. */
};

static struct line_info line_info[] = {
#define LINE(type, line, fg, bg, attr) \
	{ #type, STRING_SIZE(#type), (line), STRING_SIZE(line), (fg), (bg), (attr) }
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

static struct line_info *
get_line_info(char *name, int namelen)
{
	enum line_type type;

	for (type = 0; type < ARRAY_SIZE(line_info); type++)
		if (namelen == line_info[type].namelen &&
		    !string_enum_compare(line_info[type].name, name, namelen))
			return &line_info[type];

	return NULL;
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

struct line {
	enum line_type type;

	/* State flags */
	unsigned int selected:1;

	void *data;		/* User data */
};


/*
 * Keys
 */

struct keybinding {
	int alias;
	enum request request;
	struct keybinding *next;
};

static struct keybinding default_keybindings[] = {
	/* View switching */
	{ 'm',		REQ_VIEW_MAIN },
	{ 'd',		REQ_VIEW_DIFF },
	{ 'l',		REQ_VIEW_LOG },
	{ 't',		REQ_VIEW_TREE },
	{ 'f',		REQ_VIEW_BLOB },
	{ 'p',		REQ_VIEW_PAGER },
	{ 'h',		REQ_VIEW_HELP },
	{ 'S',		REQ_VIEW_STATUS },
	{ 'c',		REQ_VIEW_STAGE },

	/* View manipulation */
	{ 'q',		REQ_VIEW_CLOSE },
	{ KEY_TAB,	REQ_VIEW_NEXT },
	{ KEY_RETURN,	REQ_ENTER },
	{ KEY_UP,	REQ_PREVIOUS },
	{ KEY_DOWN,	REQ_NEXT },
	{ 'R',		REQ_REFRESH },

	/* Cursor navigation */
	{ 'k',		REQ_MOVE_UP },
	{ 'j',		REQ_MOVE_DOWN },
	{ KEY_HOME,	REQ_MOVE_FIRST_LINE },
	{ KEY_END,	REQ_MOVE_LAST_LINE },
	{ KEY_NPAGE,	REQ_MOVE_PAGE_DOWN },
	{ ' ',		REQ_MOVE_PAGE_DOWN },
	{ KEY_PPAGE,	REQ_MOVE_PAGE_UP },
	{ 'b',		REQ_MOVE_PAGE_UP },
	{ '-',		REQ_MOVE_PAGE_UP },

	/* Scrolling */
	{ KEY_IC,	REQ_SCROLL_LINE_UP },
	{ KEY_DC,	REQ_SCROLL_LINE_DOWN },
	{ 'w',		REQ_SCROLL_PAGE_UP },
	{ 's',		REQ_SCROLL_PAGE_DOWN },

	/* Searching */
	{ '/',		REQ_SEARCH },
	{ '?',		REQ_SEARCH_BACK },
	{ 'n',		REQ_FIND_NEXT },
	{ 'N',		REQ_FIND_PREV },

	/* Misc */
	{ 'Q',		REQ_QUIT },
	{ 'z',		REQ_STOP_LOADING },
	{ 'v',		REQ_SHOW_VERSION },
	{ 'r',		REQ_SCREEN_REDRAW },
	{ '.',		REQ_TOGGLE_LINENO },
	{ 'g',		REQ_TOGGLE_REV_GRAPH },
	{ ':',		REQ_PROMPT },
	{ 'u',		REQ_STATUS_UPDATE },
	{ 'e',		REQ_EDIT },
	{ 'C',		REQ_CHERRY_PICK },

	/* Using the ncurses SIGWINCH handler. */
	{ KEY_RESIZE,	REQ_SCREEN_RESIZE },
};

#define KEYMAP_INFO \
	KEYMAP_(GENERIC), \
	KEYMAP_(MAIN), \
	KEYMAP_(DIFF), \
	KEYMAP_(LOG), \
	KEYMAP_(TREE), \
	KEYMAP_(BLOB), \
	KEYMAP_(PAGER), \
	KEYMAP_(HELP), \
	KEYMAP_(STATUS), \
	KEYMAP_(STAGE)

enum keymap {
#define KEYMAP_(name) KEYMAP_##name
	KEYMAP_INFO
#undef	KEYMAP_
};

static struct int_map keymap_table[] = {
#define KEYMAP_(name) { #name, STRING_SIZE(#name), KEYMAP_##name }
	KEYMAP_INFO
#undef	KEYMAP_
};

#define set_keymap(map, name) \
	set_from_int_map(keymap_table, ARRAY_SIZE(keymap_table), map, name, strlen(name))

static struct keybinding *keybindings[ARRAY_SIZE(keymap_table)];

static void
add_keybinding(enum keymap keymap, enum request request, int key)
{
	struct keybinding *keybinding;

	keybinding = calloc(1, sizeof(*keybinding));
	if (!keybinding)
		die("Failed to allocate keybinding");

	keybinding->alias = key;
	keybinding->request = request;
	keybinding->next = keybindings[keymap];
	keybindings[keymap] = keybinding;
}

/* Looks for a key binding first in the given map, then in the generic map, and
 * lastly in the default keybindings. */
static enum request
get_keybinding(enum keymap keymap, int key)
{
	struct keybinding *kbd;
	int i;

	for (kbd = keybindings[keymap]; kbd; kbd = kbd->next)
		if (kbd->alias == key)
			return kbd->request;

	for (kbd = keybindings[KEYMAP_GENERIC]; kbd; kbd = kbd->next)
		if (kbd->alias == key)
			return kbd->request;

	for (i = 0; i < ARRAY_SIZE(default_keybindings); i++)
		if (default_keybindings[i].alias == key)
			return default_keybindings[i].request;

	return (enum request) key;
}


struct key {
	char *name;
	int value;
};

static struct key key_table[] = {
	{ "Enter",	KEY_RETURN },
	{ "Space",	' ' },
	{ "Backspace",	KEY_BACKSPACE },
	{ "Tab",	KEY_TAB },
	{ "Escape",	KEY_ESC },
	{ "Left",	KEY_LEFT },
	{ "Right",	KEY_RIGHT },
	{ "Up",		KEY_UP },
	{ "Down",	KEY_DOWN },
	{ "Insert",	KEY_IC },
	{ "Delete",	KEY_DC },
	{ "Hash",	'#' },
	{ "Home",	KEY_HOME },
	{ "End",	KEY_END },
	{ "PageUp",	KEY_PPAGE },
	{ "PageDown",	KEY_NPAGE },
	{ "F1",		KEY_F(1) },
	{ "F2",		KEY_F(2) },
	{ "F3",		KEY_F(3) },
	{ "F4",		KEY_F(4) },
	{ "F5",		KEY_F(5) },
	{ "F6",		KEY_F(6) },
	{ "F7",		KEY_F(7) },
	{ "F8",		KEY_F(8) },
	{ "F9",		KEY_F(9) },
	{ "F10",	KEY_F(10) },
	{ "F11",	KEY_F(11) },
	{ "F12",	KEY_F(12) },
};

static int
get_key_value(const char *name)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(key_table); i++)
		if (!strcasecmp(key_table[i].name, name))
			return key_table[i].value;

	if (strlen(name) == 1 && isprint(*name))
		return (int) *name;

	return ERR;
}

static char *
get_key(enum request request)
{
	static char buf[BUFSIZ];
	static char key_char[] = "'X'";
	size_t pos = 0;
	char *sep = "";
	int i;

	buf[pos] = 0;

	for (i = 0; i < ARRAY_SIZE(default_keybindings); i++) {
		struct keybinding *keybinding = &default_keybindings[i];
		char *seq = NULL;
		int key;

		if (keybinding->request != request)
			continue;

		for (key = 0; key < ARRAY_SIZE(key_table); key++)
			if (key_table[key].value == keybinding->alias)
				seq = key_table[key].name;

		if (seq == NULL &&
		    keybinding->alias < 127 &&
		    isprint(keybinding->alias)) {
			key_char[1] = (char) keybinding->alias;
			seq = key_char;
		}

		if (!seq)
			seq = "'?'";

		if (!string_format_from(buf, &pos, "%s%s", sep, seq))
			return "Too many keybindings!";
		sep = ", ";
	}

	return buf;
}


/*
 * User config file handling.
 */

static struct int_map color_map[] = {
#define COLOR_MAP(name) { #name, STRING_SIZE(#name), COLOR_##name }
	COLOR_MAP(DEFAULT),
	COLOR_MAP(BLACK),
	COLOR_MAP(BLUE),
	COLOR_MAP(CYAN),
	COLOR_MAP(GREEN),
	COLOR_MAP(MAGENTA),
	COLOR_MAP(RED),
	COLOR_MAP(WHITE),
	COLOR_MAP(YELLOW),
};

#define set_color(color, name) \
	set_from_int_map(color_map, ARRAY_SIZE(color_map), color, name, strlen(name))

static struct int_map attr_map[] = {
#define ATTR_MAP(name) { #name, STRING_SIZE(#name), A_##name }
	ATTR_MAP(NORMAL),
	ATTR_MAP(BLINK),
	ATTR_MAP(BOLD),
	ATTR_MAP(DIM),
	ATTR_MAP(REVERSE),
	ATTR_MAP(STANDOUT),
	ATTR_MAP(UNDERLINE),
};

#define set_attribute(attr, name) \
	set_from_int_map(attr_map, ARRAY_SIZE(attr_map), attr, name, strlen(name))

static int   config_lineno;
static bool  config_errors;
static char *config_msg;

/* Wants: object fgcolor bgcolor [attr] */
static int
option_color_command(int argc, char *argv[])
{
	struct line_info *info;

	if (argc != 3 && argc != 4) {
		config_msg = "Wrong number of arguments given to color command";
		return ERR;
	}

	info = get_line_info(argv[0], strlen(argv[0]));
	if (!info) {
		config_msg = "Unknown color name";
		return ERR;
	}

	if (set_color(&info->fg, argv[1]) == ERR ||
	    set_color(&info->bg, argv[2]) == ERR) {
		config_msg = "Unknown color";
		return ERR;
	}

	if (argc == 4 && set_attribute(&info->attr, argv[3]) == ERR) {
		config_msg = "Unknown attribute";
		return ERR;
	}

	return OK;
}

/* Wants: name = value */
static int
option_set_command(int argc, char *argv[])
{
	if (argc != 3) {
		config_msg = "Wrong number of arguments given to set command";
		return ERR;
	}

	if (strcmp(argv[1], "=")) {
		config_msg = "No value assigned";
		return ERR;
	}

	if (!strcmp(argv[0], "show-rev-graph")) {
		opt_rev_graph = (!strcmp(argv[2], "1") ||
				 !strcmp(argv[2], "true") ||
				 !strcmp(argv[2], "yes"));
		return OK;
	}

	if (!strcmp(argv[0], "line-number-interval")) {
		opt_num_interval = atoi(argv[2]);
		return OK;
	}

	if (!strcmp(argv[0], "tab-size")) {
		opt_tab_size = atoi(argv[2]);
		return OK;
	}

	if (!strcmp(argv[0], "commit-encoding")) {
		char *arg = argv[2];
		int delimiter = *arg;
		int i;

		switch (delimiter) {
		case '"':
		case '\'':
			for (arg++, i = 0; arg[i]; i++)
				if (arg[i] == delimiter) {
					arg[i] = 0;
					break;
				}
		default:
			string_ncopy(opt_encoding, arg, strlen(arg));
			return OK;
		}
	}

	config_msg = "Unknown variable name";
	return ERR;
}

/* Wants: mode request key */
static int
option_bind_command(int argc, char *argv[])
{
	enum request request;
	int keymap;
	int key;

	if (argc != 3) {
		config_msg = "Wrong number of arguments given to bind command";
		return ERR;
	}

	if (set_keymap(&keymap, argv[0]) == ERR) {
		config_msg = "Unknown key map";
		return ERR;
	}

	key = get_key_value(argv[1]);
	if (key == ERR) {
		config_msg = "Unknown key";
		return ERR;
	}

	request = get_request(argv[2]);
	if (request == REQ_UNKNOWN) {
		config_msg = "Unknown request name";
		return ERR;
	}

	add_keybinding(keymap, request, key);

	return OK;
}

static int
set_option(char *opt, char *value)
{
	char *argv[16];
	int valuelen;
	int argc = 0;

	/* Tokenize */
	while (argc < ARRAY_SIZE(argv) && (valuelen = strcspn(value, " \t"))) {
		argv[argc++] = value;

		value += valuelen;
		if (!*value)
			break;

		*value++ = 0;
		while (isspace(*value))
			value++;
	}

	if (!strcmp(opt, "color"))
		return option_color_command(argc, argv);

	if (!strcmp(opt, "set"))
		return option_set_command(argc, argv);

	if (!strcmp(opt, "bind"))
		return option_bind_command(argc, argv);

	config_msg = "Unknown option command";
	return ERR;
}

static int
read_option(char *opt, size_t optlen, char *value, size_t valuelen)
{
	int status = OK;

	config_lineno++;
	config_msg = "Internal error";

	/* Check for comment markers, since read_properties() will
	 * only ensure opt and value are split at first " \t". */
	optlen = strcspn(opt, "#");
	if (optlen == 0)
		return OK;

	if (opt[optlen] != 0) {
		config_msg = "No option value";
		status = ERR;

	}  else {
		/* Look for comment endings in the value. */
		size_t len = strcspn(value, "#");

		if (len < valuelen) {
			valuelen = len;
			value[valuelen] = 0;
		}

		status = set_option(opt, value);
	}

	if (status == ERR) {
		fprintf(stderr, "Error on line %d, near '%.*s': %s\n",
			config_lineno, (int) optlen, opt, config_msg);
		config_errors = TRUE;
	}

	/* Always keep going if errors are encountered. */
	return OK;
}

static int
load_options(void)
{
	char *home = getenv("HOME");
	char buf[SIZEOF_STR];
	FILE *file;

	config_lineno = 0;
	config_errors = FALSE;

	if (!home || !string_format(buf, "%s/.tigrc", home))
		return ERR;

	/* It's ok that the file doesn't exist. */
	file = fopen(buf, "r");
	if (!file)
		return OK;

	if (read_properties(file, " \t", read_option) == ERR ||
	    config_errors == TRUE)
		fprintf(stderr, "Errors while loading %s.\n", buf);

	return OK;
}


/*
 * The viewer
 */

struct view;
struct view_ops;

/* The display array of active views and the index of the current view. */
static struct view *display[2];
static unsigned int current_view;

/* Reading from the prompt? */
static bool input_mode = FALSE;

#define foreach_displayed_view(view, i) \
	for (i = 0; i < ARRAY_SIZE(display) && (view = display[i]); i++)

#define displayed_views()	(display[1] != NULL ? 2 : 1)

/* Current head and commit ID */
static char ref_blob[SIZEOF_REF]	= "";
static char ref_commit[SIZEOF_REF]	= "HEAD";
static char ref_head[SIZEOF_REF]	= "HEAD";

struct view {
	const char *name;	/* View name */
	const char *cmd_fmt;	/* Default command line format */
	const char *cmd_env;	/* Command line set via environment */
	const char *id;		/* Points to either of ref_{head,commit,blob} */

	struct view_ops *ops;	/* View operations */

	enum keymap keymap;	/* What keymap does this view have */

	char cmd[SIZEOF_STR];	/* Command buffer */
	char ref[SIZEOF_REF];	/* Hovered commit reference */
	char vid[SIZEOF_REF];	/* View ID. Set to id member when updating. */

	int height, width;	/* The width and height of the main window */
	WINDOW *win;		/* The main window */
	WINDOW *title;		/* The title window living below the main window */

	/* Navigation */
	unsigned long offset;	/* Offset of the window top */
	unsigned long lineno;	/* Current line number */

	/* Searching */
	char grep[SIZEOF_STR];	/* Search string */
	regex_t *regex;		/* Pre-compiled regex */

	/* If non-NULL, points to the view that opened this view. If this view
	 * is closed tig will switch back to the parent view. */
	struct view *parent;

	/* Buffering */
	unsigned long lines;	/* Total number of lines */
	struct line *line;	/* Line index */
	unsigned long line_size;/* Total number of allocated lines */
	unsigned int digits;	/* Number of digits in the lines member. */

	/* Loading */
	FILE *pipe;
	time_t start_time;
};

struct view_ops {
	/* What type of content being displayed. Used in the title bar. */
	const char *type;
	/* Open and reads in all view content. */
	bool (*open)(struct view *view);
	/* Read one line; updates view->line. */
	bool (*read)(struct view *view, char *data);
	/* Draw one line; @lineno must be < view->height. */
	bool (*draw)(struct view *view, struct line *line, unsigned int lineno, bool selected);
	/* Depending on view handle a special requests. */
	enum request (*request)(struct view *view, enum request request, struct line *line);
	/* Search for regex in a line. */
	bool (*grep)(struct view *view, struct line *line);
	/* Select line */
	void (*select)(struct view *view, struct line *line);
};

static struct view_ops pager_ops;
static struct view_ops main_ops;
static struct view_ops tree_ops;
static struct view_ops blob_ops;
static struct view_ops help_ops;
static struct view_ops status_ops;
static struct view_ops stage_ops;

#define VIEW_STR(name, cmd, env, ref, ops, map) \
	{ name, cmd, #env, ref, ops, map}

#define VIEW_(id, name, ops, ref) \
	VIEW_STR(name, TIG_##id##_CMD,  TIG_##id##_CMD, ref, ops, KEYMAP_##id)


static struct view views[] = {
	VIEW_(MAIN,   "main",   &main_ops,   ref_head),
	VIEW_(DIFF,   "diff",   &pager_ops,  ref_commit),
	VIEW_(LOG,    "log",    &pager_ops,  ref_head),
	VIEW_(TREE,   "tree",   &tree_ops,   ref_commit),
	VIEW_(BLOB,   "blob",   &blob_ops,   ref_blob),
	VIEW_(HELP,   "help",   &help_ops,   ""),
	VIEW_(PAGER,  "pager",  &pager_ops,  "stdin"),
	VIEW_(STATUS, "status", &status_ops, ""),
	VIEW_(STAGE,  "stage",	&stage_ops,  ""),
};

#define VIEW(req) (&views[(req) - REQ_OFFSET - 1])

#define foreach_view(view, i) \
	for (i = 0; i < ARRAY_SIZE(views) && (view = &views[i]); i++)

#define view_is_displayed(view) \
	(view == display[0] || view == display[1])

static bool
draw_view_line(struct view *view, unsigned int lineno)
{
	struct line *line;
	bool selected = (view->offset + lineno == view->lineno);
	bool draw_ok;

	assert(view_is_displayed(view));

	if (view->offset + lineno >= view->lines)
		return FALSE;

	line = &view->line[view->offset + lineno];

	if (selected) {
		line->selected = TRUE;
		view->ops->select(view, line);
	} else if (line->selected) {
		line->selected = FALSE;
		wmove(view->win, lineno, 0);
		wclrtoeol(view->win);
	}

	scrollok(view->win, FALSE);
	draw_ok = view->ops->draw(view, line, lineno, selected);
	scrollok(view->win, TRUE);

	return draw_ok;
}

static void
redraw_view_from(struct view *view, int lineno)
{
	assert(0 <= lineno && lineno < view->height);

	for (; lineno < view->height; lineno++) {
		if (!draw_view_line(view, lineno))
			break;
	}

	redrawwin(view->win);
	if (input_mode)
		wnoutrefresh(view->win);
	else
		wrefresh(view->win);
}

static void
redraw_view(struct view *view)
{
	wclear(view->win);
	redraw_view_from(view, 0);
}


static void
update_view_title(struct view *view)
{
	char buf[SIZEOF_STR];
	char state[SIZEOF_STR];
	size_t bufpos = 0, statelen = 0;

	assert(view_is_displayed(view));

	if (view != VIEW(REQ_VIEW_STATUS) && (view->lines || view->pipe)) {
		unsigned int view_lines = view->offset + view->height;
		unsigned int lines = view->lines
				   ? MIN(view_lines, view->lines) * 100 / view->lines
				   : 0;

		string_format_from(state, &statelen, "- %s %d of %d (%d%%)",
				   view->ops->type,
				   view->lineno + 1,
				   view->lines,
				   lines);

		if (view->pipe) {
			time_t secs = time(NULL) - view->start_time;

			/* Three git seconds are a long time ... */
			if (secs > 2)
				string_format_from(state, &statelen, " %lds", secs);
		}
	}

	string_format_from(buf, &bufpos, "[%s]", view->name);
	if (*view->ref && bufpos < view->width) {
		size_t refsize = strlen(view->ref);
		size_t minsize = bufpos + 1 + /* abbrev= */ 7 + 1 + statelen;

		if (minsize < view->width)
			refsize = view->width - minsize + 7;
		string_format_from(buf, &bufpos, " %.*s", (int) refsize, view->ref);
	}

	if (statelen && bufpos < view->width) {
		string_format_from(buf, &bufpos, " %s", state);
	}

	if (view == display[current_view])
		wbkgdset(view->title, get_line_attr(LINE_TITLE_FOCUS));
	else
		wbkgdset(view->title, get_line_attr(LINE_TITLE_BLUR));

	mvwaddnstr(view->title, 0, 0, buf, bufpos);
	wclrtoeol(view->title);
	wmove(view->title, 0, view->width - 1);

	if (input_mode)
		wnoutrefresh(view->title);
	else
		wrefresh(view->title);
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

	foreach_displayed_view (view, i) {
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
		}

		offset += view->height + 1;
	}
}

static void
redraw_display(void)
{
	struct view *view;
	int i;

	foreach_displayed_view (view, i) {
		redraw_view(view);
		update_view_title(view);
	}
}

static void
update_display_cursor(struct view *view)
{
	/* Move the cursor to the right-most column of the cursor line.
	 *
	 * XXX: This could turn out to be a bit expensive, but it ensures that
	 * the cursor does not jump around. */
	if (view->lines) {
		wmove(view->win, view->lineno - view->offset, view->width - 1);
		wrefresh(view->win);
	}
}

/*
 * Navigation
 */

/* Scrolling backend */
static void
do_scroll_view(struct view *view, int lines)
{
	bool redraw_current_line = FALSE;

	/* The rendering expects the new offset. */
	view->offset += lines;

	assert(0 <= view->offset && view->offset < view->lines);
	assert(lines);

	/* Move current line into the view. */
	if (view->lineno < view->offset) {
		view->lineno = view->offset;
		redraw_current_line = TRUE;
	} else if (view->lineno >= view->offset + view->height) {
		view->lineno = view->offset + view->height - 1;
		redraw_current_line = TRUE;
	}

	assert(view->offset <= view->lineno && view->lineno < view->lines);

	/* Redraw the whole screen if scrolling is pointless. */
	if (view->height < ABS(lines)) {
		redraw_view(view);

	} else {
		int line = lines > 0 ? view->height - lines : 0;
		int end = line + ABS(lines);

		wscrl(view->win, lines);

		for (; line < end; line++) {
			if (!draw_view_line(view, line))
				break;
		}

		if (redraw_current_line)
			draw_view_line(view, view->lineno - view->offset);
	}

	redrawwin(view->win);
	wrefresh(view->win);
	report("");
}

/* Scroll frontend */
static void
scroll_view(struct view *view, enum request request)
{
	int lines = 1;

	assert(view_is_displayed(view));

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
	int scroll_steps = 0;
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
		report("Cannot move beyond the first line");
		return;

	} else if (steps >= 0 && view->lineno + 1 >= view->lines) {
		report("Cannot move beyond the last line");
		return;
	}

	/* Move the current line */
	view->lineno += steps;
	assert(0 <= view->lineno && view->lineno < view->lines);

	/* Check whether the view needs to be scrolled */
	if (view->lineno < view->offset ||
	    view->lineno >= view->offset + view->height) {
		scroll_steps = steps;
		if (steps < 0 && -steps > view->offset) {
			scroll_steps = -view->offset;

		} else if (steps > 0) {
			if (view->lineno == view->lines - 1 &&
			    view->lines > view->height) {
				scroll_steps = view->lines - view->offset - 1;
				if (scroll_steps >= view->height)
					scroll_steps -= view->height - 1;
			}
		}
	}

	if (!view_is_displayed(view)) {
		view->offset += scroll_steps;
		assert(0 <= view->offset && view->offset < view->lines);
		view->ops->select(view, &view->line[view->lineno]);
		return;
	}

	/* Repaint the old "current" line if we be scrolling */
	if (ABS(steps) < view->height)
		draw_view_line(view, view->lineno - steps - view->offset);

	if (scroll_steps) {
		do_scroll_view(view, scroll_steps);
		return;
	}

	/* Draw the current line */
	draw_view_line(view, view->lineno - view->offset);

	redrawwin(view->win);
	wrefresh(view->win);
	report("");
}


/*
 * Searching
 */

static void search_view(struct view *view, enum request request);

static bool
find_next_line(struct view *view, unsigned long lineno, struct line *line)
{
	assert(view_is_displayed(view));

	if (!view->ops->grep(view, line))
		return FALSE;

	if (lineno - view->offset >= view->height) {
		view->offset = lineno;
		view->lineno = lineno;
		redraw_view(view);

	} else {
		unsigned long old_lineno = view->lineno - view->offset;

		view->lineno = lineno;
		draw_view_line(view, old_lineno);

		draw_view_line(view, view->lineno - view->offset);
		redrawwin(view->win);
		wrefresh(view->win);
	}

	report("Line %ld matches '%s'", lineno + 1, view->grep);
	return TRUE;
}

static void
find_next(struct view *view, enum request request)
{
	unsigned long lineno = view->lineno;
	int direction;

	if (!*view->grep) {
		if (!*opt_search)
			report("No previous search");
		else
			search_view(view, request);
		return;
	}

	switch (request) {
	case REQ_SEARCH:
	case REQ_FIND_NEXT:
		direction = 1;
		break;

	case REQ_SEARCH_BACK:
	case REQ_FIND_PREV:
		direction = -1;
		break;

	default:
		return;
	}

	if (request == REQ_FIND_NEXT || request == REQ_FIND_PREV)
		lineno += direction;

	/* Note, lineno is unsigned long so will wrap around in which case it
	 * will become bigger than view->lines. */
	for (; lineno < view->lines; lineno += direction) {
		struct line *line = &view->line[lineno];

		if (find_next_line(view, lineno, line))
			return;
	}

	report("No match found for '%s'", view->grep);
}

static void
search_view(struct view *view, enum request request)
{
	int regex_err;

	if (view->regex) {
		regfree(view->regex);
		*view->grep = 0;
	} else {
		view->regex = calloc(1, sizeof(*view->regex));
		if (!view->regex)
			return;
	}

	regex_err = regcomp(view->regex, opt_search, REG_EXTENDED);
	if (regex_err != 0) {
		char buf[SIZEOF_STR] = "unknown error";

		regerror(regex_err, view->regex, buf, sizeof(buf));
		report("Search failed: %s", buf);
		return;
	}

	string_copy(view->grep, opt_search);

	find_next(view, request);
}

/*
 * Incremental updating
 */

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
begin_update(struct view *view)
{
	if (view->pipe)
		end_update(view);

	if (opt_cmd[0]) {
		string_copy(view->cmd, opt_cmd);
		opt_cmd[0] = 0;
		/* When running random commands, initially show the
		 * command in the title. However, it maybe later be
		 * overwritten if a commit line is selected. */
		if (view == VIEW(REQ_VIEW_PAGER))
			string_copy(view->ref, view->cmd);
		else
			view->ref[0] = 0;

	} else if (view == VIEW(REQ_VIEW_TREE)) {
		const char *format = view->cmd_env ? view->cmd_env : view->cmd_fmt;
		char path[SIZEOF_STR];

		if (strcmp(view->vid, view->id))
			opt_path[0] = path[0] = 0;
		else if (sq_quote(path, 0, opt_path) >= sizeof(path))
			return FALSE;

		if (!string_format(view->cmd, format, view->id, path))
			return FALSE;

	} else {
		const char *format = view->cmd_env ? view->cmd_env : view->cmd_fmt;
		const char *id = view->id;

		if (!string_format(view->cmd, format, id, id, id, id, id))
			return FALSE;

		/* Put the current ref_* value to the view title ref
		 * member. This is needed by the blob view. Most other
		 * views sets it automatically after loading because the
		 * first line is a commit line. */
		string_copy_rev(view->ref, view->id);
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
	string_copy_rev(view->vid, view->id);

	if (view->line) {
		int i;

		for (i = 0; i < view->lines; i++)
			if (view->line[i].data)
				free(view->line[i].data);

		free(view->line);
		view->line = NULL;
	}

	view->start_time = time(NULL);

	return TRUE;
}

static struct line *
realloc_lines(struct view *view, size_t line_size)
{
	struct line *tmp = realloc(view->line, sizeof(*view->line) * line_size);

	if (!tmp)
		return NULL;

	view->line = tmp;
	view->line_size = line_size;
	return view->line;
}

static bool
update_view(struct view *view)
{
	char in_buffer[BUFSIZ];
	char out_buffer[BUFSIZ * 2];
	char *line;
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

	/* FIXME: This is probably not perfect for backgrounded views. */
	if (!realloc_lines(view, view->lines + lines))
		goto alloc_error;

	while ((line = fgets(in_buffer, sizeof(in_buffer), view->pipe))) {
		size_t linelen = strlen(line);

		if (linelen)
			line[linelen - 1] = 0;

		if (opt_iconv != ICONV_NONE) {
			ICONV_CONST char *inbuf = line;
			size_t inlen = linelen;

			char *outbuf = out_buffer;
			size_t outlen = sizeof(out_buffer);

			size_t ret;

			ret = iconv(opt_iconv, &inbuf, &inlen, &outbuf, &outlen);
			if (ret != (size_t) -1) {
				line = out_buffer;
				linelen = strlen(out_buffer);
			}
		}

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

	if (!view_is_displayed(view))
		goto check_pipe;

	if (view == VIEW(REQ_VIEW_TREE)) {
		/* Clear the view and redraw everything since the tree sorting
		 * might have rearranged things. */
		redraw_view(view);

	} else if (redraw_from >= 0) {
		/* If this is an incremental update, redraw the previous line
		 * since for commits some members could have changed when
		 * loading the main view. */
		if (redraw_from > 0)
			redraw_from--;

		/* Since revision graph visualization requires knowledge
		 * about the parent commit, it causes a further one-off
		 * needed to be redrawn for incremental updates. */
		if (redraw_from > 0 && opt_rev_graph)
			redraw_from--;

		/* Incrementally draw avoids flickering. */
		redraw_view_from(view, redraw_from);
	}

	/* Update the title _after_ the redraw so that if the redraw picks up a
	 * commit reference in view->ref it'll be available here. */
	update_view_title(view);

check_pipe:
	if (ferror(view->pipe)) {
		report("Failed to read: %s", strerror(errno));
		goto end;

	} else if (feof(view->pipe)) {
		report("");
		goto end;
	}

	return TRUE;

alloc_error:
	report("Allocation failure");

end:
	view->ops->read(view, NULL);
	end_update(view);
	return FALSE;
}

static struct line *
add_line_data(struct view *view, void *data, enum line_type type)
{
	struct line *line = &view->line[view->lines++];

	memset(line, 0, sizeof(*line));
	line->type = type;
	line->data = data;

	return line;
}

static struct line *
add_line_text(struct view *view, char *data, enum line_type type)
{
	if (data)
		data = strdup(data);

	return data ? add_line_data(view, data, type) : NULL;
}


/*
 * View opening
 */

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
	int nviews = displayed_views();
	struct view *base_view = display[0];

	if (view == prev && nviews == 1 && !reload) {
		report("Already in %s view", view->name);
		return;
	}

	if (view->ops->open) {
		if (!view->ops->open(view)) {
			report("Failed to load %s view", view->name);
			return;
		}

	} else if ((reload || strcmp(view->vid, view->id)) &&
		   !begin_update(view)) {
		report("Failed to load %s view", view->name);
		return;
	}

	if (split) {
		display[1] = view;
		if (!backgrounded)
			current_view = 1;
	} else {
		/* Maximize the current view. */
		memset(display, 0, sizeof(display));
		current_view = 0;
		display[current_view] = view;
	}

	/* Resize the view when switching between split- and full-screen,
	 * or when switching between two different full-screen views. */
	if (nviews != displayed_views() ||
	    (nviews == 1 && base_view != display[0]))
		resize_display();

	if (split && prev->lineno - prev->offset >= prev->height) {
		/* Take the title line into account. */
		int lines = prev->lineno - prev->offset - prev->height + 1;

		/* Scroll the view that was split if the current line is
		 * outside the new limited view. */
		do_scroll_view(prev, lines);
	}

	if (prev && view != prev) {
		if (split && !backgrounded) {
			/* "Blur" the previous view. */
			update_view_title(prev);
		}

		view->parent = prev;
	}

	if (view->pipe && view->lines == 0) {
		/* Clear the old view and let the incremental updating refill
		 * the screen. */
		wclear(view->win);
		report("");
	} else {
		redraw_view(view);
		report("");
	}

	/* If the view is backgrounded the above calls to report()
	 * won't redraw the view title. */
	if (backgrounded)
		update_view_title(view);
}

static void
open_editor(bool from_root, char *file)
{
	char cmd[SIZEOF_STR];
	char file_sq[SIZEOF_STR];
	char *editor;
	char *prefix = from_root ? opt_cdup : "";

	editor = getenv("GIT_EDITOR");
	if (!editor && *opt_editor)
		editor = opt_editor;
	if (!editor)
		editor = getenv("VISUAL");
	if (!editor)
		editor = getenv("EDITOR");
	if (!editor)
		editor = "vi";

	if (sq_quote(file_sq, 0, file) < sizeof(file_sq) &&
	    string_format(cmd, "%s %s%s", editor, prefix, file_sq)) {
		def_prog_mode();           /* save current tty modes */
		endwin();                  /* restore original tty modes */
		system(cmd);
		reset_prog_mode();
		redraw_display();
	}
}

/*
 * User request switch noodle
 */

static int
view_driver(struct view *view, enum request request)
{
	int i;

	if (request == REQ_NONE) {
		doupdate();
		return TRUE;
	}

	if (view && view->lines) {
		request = view->ops->request(view, request, &view->line[view->lineno]);
		if (request == REQ_NONE)
			return TRUE;
	}

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

	case REQ_VIEW_BLOB:
		if (!ref_blob[0]) {
			report("No file chosen, press %s to open tree view",
			       get_key(REQ_VIEW_TREE));
			break;
		}
		open_view(view, request, OPEN_DEFAULT);
		break;

	case REQ_VIEW_PAGER:
		if (!opt_pipe && !VIEW(REQ_VIEW_PAGER)->lines) {
			report("No pager content, press %s to run command from prompt",
			       get_key(REQ_PROMPT));
			break;
		}
		open_view(view, request, OPEN_DEFAULT);
		break;

	case REQ_VIEW_STAGE:
		if (!VIEW(REQ_VIEW_STAGE)->lines) {
			report("No stage content, press %s to open the status view and choose file",
			       get_key(REQ_VIEW_STATUS));
			break;
		}
		open_view(view, request, OPEN_DEFAULT);
		break;

	case REQ_VIEW_MAIN:
	case REQ_VIEW_DIFF:
	case REQ_VIEW_LOG:
	case REQ_VIEW_TREE:
	case REQ_VIEW_HELP:
	case REQ_VIEW_STATUS:
		open_view(view, request, OPEN_DEFAULT);
		break;

	case REQ_NEXT:
	case REQ_PREVIOUS:
		request = request == REQ_NEXT ? REQ_MOVE_DOWN : REQ_MOVE_UP;

		if ((view == VIEW(REQ_VIEW_DIFF) &&
		     view->parent == VIEW(REQ_VIEW_MAIN)) ||
		   (view == VIEW(REQ_VIEW_STAGE) &&
		     view->parent == VIEW(REQ_VIEW_STATUS)) ||
		   (view == VIEW(REQ_VIEW_BLOB) &&
		     view->parent == VIEW(REQ_VIEW_TREE))) {
			int line;

			view = view->parent;
			line = view->lineno;
			move_view(view, request);
			if (view_is_displayed(view))
				update_view_title(view);
			if (line != view->lineno)
				view->ops->request(view, REQ_ENTER,
						   &view->line[view->lineno]);

		} else {
			move_view(view, request);
		}
		break;

	case REQ_VIEW_NEXT:
	{
		int nviews = displayed_views();
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
	case REQ_REFRESH:
		report("Refreshing is not yet supported for the %s view", view->name);
		break;

	case REQ_TOGGLE_LINENO:
		opt_line_number = !opt_line_number;
		redraw_display();
		break;

	case REQ_TOGGLE_REV_GRAPH:
		opt_rev_graph = !opt_rev_graph;
		redraw_display();
		break;

	case REQ_PROMPT:
		/* Always reload^Wrerun commands from the prompt. */
		open_view(view, opt_request, OPEN_RELOAD);
		break;

	case REQ_SEARCH:
	case REQ_SEARCH_BACK:
		search_view(view, request);
		break;

	case REQ_FIND_NEXT:
	case REQ_FIND_PREV:
		find_next(view, request);
		break;

	case REQ_STOP_LOADING:
		for (i = 0; i < ARRAY_SIZE(views); i++) {
			view = &views[i];
			if (view->pipe)
				report("Stopped loading the %s view", view->name),
			end_update(view);
		}
		break;

	case REQ_SHOW_VERSION:
		report("tig-%s (built %s)", TIG_VERSION, __DATE__);
		return TRUE;

	case REQ_SCREEN_RESIZE:
		resize_display();
		/* Fall-through */
	case REQ_SCREEN_REDRAW:
		redraw_display();
		break;

	case REQ_EDIT:
		report("Nothing to edit");
		break;

	case REQ_CHERRY_PICK:
		report("Nothing to cherry-pick");
		break;

	case REQ_ENTER:
		report("Nothing to enter");
		break;


	case REQ_VIEW_CLOSE:
		/* XXX: Mark closed views by letting view->parent point to the
		 * view itself. Parents to closed view should never be
		 * followed. */
		if (view->parent &&
		    view->parent->parent != view->parent) {
			memset(display, 0, sizeof(display));
			current_view = 0;
			display[current_view] = view->parent;
			view->parent = view;
			resize_display();
			redraw_display();
			break;
		}
		/* Fall-through */
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
 * Pager backend
 */

static bool
pager_draw(struct view *view, struct line *line, unsigned int lineno, bool selected)
{
	char *text = line->data;
	enum line_type type = line->type;
	int textlen = strlen(text);
	int attr;

	wmove(view->win, lineno, 0);

	if (selected) {
		type = LINE_CURSOR;
		wchgat(view->win, -1, 0, type, NULL);
	}

	attr = get_line_attr(type);
	wattrset(view->win, attr);

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

		while (text && col_offset + col < view->width) {
			int cols_max = view->width - col_offset - col;
			char *pos = text;
			int cols;

			if (*text == '\t') {
				text++;
				assert(sizeof(spaces) > TABSIZE);
				pos = spaces;
				cols = opt_tab_size - (col % opt_tab_size);

			} else {
				text = strchr(text, '\t');
				cols = line ? text - pos : strlen(pos);
			}

			waddnstr(view->win, pos, MIN(cols, cols_max));
			col += cols;
		}

	} else {
		int col = 0, pos = 0;

		for (; pos < textlen && col < view->width; pos++, col++)
			if (text[pos] == '\t')
				col += TABSIZE - (col % TABSIZE) - 1;

		waddnstr(view->win, text, pos);
	}

	return TRUE;
}

static bool
add_describe_ref(char *buf, size_t *bufpos, char *commit_id, const char *sep)
{
	char refbuf[SIZEOF_STR];
	char *ref = NULL;
	FILE *pipe;

	if (!string_format(refbuf, "git describe %s 2>/dev/null", commit_id))
		return TRUE;

	pipe = popen(refbuf, "r");
	if (!pipe)
		return TRUE;

	if ((ref = fgets(refbuf, sizeof(refbuf), pipe)))
		ref = chomp_string(ref);
	pclose(pipe);

	if (!ref || !*ref)
		return TRUE;

	/* This is the only fatal call, since it can "corrupt" the buffer. */
	if (!string_nformat(buf, SIZEOF_STR, bufpos, "%s%s", sep, ref))
		return FALSE;

	return TRUE;
}

static void
add_pager_refs(struct view *view, struct line *line)
{
	char buf[SIZEOF_STR];
	char *commit_id = line->data + STRING_SIZE("commit ");
	struct ref **refs;
	size_t bufpos = 0, refpos = 0;
	const char *sep = "Refs: ";
	bool is_tag = FALSE;

	assert(line->type == LINE_COMMIT);

	refs = get_refs(commit_id);
	if (!refs) {
		if (view == VIEW(REQ_VIEW_DIFF))
			goto try_add_describe_ref;
		return;
	}

	do {
		struct ref *ref = refs[refpos];
		char *fmt = ref->tag    ? "%s[%s]" :
		            ref->remote ? "%s<%s>" : "%s%s";

		if (!string_format_from(buf, &bufpos, fmt, sep, ref->name))
			return;
		sep = ", ";
		if (ref->tag)
			is_tag = TRUE;
	} while (refs[refpos++]->next);

	if (!is_tag && view == VIEW(REQ_VIEW_DIFF)) {
try_add_describe_ref:
		/* Add <tag>-g<commit_id> "fake" reference. */
		if (!add_describe_ref(buf, &bufpos, commit_id, sep))
			return;
	}

	if (bufpos == 0)
		return;

	if (!realloc_lines(view, view->line_size + 1))
		return;

	add_line_text(view, buf, LINE_PP_REFS);
}

static bool
pager_read(struct view *view, char *data)
{
	struct line *line;

	if (!data)
		return TRUE;

	line = add_line_text(view, data, get_line_type(data));
	if (!line)
		return FALSE;

	if (line->type == LINE_COMMIT &&
	    (view == VIEW(REQ_VIEW_DIFF) ||
	     view == VIEW(REQ_VIEW_LOG)))
		add_pager_refs(view, line);

	return TRUE;
}

static enum request
pager_request(struct view *view, enum request request, struct line *line)
{
	int split = 0;

	if (request != REQ_ENTER)
		return request;

	if (line->type == LINE_COMMIT &&
	   (view == VIEW(REQ_VIEW_LOG) ||
	    view == VIEW(REQ_VIEW_PAGER))) {
		open_view(view, REQ_VIEW_DIFF, OPEN_SPLIT);
		split = 1;
	}

	/* Always scroll the view even if it was split. That way
	 * you can use Enter to scroll through the log view and
	 * split open each commit diff. */
	scroll_view(view, REQ_SCROLL_LINE_DOWN);

	/* FIXME: A minor workaround. Scrolling the view will call report("")
	 * but if we are scrolling a non-current view this won't properly
	 * update the view title. */
	if (split)
		update_view_title(view);

	return REQ_NONE;
}

static bool
pager_grep(struct view *view, struct line *line)
{
	regmatch_t pmatch;
	char *text = line->data;

	if (!*text)
		return FALSE;

	if (regexec(view->regex, text, 1, &pmatch, 0) == REG_NOMATCH)
		return FALSE;

	return TRUE;
}

static void
pager_select(struct view *view, struct line *line)
{
	if (line->type == LINE_COMMIT) {
		char *text = line->data + STRING_SIZE("commit ");

		if (view != VIEW(REQ_VIEW_PAGER))
			string_copy_rev(view->ref, text);
		string_copy_rev(ref_commit, text);
	}
}

static struct view_ops pager_ops = {
	"line",
	NULL,
	pager_read,
	pager_draw,
	pager_request,
	pager_grep,
	pager_select,
};


/*
 * Help backend
 */

static bool
help_open(struct view *view)
{
	char buf[BUFSIZ];
	int lines = ARRAY_SIZE(req_info) + 2;
	int i;

	if (view->lines > 0)
		return TRUE;

	for (i = 0; i < ARRAY_SIZE(req_info); i++)
		if (!req_info[i].request)
			lines++;

	view->line = calloc(lines, sizeof(*view->line));
	if (!view->line)
		return FALSE;

	add_line_text(view, "Quick reference for tig keybindings:", LINE_DEFAULT);

	for (i = 0; i < ARRAY_SIZE(req_info); i++) {
		char *key;

		if (req_info[i].request == REQ_NONE)
			continue;

		if (!req_info[i].request) {
			add_line_text(view, "", LINE_DEFAULT);
			add_line_text(view, req_info[i].help, LINE_DEFAULT);
			continue;
		}

		key = get_key(req_info[i].request);
		if (!*key)
			key = "(no key defined)";

		if (!string_format(buf, "    %-25s %s", key, req_info[i].help))
			continue;

		add_line_text(view, buf, LINE_DEFAULT);
	}

	return TRUE;
}

static struct view_ops help_ops = {
	"line",
	help_open,
	NULL,
	pager_draw,
	pager_request,
	pager_grep,
	pager_select,
};


/*
 * Tree backend
 */

struct tree_stack_entry {
	struct tree_stack_entry *prev;	/* Entry below this in the stack */
	unsigned long lineno;		/* Line number to restore */
	char *name;			/* Position of name in opt_path */
};

/* The top of the path stack. */
static struct tree_stack_entry *tree_stack = NULL;
unsigned long tree_lineno = 0;

static void
pop_tree_stack_entry(void)
{
	struct tree_stack_entry *entry = tree_stack;

	tree_lineno = entry->lineno;
	entry->name[0] = 0;
	tree_stack = entry->prev;
	free(entry);
}

static void
push_tree_stack_entry(char *name, unsigned long lineno)
{
	struct tree_stack_entry *entry = calloc(1, sizeof(*entry));
	size_t pathlen = strlen(opt_path);

	if (!entry)
		return;

	entry->prev = tree_stack;
	entry->name = opt_path + pathlen;
	tree_stack = entry;

	if (!string_format_from(opt_path, &pathlen, "%s/", name)) {
		pop_tree_stack_entry();
		return;
	}

	/* Move the current line to the first tree entry. */
	tree_lineno = 1;
	entry->lineno = lineno;
}

/* Parse output from git-ls-tree(1):
 *
 * 100644 blob fb0e31ea6cc679b7379631188190e975f5789c26	Makefile
 * 100644 blob 5304ca4260aaddaee6498f9630e7d471b8591ea6	README
 * 100644 blob f931e1d229c3e185caad4449bf5b66ed72462657	tig.c
 * 100644 blob ed09fe897f3c7c9af90bcf80cae92558ea88ae38	web.conf
 */

#define SIZEOF_TREE_ATTR \
	STRING_SIZE("100644 blob ed09fe897f3c7c9af90bcf80cae92558ea88ae38\t")

#define TREE_UP_FORMAT "040000 tree %s\t.."

static int
tree_compare_entry(enum line_type type1, char *name1,
		   enum line_type type2, char *name2)
{
	if (type1 != type2) {
		if (type1 == LINE_TREE_DIR)
			return -1;
		return 1;
	}

	return strcmp(name1, name2);
}

static bool
tree_read(struct view *view, char *text)
{
	size_t textlen = text ? strlen(text) : 0;
	char buf[SIZEOF_STR];
	unsigned long pos;
	enum line_type type;
	bool first_read = view->lines == 0;

	if (textlen <= SIZEOF_TREE_ATTR)
		return FALSE;

	type = text[STRING_SIZE("100644 ")] == 't'
	     ? LINE_TREE_DIR : LINE_TREE_FILE;

	if (first_read) {
		/* Add path info line */
		if (!string_format(buf, "Directory path /%s", opt_path) ||
		    !realloc_lines(view, view->line_size + 1) ||
		    !add_line_text(view, buf, LINE_DEFAULT))
			return FALSE;

		/* Insert "link" to parent directory. */
		if (*opt_path) {
			if (!string_format(buf, TREE_UP_FORMAT, view->ref) ||
			    !realloc_lines(view, view->line_size + 1) ||
			    !add_line_text(view, buf, LINE_TREE_DIR))
				return FALSE;
		}
	}

	/* Strip the path part ... */
	if (*opt_path) {
		size_t pathlen = textlen - SIZEOF_TREE_ATTR;
		size_t striplen = strlen(opt_path);
		char *path = text + SIZEOF_TREE_ATTR;

		if (pathlen > striplen)
			memmove(path, path + striplen,
				pathlen - striplen + 1);
	}

	/* Skip "Directory ..." and ".." line. */
	for (pos = 1 + !!*opt_path; pos < view->lines; pos++) {
		struct line *line = &view->line[pos];
		char *path1 = ((char *) line->data) + SIZEOF_TREE_ATTR;
		char *path2 = text + SIZEOF_TREE_ATTR;
		int cmp = tree_compare_entry(line->type, path1, type, path2);

		if (cmp <= 0)
			continue;

		text = strdup(text);
		if (!text)
			return FALSE;

		if (view->lines > pos)
			memmove(&view->line[pos + 1], &view->line[pos],
				(view->lines - pos) * sizeof(*line));

		line = &view->line[pos];
		line->data = text;
		line->type = type;
		view->lines++;
		return TRUE;
	}

	if (!add_line_text(view, text, type))
		return FALSE;

	if (tree_lineno > view->lineno) {
		view->lineno = tree_lineno;
		tree_lineno = 0;
	}

	return TRUE;
}

static enum request
tree_request(struct view *view, enum request request, struct line *line)
{
	enum open_flags flags;

	if (request != REQ_ENTER)
		return request;

	/* Cleanup the stack if the tree view is at a different tree. */
	while (!*opt_path && tree_stack)
		pop_tree_stack_entry();

	switch (line->type) {
	case LINE_TREE_DIR:
		/* Depending on whether it is a subdir or parent (updir?) link
		 * mangle the path buffer. */
		if (line == &view->line[1] && *opt_path) {
			pop_tree_stack_entry();

		} else {
			char *data = line->data;
			char *basename = data + SIZEOF_TREE_ATTR;

			push_tree_stack_entry(basename, view->lineno);
		}

		/* Trees and subtrees share the same ID, so they are not not
		 * unique like blobs. */
		flags = OPEN_RELOAD;
		request = REQ_VIEW_TREE;
		break;

	case LINE_TREE_FILE:
		flags = display[0] == view ? OPEN_SPLIT : OPEN_DEFAULT;
		request = REQ_VIEW_BLOB;
		break;

	default:
		return TRUE;
	}

	open_view(view, request, flags);
	if (request == REQ_VIEW_TREE) {
		view->lineno = tree_lineno;
	}

	return REQ_NONE;
}

static void
tree_select(struct view *view, struct line *line)
{
	char *text = line->data + STRING_SIZE("100644 blob ");

	if (line->type == LINE_TREE_FILE) {
		string_copy_rev(ref_blob, text);

	} else if (line->type != LINE_TREE_DIR) {
		return;
	}

	string_copy_rev(view->ref, text);
}

static struct view_ops tree_ops = {
	"file",
	NULL,
	tree_read,
	pager_draw,
	tree_request,
	pager_grep,
	tree_select,
};

static bool
blob_read(struct view *view, char *line)
{
	return add_line_text(view, line, LINE_DEFAULT) != NULL;
}

static struct view_ops blob_ops = {
	"line",
	NULL,
	blob_read,
	pager_draw,
	pager_request,
	pager_grep,
	pager_select,
};


/*
 * Status backend
 */

struct status {
	char status;
	struct {
		mode_t mode;
		char rev[SIZEOF_REV];
	} old;
	struct {
		mode_t mode;
		char rev[SIZEOF_REV];
	} new;
	char name[SIZEOF_STR];
};

static struct status stage_status;
static enum line_type stage_line_type;

/* Get fields from the diff line:
 * :100644 100644 06a5d6ae9eca55be2e0e585a152e6b1336f2b20e 0000000000000000000000000000000000000000 M
 */
static inline bool
status_get_diff(struct status *file, char *buf, size_t bufsize)
{
	char *old_mode = buf +  1;
	char *new_mode = buf +  8;
	char *old_rev  = buf + 15;
	char *new_rev  = buf + 56;
	char *status   = buf + 97;

	if (bufsize != 99 ||
	    old_mode[-1] != ':' ||
	    new_mode[-1] != ' ' ||
	    old_rev[-1]  != ' ' ||
	    new_rev[-1]  != ' ' ||
	    status[-1]   != ' ')
		return FALSE;

	file->status = *status;

	string_copy_rev(file->old.rev, old_rev);
	string_copy_rev(file->new.rev, new_rev);

	file->old.mode = strtoul(old_mode, NULL, 8);
	file->new.mode = strtoul(new_mode, NULL, 8);

	file->name[0] = 0;

	return TRUE;
}

static bool
status_run(struct view *view, const char cmd[], bool diff, enum line_type type)
{
	struct status *file = NULL;
	char buf[SIZEOF_STR * 4];
	size_t bufsize = 0;
	FILE *pipe;

	pipe = popen(cmd, "r");
	if (!pipe)
		return FALSE;

	add_line_data(view, NULL, type);

	while (!feof(pipe) && !ferror(pipe)) {
		char *sep;
		size_t readsize;

		readsize = fread(buf + bufsize, 1, sizeof(buf) - bufsize, pipe);
		if (!readsize)
			break;
		bufsize += readsize;

		/* Process while we have NUL chars. */
		while ((sep = memchr(buf, 0, bufsize))) {
			size_t sepsize = sep - buf + 1;

			if (!file) {
				if (!realloc_lines(view, view->line_size + 1))
					goto error_out;

				file = calloc(1, sizeof(*file));
				if (!file)
					goto error_out;

				add_line_data(view, file, type);
			}

			/* Parse diff info part. */
			if (!diff) {
				file->status = '?';

			} else if (!file->status) {
				if (!status_get_diff(file, buf, sepsize))
					goto error_out;

				bufsize -= sepsize;
				memmove(buf, sep + 1, bufsize);

				sep = memchr(buf, 0, bufsize);
				if (!sep)
					break;
				sepsize = sep - buf + 1;
			}

			/* git-ls-files just delivers a NUL separated
			 * list of file names similar to the second half
			 * of the git-diff-* output. */
			string_ncopy(file->name, buf, sepsize);
			bufsize -= sepsize;
			memmove(buf, sep + 1, bufsize);
			file = NULL;
		}
	}

	if (ferror(pipe)) {
error_out:
		pclose(pipe);
		return FALSE;
	}

	if (!view->line[view->lines - 1].data)
		add_line_data(view, NULL, LINE_STAT_NONE);

	pclose(pipe);
	return TRUE;
}

#define STATUS_DIFF_INDEX_CMD "git diff-index -z --cached HEAD"
#define STATUS_DIFF_FILES_CMD "git diff-files -z"
#define STATUS_LIST_OTHER_CMD \
	"git ls-files -z --others --exclude-per-directory=.gitignore"

#define STATUS_DIFF_SHOW_CMD \
	"git diff --root --patch-with-stat --find-copies-harder -B -C %s -- %s 2>/dev/null"

/* First parse staged info using git-diff-index(1), then parse unstaged
 * info using git-diff-files(1), and finally untracked files using
 * git-ls-files(1). */
static bool
status_open(struct view *view)
{
	struct stat statbuf;
	char exclude[SIZEOF_STR];
	char cmd[SIZEOF_STR];
	unsigned long prev_lineno = view->lineno;
	size_t i;


	for (i = 0; i < view->lines; i++)
		free(view->line[i].data);
	free(view->line);
	view->lines = view->line_size = view->lineno = 0;
	view->line = NULL;

	if (!realloc_lines(view, view->line_size + 6))
		return FALSE;

	if (!string_format(exclude, "%s/info/exclude", opt_git_dir))
		return FALSE;

	string_copy(cmd, STATUS_LIST_OTHER_CMD);

	if (stat(exclude, &statbuf) >= 0) {
		size_t cmdsize = strlen(cmd);

		if (!string_format_from(cmd, &cmdsize, " %s", "--exclude-from=") ||
		    sq_quote(cmd, cmdsize, exclude) >= sizeof(cmd))
			return FALSE;
	}

	if (!status_run(view, STATUS_DIFF_INDEX_CMD, TRUE, LINE_STAT_STAGED) ||
	    !status_run(view, STATUS_DIFF_FILES_CMD, TRUE, LINE_STAT_UNSTAGED) ||
	    !status_run(view, cmd, FALSE, LINE_STAT_UNTRACKED))
		return FALSE;

	/* If all went well restore the previous line number to stay in
	 * the context. */
	if (prev_lineno < view->lines)
		view->lineno = prev_lineno;
	else
		view->lineno = view->lines - 1;

	return TRUE;
}

static bool
status_draw(struct view *view, struct line *line, unsigned int lineno, bool selected)
{
	struct status *status = line->data;

	wmove(view->win, lineno, 0);

	if (selected) {
		wattrset(view->win, get_line_attr(LINE_CURSOR));
		wchgat(view->win, -1, 0, LINE_CURSOR, NULL);

	} else if (!status && line->type != LINE_STAT_NONE) {
		wattrset(view->win, get_line_attr(LINE_STAT_SECTION));
		wchgat(view->win, -1, 0, LINE_STAT_SECTION, NULL);

	} else {
		wattrset(view->win, get_line_attr(line->type));
	}

	if (!status) {
		char *text;

		switch (line->type) {
		case LINE_STAT_STAGED:
			text = "Changes to be committed:";
			break;

		case LINE_STAT_UNSTAGED:
			text = "Changed but not updated:";
			break;

		case LINE_STAT_UNTRACKED:
			text = "Untracked files:";
			break;

		case LINE_STAT_NONE:
			text = "    (no files)";
			break;

		default:
			return FALSE;
		}

		waddstr(view->win, text);
		return TRUE;
	}

	waddch(view->win, status->status);
	if (!selected)
		wattrset(view->win, A_NORMAL);
	wmove(view->win, lineno, 4);
	waddstr(view->win, status->name);

	return TRUE;
}

static enum request
status_enter(struct view *view, struct line *line)
{
	struct status *status = line->data;
	char path[SIZEOF_STR] = "";
	char *info;
	size_t cmdsize = 0;

	if (line->type == LINE_STAT_NONE ||
	    (!status && line[1].type == LINE_STAT_NONE)) {
		report("No file to diff");
		return REQ_NONE;
	}

	if (status && sq_quote(path, 0, status->name) >= sizeof(path))
		return REQ_QUIT;

	if (opt_cdup[0] &&
	    line->type != LINE_STAT_UNTRACKED &&
	    !string_format_from(opt_cmd, &cmdsize, "cd %s;", opt_cdup))
		return REQ_QUIT;

	switch (line->type) {
	case LINE_STAT_STAGED:
		if (!string_format_from(opt_cmd, &cmdsize, STATUS_DIFF_SHOW_CMD,
					"--cached", path))
			return REQ_QUIT;
		if (status)
			info = "Staged changes to %s";
		else
			info = "Staged changes";
		break;

	case LINE_STAT_UNSTAGED:
		if (!string_format_from(opt_cmd, &cmdsize, STATUS_DIFF_SHOW_CMD,
					"", path))
			return REQ_QUIT;
		if (status)
			info = "Unstaged changes to %s";
		else
			info = "Unstaged changes";
		break;

	case LINE_STAT_UNTRACKED:
		if (opt_pipe)
			return REQ_QUIT;


	    	if (!status) {
			report("No file to show");
			return REQ_NONE;
		}

		opt_pipe = fopen(status->name, "r");
		info = "Untracked file %s";
		break;

	default:
		die("w00t");
	}

	open_view(view, REQ_VIEW_STAGE, OPEN_RELOAD | OPEN_SPLIT);
	if (view_is_displayed(VIEW(REQ_VIEW_STAGE))) {
		if (status) {
			stage_status = *status;
		} else {
			memset(&stage_status, 0, sizeof(stage_status));
		}

		stage_line_type = line->type;
		string_format(VIEW(REQ_VIEW_STAGE)->ref, info, stage_status.name);
	}

	return REQ_NONE;
}


static bool
status_update_file(struct view *view, struct status *status, enum line_type type)
{
	char cmd[SIZEOF_STR];
	char buf[SIZEOF_STR];
	size_t cmdsize = 0;
	size_t bufsize = 0;
	size_t written = 0;
	FILE *pipe;

	if (opt_cdup[0] &&
	    type != LINE_STAT_UNTRACKED &&
	    !string_format_from(cmd, &cmdsize, "cd %s;", opt_cdup))
		return FALSE;

	switch (type) {
	case LINE_STAT_STAGED:
		if (!string_format_from(buf, &bufsize, "%06o %s\t%s%c",
				        status->old.mode,
					status->old.rev,
					status->name, 0))
			return FALSE;

		string_add(cmd, cmdsize, "git update-index -z --index-info");
		break;

	case LINE_STAT_UNSTAGED:
	case LINE_STAT_UNTRACKED:
		if (!string_format_from(buf, &bufsize, "%s%c", status->name, 0))
			return FALSE;

		string_add(cmd, cmdsize, "git update-index -z --add --remove --stdin");
		break;

	default:
		die("w00t");
	}

	pipe = popen(cmd, "w");
	if (!pipe)
		return FALSE;

	while (!ferror(pipe) && written < bufsize) {
		written += fwrite(buf + written, 1, bufsize - written, pipe);
	}

	pclose(pipe);

	if (written != bufsize)
		return FALSE;

	return TRUE;
}

static void
status_update(struct view *view)
{
	struct line *line = &view->line[view->lineno];

	assert(view->lines);

	if (!line->data) {
		while (++line < view->line + view->lines && line->data) {
			if (!status_update_file(view, line->data, line->type))
				report("Failed to update file status");
		}

		if (!line[-1].data) {
			report("Nothing to update");
			return;
		}

	} else if (!status_update_file(view, line->data, line->type)) {
		report("Failed to update file status");
	}

	open_view(view, REQ_VIEW_STATUS, OPEN_RELOAD);
}

static enum request
status_request(struct view *view, enum request request, struct line *line)
{
	struct status *status = line->data;

	switch (request) {
	case REQ_STATUS_UPDATE:
		status_update(view);
		break;

	case REQ_EDIT:
		if (!status)
			return request;

		open_editor(status->status != '?', status->name);
		open_view(view, REQ_VIEW_STATUS, OPEN_RELOAD);
		break;

	case REQ_ENTER:
		status_enter(view, line);
		break;

	case REQ_REFRESH:
		open_view(view, REQ_VIEW_STATUS, OPEN_RELOAD);
		break;

	default:
		return request;
	}

	return REQ_NONE;
}

static void
status_select(struct view *view, struct line *line)
{
	struct status *status = line->data;
	char file[SIZEOF_STR] = "all files";
	char *text;

	if (status && !string_format(file, "'%s'", status->name))
		return;

	if (!status && line[1].type == LINE_STAT_NONE)
		line++;

	switch (line->type) {
	case LINE_STAT_STAGED:
		text = "Press %s to unstage %s for commit";
		break;

	case LINE_STAT_UNSTAGED:
		text = "Press %s to stage %s for commit";
		break;

	case LINE_STAT_UNTRACKED:
		text = "Press %s to stage %s for addition";
		break;

	case LINE_STAT_NONE:
		text = "Nothing to update";
		break;

	default:
		die("w00t");
	}

	string_format(view->ref, text, get_key(REQ_STATUS_UPDATE), file);
}

static bool
status_grep(struct view *view, struct line *line)
{
	struct status *status = line->data;
	enum { S_STATUS, S_NAME, S_END } state;
	char buf[2] = "?";
	regmatch_t pmatch;

	if (!status)
		return FALSE;

	for (state = S_STATUS; state < S_END; state++) {
		char *text;

		switch (state) {
		case S_NAME:	text = status->name;	break;
		case S_STATUS:
			buf[0] = status->status;
			text = buf;
			break;

		default:
			return FALSE;
		}

		if (regexec(view->regex, text, 1, &pmatch, 0) != REG_NOMATCH)
			return TRUE;
	}

	return FALSE;
}

static struct view_ops status_ops = {
	"file",
	status_open,
	NULL,
	status_draw,
	status_request,
	status_grep,
	status_select,
};


static bool
stage_diff_line(FILE *pipe, struct line *line)
{
	char *buf = line->data;
	size_t bufsize = strlen(buf);
	size_t written = 0;

	while (!ferror(pipe) && written < bufsize) {
		written += fwrite(buf + written, 1, bufsize - written, pipe);
	}

	fputc('\n', pipe);

	return written == bufsize;
}

static struct line *
stage_diff_hdr(struct view *view, struct line *line)
{
	int diff_hdr_dir = line->type == LINE_DIFF_CHUNK ? -1 : 1;
	struct line *diff_hdr;

	if (line->type == LINE_DIFF_CHUNK)
		diff_hdr = line - 1;
	else
		diff_hdr = view->line + 1;

	while (diff_hdr > view->line && diff_hdr < view->line + view->lines) {
		if (diff_hdr->type == LINE_DIFF_HEADER)
			return diff_hdr;

		diff_hdr += diff_hdr_dir;
	}

	return NULL;
}

static bool
stage_update_chunk(struct view *view, struct line *line)
{
	char cmd[SIZEOF_STR];
	size_t cmdsize = 0;
	struct line *diff_hdr, *diff_chunk, *diff_end;
	FILE *pipe;

	diff_hdr = stage_diff_hdr(view, line);
	if (!diff_hdr)
		return FALSE;

	if (opt_cdup[0] &&
	    !string_format_from(cmd, &cmdsize, "cd %s;", opt_cdup))
		return FALSE;

	if (!string_format_from(cmd, &cmdsize,
				"git apply --cached %s - && "
				"git update-index -q --unmerged --refresh 2>/dev/null",
				stage_line_type == LINE_STAT_STAGED ? "-R" : ""))
		return FALSE;

	pipe = popen(cmd, "w");
	if (!pipe)
		return FALSE;

	diff_end = view->line + view->lines;
	if (line->type != LINE_DIFF_CHUNK) {
		diff_chunk = diff_hdr;

	} else {
		for (diff_chunk = line + 1; diff_chunk < diff_end; diff_chunk++)
			if (diff_chunk->type == LINE_DIFF_CHUNK ||
			    diff_chunk->type == LINE_DIFF_HEADER)
				diff_end = diff_chunk;

		diff_chunk = line;

		while (diff_hdr->type != LINE_DIFF_CHUNK) {
			switch (diff_hdr->type) {
			case LINE_DIFF_HEADER:
			case LINE_DIFF_INDEX:
			case LINE_DIFF_ADD:
			case LINE_DIFF_DEL:
				break;

			default:
				diff_hdr++;
				continue;
			}

			if (!stage_diff_line(pipe, diff_hdr++)) {
				pclose(pipe);
				return FALSE;
			}
		}
	}

	while (diff_chunk < diff_end && stage_diff_line(pipe, diff_chunk))
		diff_chunk++;

	pclose(pipe);

	if (diff_chunk != diff_end)
		return FALSE;

	return TRUE;
}

static void
stage_update(struct view *view, struct line *line)
{
	if (stage_line_type != LINE_STAT_UNTRACKED &&
	    (line->type == LINE_DIFF_CHUNK || !stage_status.status)) {
		if (!stage_update_chunk(view, line)) {
			report("Failed to apply chunk");
			return;
		}

	} else if (!status_update_file(view, &stage_status, stage_line_type)) {
		report("Failed to update file");
		return;
	}

	open_view(view, REQ_VIEW_STATUS, OPEN_RELOAD);

	view = VIEW(REQ_VIEW_STATUS);
	if (view_is_displayed(view))
		status_enter(view, &view->line[view->lineno]);
}

static enum request
stage_request(struct view *view, enum request request, struct line *line)
{
	switch (request) {
	case REQ_STATUS_UPDATE:
		stage_update(view, line);
		break;

	case REQ_EDIT:
		if (!stage_status.name[0])
			return request;

		open_editor(stage_status.status != '?', stage_status.name);
		break;

	case REQ_ENTER:
		pager_request(view, request, line);
		break;

	default:
		return request;
	}

	return REQ_NONE;
}

static struct view_ops stage_ops = {
	"line",
	NULL,
	pager_read,
	pager_draw,
	stage_request,
	pager_grep,
	pager_select,
};


/*
 * Revision graph
 */

struct commit {
	char id[SIZEOF_REV];		/* SHA1 ID. */
	char title[128];		/* First line of the commit message. */
	char author[75];		/* Author of the commit. */
	struct tm time;			/* Date from the author ident. */
	struct ref **refs;		/* Repository references. */
	chtype graph[SIZEOF_REVGRAPH];	/* Ancestry chain graphics. */
	size_t graph_size;		/* The width of the graph array. */
};

/* Size of rev graph with no  "padding" columns */
#define SIZEOF_REVITEMS	(SIZEOF_REVGRAPH - (SIZEOF_REVGRAPH / 2))

struct rev_graph {
	struct rev_graph *prev, *next, *parents;
	char rev[SIZEOF_REVITEMS][SIZEOF_REV];
	size_t size;
	struct commit *commit;
	size_t pos;
};

/* Parents of the commit being visualized. */
static struct rev_graph graph_parents[4];

/* The current stack of revisions on the graph. */
static struct rev_graph graph_stacks[4] = {
	{ &graph_stacks[3], &graph_stacks[1], &graph_parents[0] },
	{ &graph_stacks[0], &graph_stacks[2], &graph_parents[1] },
	{ &graph_stacks[1], &graph_stacks[3], &graph_parents[2] },
	{ &graph_stacks[2], &graph_stacks[0], &graph_parents[3] },
};

static inline bool
graph_parent_is_merge(struct rev_graph *graph)
{
	return graph->parents->size > 1;
}

static inline void
append_to_rev_graph(struct rev_graph *graph, chtype symbol)
{
	struct commit *commit = graph->commit;

	if (commit->graph_size < ARRAY_SIZE(commit->graph) - 1)
		commit->graph[commit->graph_size++] = symbol;
}

static void
done_rev_graph(struct rev_graph *graph)
{
	if (graph_parent_is_merge(graph) &&
	    graph->pos < graph->size - 1 &&
	    graph->next->size == graph->size + graph->parents->size - 1) {
		size_t i = graph->pos + graph->parents->size - 1;

		graph->commit->graph_size = i * 2;
		while (i < graph->next->size - 1) {
			append_to_rev_graph(graph, ' ');
			append_to_rev_graph(graph, '\\');
			i++;
		}
	}

	graph->size = graph->pos = 0;
	graph->commit = NULL;
	memset(graph->parents, 0, sizeof(*graph->parents));
}

static void
push_rev_graph(struct rev_graph *graph, char *parent)
{
	int i;

	/* "Collapse" duplicate parents lines.
	 *
	 * FIXME: This needs to also update update the drawn graph but
	 * for now it just serves as a method for pruning graph lines. */
	for (i = 0; i < graph->size; i++)
		if (!strncmp(graph->rev[i], parent, SIZEOF_REV))
			return;

	if (graph->size < SIZEOF_REVITEMS) {
		string_copy_rev(graph->rev[graph->size++], parent);
	}
}

static chtype
get_rev_graph_symbol(struct rev_graph *graph)
{
	chtype symbol;

	if (graph->parents->size == 0)
		symbol = REVGRAPH_INIT;
	else if (graph_parent_is_merge(graph))
		symbol = REVGRAPH_MERGE;
	else if (graph->pos >= graph->size)
		symbol = REVGRAPH_BRANCH;
	else
		symbol = REVGRAPH_COMMIT;

	return symbol;
}

static void
draw_rev_graph(struct rev_graph *graph)
{
	struct rev_filler {
		chtype separator, line;
	};
	enum { DEFAULT, RSHARP, RDIAG, LDIAG };
	static struct rev_filler fillers[] = {
		{ ' ',	REVGRAPH_LINE },
		{ '`',	'.' },
		{ '\'',	' ' },
		{ '/',	' ' },
	};
	chtype symbol = get_rev_graph_symbol(graph);
	struct rev_filler *filler;
	size_t i;

	filler = &fillers[DEFAULT];

	for (i = 0; i < graph->pos; i++) {
		append_to_rev_graph(graph, filler->line);
		if (graph_parent_is_merge(graph->prev) &&
		    graph->prev->pos == i)
			filler = &fillers[RSHARP];

		append_to_rev_graph(graph, filler->separator);
	}

	/* Place the symbol for this revision. */
	append_to_rev_graph(graph, symbol);

	if (graph->prev->size > graph->size)
		filler = &fillers[RDIAG];
	else
		filler = &fillers[DEFAULT];

	i++;

	for (; i < graph->size; i++) {
		append_to_rev_graph(graph, filler->separator);
		append_to_rev_graph(graph, filler->line);
		if (graph_parent_is_merge(graph->prev) &&
		    i < graph->prev->pos + graph->parents->size)
			filler = &fillers[RSHARP];
		if (graph->prev->size > graph->size)
			filler = &fillers[LDIAG];
	}

	if (graph->prev->size > graph->size) {
		append_to_rev_graph(graph, filler->separator);
		if (filler->line != ' ')
			append_to_rev_graph(graph, filler->line);
	}
}

/* Prepare the next rev graph */
static void
prepare_rev_graph(struct rev_graph *graph)
{
	size_t i;

	/* First, traverse all lines of revisions up to the active one. */
	for (graph->pos = 0; graph->pos < graph->size; graph->pos++) {
		if (!strcmp(graph->rev[graph->pos], graph->commit->id))
			break;

		push_rev_graph(graph->next, graph->rev[graph->pos]);
	}

	/* Interleave the new revision parent(s). */
	for (i = 0; i < graph->parents->size; i++)
		push_rev_graph(graph->next, graph->parents->rev[i]);

	/* Lastly, put any remaining revisions. */
	for (i = graph->pos + 1; i < graph->size; i++)
		push_rev_graph(graph->next, graph->rev[i]);
}

static void
update_rev_graph(struct rev_graph *graph)
{
	/* If this is the finalizing update ... */
	if (graph->commit)
		prepare_rev_graph(graph);

	/* Graph visualization needs a one rev look-ahead,
	 * so the first update doesn't visualize anything. */
	if (!graph->prev->commit)
		return;

	draw_rev_graph(graph->prev);
	done_rev_graph(graph->prev->prev);
}


/*
 * Main view backend
 */

static bool
main_draw(struct view *view, struct line *line, unsigned int lineno, bool selected)
{
	char buf[DATE_COLS + 1];
	struct commit *commit = line->data;
	enum line_type type;
	int col = 0;
	size_t timelen;
	size_t authorlen;
	int trimmed = 1;

	if (!*commit->author)
		return FALSE;

	wmove(view->win, lineno, col);

	if (selected) {
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

	if (opt_utf8) {
		authorlen = utf8_length(commit->author, AUTHOR_COLS - 2, &col, &trimmed);
	} else {
		authorlen = strlen(commit->author);
		if (authorlen > AUTHOR_COLS - 2) {
			authorlen = AUTHOR_COLS - 2;
			trimmed = 1;
		}
	}

	if (trimmed) {
		waddnstr(view->win, commit->author, authorlen);
		if (type != LINE_CURSOR)
			wattrset(view->win, get_line_attr(LINE_MAIN_DELIM));
		waddch(view->win, '~');
	} else {
		waddstr(view->win, commit->author);
	}

	col += AUTHOR_COLS;
	if (type != LINE_CURSOR)
		wattrset(view->win, A_NORMAL);

	if (opt_rev_graph && commit->graph_size) {
		size_t i;

		wmove(view->win, lineno, col);
		/* Using waddch() instead of waddnstr() ensures that
		 * they'll be rendered correctly for the cursor line. */
		for (i = 0; i < commit->graph_size; i++)
			waddch(view->win, commit->graph[i]);

		waddch(view->win, ' ');
		col += commit->graph_size + 1;
	}

	wmove(view->win, lineno, col);

	if (commit->refs) {
		size_t i = 0;

		do {
			if (type == LINE_CURSOR)
				;
			else if (commit->refs[i]->tag)
				wattrset(view->win, get_line_attr(LINE_MAIN_TAG));
			else if (commit->refs[i]->remote)
				wattrset(view->win, get_line_attr(LINE_MAIN_REMOTE));
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
	static struct rev_graph *graph = graph_stacks;
	enum line_type type;
	struct commit *commit;

	if (!line) {
		update_rev_graph(graph);
		return TRUE;
	}

	type = get_line_type(line);
	if (type == LINE_COMMIT) {
		commit = calloc(1, sizeof(struct commit));
		if (!commit)
			return FALSE;

		string_copy_rev(commit->id, line + STRING_SIZE("commit "));
		commit->refs = get_refs(commit->id);
		graph->commit = commit;
		add_line_data(view, commit, LINE_MAIN_COMMIT);
		return TRUE;
	}

	if (!view->lines)
		return TRUE;
	commit = view->line[view->lines - 1].data;

	switch (type) {
	case LINE_PARENT:
		push_rev_graph(graph->parents, line + STRING_SIZE("parent "));
		break;

	case LINE_AUTHOR:
	{
		/* Parse author lines where the name may be empty:
		 *	author  <email@address.tld> 1138474660 +0100
		 */
		char *ident = line + STRING_SIZE("author ");
		char *nameend = strchr(ident, '<');
		char *emailend = strchr(ident, '>');

		if (!nameend || !emailend)
			break;

		update_rev_graph(graph);
		graph = graph->next;

		*nameend = *emailend = 0;
		ident = chomp_string(ident);
		if (!*ident) {
			ident = chomp_string(nameend + 1);
			if (!*ident)
				ident = "Unknown";
		}

		string_ncopy(commit->author, ident, strlen(ident));

		/* Parse epoch and timezone */
		if (emailend[1] == ' ') {
			char *secs = emailend + 2;
			char *zone = strchr(secs, ' ');
			time_t time = (time_t) atol(secs);

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
		/* Fill in the commit title if it has not already been set. */
		if (commit->title[0])
			break;

		/* Require titles to start with a non-space character at the
		 * offset used by git log. */
		if (strncmp(line, "    ", 4))
			break;
		line += 4;
		/* Well, if the title starts with a whitespace character,
		 * try to be forgiving.  Otherwise we end up with no title. */
		while (isspace(*line))
			line++;
		if (*line == '\0')
			break;
		/* FIXME: More graceful handling of titles; append "..." to
		 * shortened titles, etc. */

		string_ncopy(commit->title, line, strlen(line));
	}

	return TRUE;
}

static void
cherry_pick_commit(struct commit *commit)
{
	char cmd[SIZEOF_STR];
	char *cherry_pick = getenv("TIG_CHERRY_PICK");

	if (!cherry_pick)
		cherry_pick = "git cherry-pick";

	if (string_format(cmd, "%s %s", cherry_pick, commit->id)) {
		def_prog_mode();           /* save current tty modes */
		endwin();                  /* restore original tty modes */
		system(cmd);
		fprintf(stderr, "Press Enter to continue");
		getc(stdin);
		reset_prog_mode();
		redraw_display();
	}
}

static enum request
main_request(struct view *view, enum request request, struct line *line)
{
	enum open_flags flags = display[0] == view ? OPEN_SPLIT : OPEN_DEFAULT;

	if (request == REQ_ENTER)
		open_view(view, REQ_VIEW_DIFF, flags);
	else if (request == REQ_CHERRY_PICK)
		cherry_pick_commit(line->data);
	else
		return request;

	return REQ_NONE;
}

static bool
main_grep(struct view *view, struct line *line)
{
	struct commit *commit = line->data;
	enum { S_TITLE, S_AUTHOR, S_DATE, S_END } state;
	char buf[DATE_COLS + 1];
	regmatch_t pmatch;

	for (state = S_TITLE; state < S_END; state++) {
		char *text;

		switch (state) {
		case S_TITLE:	text = commit->title;	break;
		case S_AUTHOR:	text = commit->author;	break;
		case S_DATE:
			if (!strftime(buf, sizeof(buf), DATE_FORMAT, &commit->time))
				continue;
			text = buf;
			break;

		default:
			return FALSE;
		}

		if (regexec(view->regex, text, 1, &pmatch, 0) != REG_NOMATCH)
			return TRUE;
	}

	return FALSE;
}

static void
main_select(struct view *view, struct line *line)
{
	struct commit *commit = line->data;

	string_copy_rev(view->ref, commit->id);
	string_copy_rev(ref_commit, view->ref);
}

static struct view_ops main_ops = {
	"commit",
	NULL,
	main_read,
	main_draw,
	main_request,
	main_grep,
	main_select,
};


/*
 * Unicode / UTF-8 handling
 *
 * NOTE: Much of the following code for dealing with unicode is derived from
 * ELinks' UTF-8 code developed by Scrool <scroolik@gmail.com>. Origin file is
 * src/intl/charset.c from the utf8 branch commit elinks-0.11.0-g31f2c28.
 */

/* I've (over)annotated a lot of code snippets because I am not entirely
 * confident that the approach taken by this small UTF-8 interface is correct.
 * --jonas */

static inline int
unicode_width(unsigned long c)
{
	if (c >= 0x1100 &&
	   (c <= 0x115f				/* Hangul Jamo */
	    || c == 0x2329
	    || c == 0x232a
	    || (c >= 0x2e80  && c <= 0xa4cf && c != 0x303f)
						/* CJK ... Yi */
	    || (c >= 0xac00  && c <= 0xd7a3)	/* Hangul Syllables */
	    || (c >= 0xf900  && c <= 0xfaff)	/* CJK Compatibility Ideographs */
	    || (c >= 0xfe30  && c <= 0xfe6f)	/* CJK Compatibility Forms */
	    || (c >= 0xff00  && c <= 0xff60)	/* Fullwidth Forms */
	    || (c >= 0xffe0  && c <= 0xffe6)
	    || (c >= 0x20000 && c <= 0x2fffd)
	    || (c >= 0x30000 && c <= 0x3fffd)))
		return 2;

	return 1;
}

/* Number of bytes used for encoding a UTF-8 character indexed by first byte.
 * Illegal bytes are set one. */
static const unsigned char utf8_bytes[256] = {
	1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,
	2,2,2,2,2,2,2,2, 2,2,2,2,2,2,2,2, 2,2,2,2,2,2,2,2, 2,2,2,2,2,2,2,2,
	3,3,3,3,3,3,3,3, 3,3,3,3,3,3,3,3, 4,4,4,4,4,4,4,4, 5,5,5,5,6,6,1,1,
};

/* Decode UTF-8 multi-byte representation into a unicode character. */
static inline unsigned long
utf8_to_unicode(const char *string, size_t length)
{
	unsigned long unicode;

	switch (length) {
	case 1:
		unicode  =   string[0];
		break;
	case 2:
		unicode  =  (string[0] & 0x1f) << 6;
		unicode +=  (string[1] & 0x3f);
		break;
	case 3:
		unicode  =  (string[0] & 0x0f) << 12;
		unicode += ((string[1] & 0x3f) << 6);
		unicode +=  (string[2] & 0x3f);
		break;
	case 4:
		unicode  =  (string[0] & 0x0f) << 18;
		unicode += ((string[1] & 0x3f) << 12);
		unicode += ((string[2] & 0x3f) << 6);
		unicode +=  (string[3] & 0x3f);
		break;
	case 5:
		unicode  =  (string[0] & 0x0f) << 24;
		unicode += ((string[1] & 0x3f) << 18);
		unicode += ((string[2] & 0x3f) << 12);
		unicode += ((string[3] & 0x3f) << 6);
		unicode +=  (string[4] & 0x3f);
		break;
	case 6:
		unicode  =  (string[0] & 0x01) << 30;
		unicode += ((string[1] & 0x3f) << 24);
		unicode += ((string[2] & 0x3f) << 18);
		unicode += ((string[3] & 0x3f) << 12);
		unicode += ((string[4] & 0x3f) << 6);
		unicode +=  (string[5] & 0x3f);
		break;
	default:
		die("Invalid unicode length");
	}

	/* Invalid characters could return the special 0xfffd value but NUL
	 * should be just as good. */
	return unicode > 0xffff ? 0 : unicode;
}

/* Calculates how much of string can be shown within the given maximum width
 * and sets trimmed parameter to non-zero value if all of string could not be
 * shown.
 *
 * Additionally, adds to coloffset how many many columns to move to align with
 * the expected position. Takes into account how multi-byte and double-width
 * characters will effect the cursor position.
 *
 * Returns the number of bytes to output from string to satisfy max_width. */
static size_t
utf8_length(const char *string, size_t max_width, int *coloffset, int *trimmed)
{
	const char *start = string;
	const char *end = strchr(string, '\0');
	size_t mbwidth = 0;
	size_t width = 0;

	*trimmed = 0;

	while (string < end) {
		int c = *(unsigned char *) string;
		unsigned char bytes = utf8_bytes[c];
		size_t ucwidth;
		unsigned long unicode;

		if (string + bytes > end)
			break;

		/* Change representation to figure out whether
		 * it is a single- or double-width character. */

		unicode = utf8_to_unicode(string, bytes);
		/* FIXME: Graceful handling of invalid unicode character. */
		if (!unicode)
			break;

		ucwidth = unicode_width(unicode);
		width  += ucwidth;
		if (width > max_width) {
			*trimmed = 1;
			break;
		}

		/* The column offset collects the differences between the
		 * number of bytes encoding a character and the number of
		 * columns will be used for rendering said character.
		 *
		 * So if some character A is encoded in 2 bytes, but will be
		 * represented on the screen using only 1 byte this will and up
		 * adding 1 to the multi-byte column offset.
		 *
		 * Assumes that no double-width character can be encoding in
		 * less than two bytes. */
		if (bytes > ucwidth)
			mbwidth += bytes - ucwidth;

		string  += bytes;
	}

	*coloffset += mbwidth;

	return string - start;
}


/*
 * Status management
 */

/* Whether or not the curses interface has been initialized. */
static bool cursed = FALSE;

/* The status window is used for polling keystrokes. */
static WINDOW *status_win;

static bool status_empty = TRUE;

/* Update status and title window. */
static void
report(const char *msg, ...)
{
	struct view *view = display[current_view];

	if (input_mode)
		return;

	if (!status_empty || *msg) {
		va_list args;

		va_start(args, msg);

		wmove(status_win, 0, 0);
		if (*msg) {
			vwprintw(status_win, msg, args);
			status_empty = FALSE;
		} else {
			status_empty = TRUE;
		}
		wclrtoeol(status_win);
		wrefresh(status_win);

		va_end(args);
	}

	update_view_title(view);
	update_display_cursor(view);
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

		if (!io)
			die("Failed to open /dev/tty");
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

static char *
read_prompt(const char *prompt)
{
	enum { READING, STOP, CANCEL } status = READING;
	static char buf[sizeof(opt_cmd) - STRING_SIZE("git \0")];
	int pos = 0;

	while (status == READING) {
		struct view *view;
		int i, key;

		input_mode = TRUE;

		foreach_view (view, i)
			update_view(view);

		input_mode = FALSE;

		mvwprintw(status_win, 0, 0, "%s%.*s", prompt, pos, buf);
		wclrtoeol(status_win);

		/* Refresh, accept single keystroke of input */
		key = wgetch(status_win);
		switch (key) {
		case KEY_RETURN:
		case KEY_ENTER:
		case '\n':
			status = pos ? STOP : CANCEL;
			break;

		case KEY_BACKSPACE:
			if (pos > 0)
				pos--;
			else
				status = CANCEL;
			break;

		case KEY_ESC:
			status = CANCEL;
			break;

		case ERR:
			break;

		default:
			if (pos >= sizeof(buf)) {
				report("Input string too long");
				return NULL;
			}

			if (isprint(key))
				buf[pos++] = (char) key;
		}
	}

	/* Clear the status window */
	status_empty = FALSE;
	report("");

	if (status == CANCEL)
		return NULL;

	buf[pos++] = 0;

	return buf;
}

/*
 * Repository references
 */

static struct ref *refs;
static size_t refs_size;

/* Id <-> ref store */
static struct ref ***id_refs;
static size_t id_refs_size;

static struct ref **
get_refs(char *id)
{
	struct ref ***tmp_id_refs;
	struct ref **ref_list = NULL;
	size_t ref_list_size = 0;
	size_t i;

	for (i = 0; i < id_refs_size; i++)
		if (!strcmp(id, id_refs[i][0]->id))
			return id_refs[i];

	tmp_id_refs = realloc(id_refs, (id_refs_size + 1) * sizeof(*id_refs));
	if (!tmp_id_refs)
		return NULL;

	id_refs = tmp_id_refs;

	for (i = 0; i < refs_size; i++) {
		struct ref **tmp;

		if (strcmp(id, refs[i].id))
			continue;

		tmp = realloc(ref_list, (ref_list_size + 1) * sizeof(*ref_list));
		if (!tmp) {
			if (ref_list)
				free(ref_list);
			return NULL;
		}

		ref_list = tmp;
		if (ref_list_size > 0)
			ref_list[ref_list_size - 1]->next = 1;
		ref_list[ref_list_size] = &refs[i];

		/* XXX: The properties of the commit chains ensures that we can
		 * safely modify the shared ref. The repo references will
		 * always be similar for the same id. */
		ref_list[ref_list_size]->next = 0;
		ref_list_size++;
	}

	if (ref_list)
		id_refs[id_refs_size++] = ref_list;

	return ref_list;
}

static int
read_ref(char *id, size_t idlen, char *name, size_t namelen)
{
	struct ref *ref;
	bool tag = FALSE;
	bool remote = FALSE;

	if (!strncmp(name, "refs/tags/", STRING_SIZE("refs/tags/"))) {
		/* Commits referenced by tags has "^{}" appended. */
		if (name[namelen - 1] != '}')
			return OK;

		while (namelen > 0 && name[namelen] != '^')
			namelen--;

		tag = TRUE;
		namelen -= STRING_SIZE("refs/tags/");
		name	+= STRING_SIZE("refs/tags/");

	} else if (!strncmp(name, "refs/remotes/", STRING_SIZE("refs/remotes/"))) {
		remote = TRUE;
		namelen -= STRING_SIZE("refs/remotes/");
		name	+= STRING_SIZE("refs/remotes/");

	} else if (!strncmp(name, "refs/heads/", STRING_SIZE("refs/heads/"))) {
		namelen -= STRING_SIZE("refs/heads/");
		name	+= STRING_SIZE("refs/heads/");

	} else if (!strcmp(name, "HEAD")) {
		return OK;
	}

	refs = realloc(refs, sizeof(*refs) * (refs_size + 1));
	if (!refs)
		return ERR;

	ref = &refs[refs_size++];
	ref->name = malloc(namelen + 1);
	if (!ref->name)
		return ERR;

	strncpy(ref->name, name, namelen);
	ref->name[namelen] = 0;
	ref->tag = tag;
	ref->remote = remote;
	string_copy_rev(ref->id, id);

	return OK;
}

static int
load_refs(void)
{
	const char *cmd_env = getenv("TIG_LS_REMOTE");
	const char *cmd = cmd_env && *cmd_env ? cmd_env : TIG_LS_REMOTE;

	return read_properties(popen(cmd, "r"), "\t", read_ref);
}

static int
read_repo_config_option(char *name, size_t namelen, char *value, size_t valuelen)
{
	if (!strcmp(name, "i18n.commitencoding"))
		string_ncopy(opt_encoding, value, valuelen);

	if (!strcmp(name, "core.editor"))
		string_ncopy(opt_editor, value, valuelen);

	return OK;
}

static int
load_repo_config(void)
{
	return read_properties(popen(GIT_CONFIG " --list", "r"),
			       "=", read_repo_config_option);
}

static int
read_repo_info(char *name, size_t namelen, char *value, size_t valuelen)
{
	if (!opt_git_dir[0])
		string_ncopy(opt_git_dir, name, namelen);
	else
		string_ncopy(opt_cdup, name, namelen);
	return OK;
}

/* XXX: The line outputted by "--show-cdup" can be empty so the option
 * must be the last one! */
static int
load_repo_info(void)
{
	return read_properties(popen("git rev-parse --git-dir --show-cdup 2>/dev/null", "r"),
			       "=", read_repo_info);
}

static int
read_properties(FILE *pipe, const char *separators,
		int (*read_property)(char *, size_t, char *, size_t))
{
	char buffer[BUFSIZ];
	char *name;
	int state = OK;

	if (!pipe)
		return ERR;

	while (state == OK && (name = fgets(buffer, sizeof(buffer), pipe))) {
		char *value;
		size_t namelen;
		size_t valuelen;

		name = chomp_string(name);
		namelen = strcspn(name, separators);

		if (name[namelen]) {
			name[namelen] = 0;
			value = chomp_string(name + namelen + 1);
			valuelen = strlen(value);

		} else {
			value = "";
			valuelen = 0;
		}

		state = read_property(name, namelen, value, valuelen);
	}

	if (state != ERR && ferror(pipe))
		state = ERR;

	pclose(pipe);

	return state;
}


/*
 * Main
 */

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

	if (setlocale(LC_ALL, "")) {
		char *codeset = nl_langinfo(CODESET);

		string_ncopy(opt_codeset, codeset, strlen(codeset));
	}

	if (load_repo_info() == ERR)
		die("Failed to load repo info.");

	if (load_options() == ERR)
		die("Failed to load user config.");

	/* Load the repo config file so options can be overwritten from
	 * the command line. */
	if (load_repo_config() == ERR)
		die("Failed to load repo config.");

	if (!parse_options(argc, argv))
		return 0;

	/* Require a git repository unless when running in pager mode. */
	if (!opt_git_dir[0])
		die("Not a git repository");

	if (*opt_codeset && strcmp(opt_codeset, opt_encoding)) {
		opt_iconv = iconv_open(opt_codeset, opt_encoding);
		if (opt_iconv == ICONV_NONE)
			die("Failed to initialize character set conversion");
	}

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

		/* wgetch() with nodelay() enabled returns ERR when there's no
		 * input. */
		if (key == ERR) {
			request = REQ_NONE;
			continue;
		}

		request = get_keybinding(display[current_view]->keymap, key);

		/* Some low-level request handling. This keeps access to
		 * status_win restricted. */
		switch (request) {
		case REQ_PROMPT:
		{
			char *cmd = read_prompt(":");

			if (cmd && string_format(opt_cmd, "git %s", cmd)) {
				if (strncmp(cmd, "show", 4) && isspace(cmd[4])) {
					opt_request = REQ_VIEW_DIFF;
				} else {
					opt_request = REQ_VIEW_PAGER;
				}
				break;
			}

			request = REQ_NONE;
			break;
		}
		case REQ_SEARCH:
		case REQ_SEARCH_BACK:
		{
			const char *prompt = request == REQ_SEARCH
					   ? "/" : "?";
			char *search = read_prompt(prompt);

			if (search)
				string_ncopy(opt_search, search, strlen(search));
			else
				request = REQ_NONE;
			break;
		}
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
