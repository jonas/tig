/* Copyright (c) 2006-2013 Jonas Fonseca <fonseca@diku.dk>
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

#define WARN_MISSING_CURSES_CONFIGURATION

#include "tig.h"
#include "util.h"
#include "io.h"
#include "refs.h"
#include "graph.h"
#include "git.h"

static void report(const char *msg, ...) PRINTF_LIKE(1, 2);
#define report_clear() report("%s", "")


enum input_status {
	INPUT_OK,
	INPUT_SKIP,
	INPUT_STOP,
	INPUT_CANCEL
};

typedef enum input_status (*input_handler)(void *data, char *buf, int c);

static char *prompt_input(const char *prompt, input_handler handler, void *data);
static bool prompt_yesno(const char *prompt);
static char *read_prompt(const char *prompt);

struct menu_item {
	int hotkey;
	const char *text;
	void *data;
};

static bool prompt_menu(const char *prompt, const struct menu_item *items, int *selected);

#define VERTICAL_SPLIT_ENUM(_) \
	_(VERTICAL_SPLIT, HORIZONTAL), \
	_(VERTICAL_SPLIT, VERTICAL), \
	_(VERTICAL_SPLIT, AUTO)

DEFINE_ENUM(vertical_split, VERTICAL_SPLIT_ENUM);

#define GRAPHIC_ENUM(_) \
	_(GRAPHIC, ASCII), \
	_(GRAPHIC, DEFAULT), \
	_(GRAPHIC, UTF_8)

DEFINE_ENUM(graphic, GRAPHIC_ENUM);

#define DATE_ENUM(_) \
	_(DATE, NO), \
	_(DATE, DEFAULT), \
	_(DATE, LOCAL), \
	_(DATE, RELATIVE), \
	_(DATE, SHORT)

DEFINE_ENUM(date, DATE_ENUM);

struct time {
	time_t sec;
	int tz;
};

static inline int timecmp(const struct time *t1, const struct time *t2)
{
	return t1->sec - t2->sec;
}

static const char *
mkdate(const struct time *time, enum date date)
{
	static char buf[DATE_WIDTH + 1];
	static const struct enum_map_entry reldate[] = {
		{ "second", 1,			60 * 2 },
		{ "minute", 60,			60 * 60 * 2 },
		{ "hour",   60 * 60,		60 * 60 * 24 * 2 },
		{ "day",    60 * 60 * 24,	60 * 60 * 24 * 7 * 2 },
		{ "week",   60 * 60 * 24 * 7,	60 * 60 * 24 * 7 * 5 },
		{ "month",  60 * 60 * 24 * 30,	60 * 60 * 24 * 365 },
		{ "year",   60 * 60 * 24 * 365, 0 },
	};
	struct tm tm;

	if (!date || !time || !time->sec)
		return "";

	if (date == DATE_RELATIVE) {
		struct timeval now;
		time_t date = time->sec + time->tz;
		time_t seconds;
		int i;

		gettimeofday(&now, NULL);
		seconds = now.tv_sec < date ? date - now.tv_sec : now.tv_sec - date;
		for (i = 0; i < ARRAY_SIZE(reldate); i++) {
			if (seconds >= reldate[i].value && reldate[i].value)
				continue;

			seconds /= reldate[i].namelen;
			if (!string_format(buf, "%ld %s%s %s",
					   seconds, reldate[i].name,
					   seconds > 1 ? "s" : "",
					   now.tv_sec >= date ? "ago" : "ahead"))
				break;
			return buf;
		}
	}

	if (date == DATE_LOCAL) {
		time_t date = time->sec + time->tz;
		localtime_r(&date, &tm);
	}
	else {
		gmtime_r(&time->sec, &tm);
	}
	return strftime(buf, sizeof(buf), DATE_FORMAT, &tm) ? buf : NULL;
}

#define FILE_SIZE_ENUM(_) \
	_(FILE_SIZE, NO), \
	_(FILE_SIZE, DEFAULT), \
	_(FILE_SIZE, UNITS)

DEFINE_ENUM(file_size, FILE_SIZE_ENUM);

static const char *
mkfilesize(unsigned long size, enum file_size format)
{
	static char buf[64 + 1];
	static const char relsize[] = {
		'B', 'K', 'M', 'G', 'T', 'P'
	};

	if (!format)
		return "";

	if (format == FILE_SIZE_UNITS) {
		const char *fmt = "%.0f%c";
		double rsize = size;
		int i;

		for (i = 0; i < ARRAY_SIZE(relsize); i++) {
			if (rsize > 1024.0 && i + 1 < ARRAY_SIZE(relsize)) {
				rsize /= 1024;
				continue;
			}

			size = rsize * 10;
			if (size % 10 > 0)
				fmt = "%.1f%c";

			return string_format(buf, fmt, rsize, relsize[i])
				? buf : NULL;
		}
	}

	return string_format(buf, "%ld", size) ? buf : NULL;
}

#define AUTHOR_ENUM(_) \
	_(AUTHOR, NO), \
	_(AUTHOR, FULL), \
	_(AUTHOR, ABBREVIATED), \
	_(AUTHOR, EMAIL), \
	_(AUTHOR, EMAIL_USER)

DEFINE_ENUM(author, AUTHOR_ENUM);

struct ident {
	const char *name;
	const char *email;
};

static const struct ident unknown_ident = { "Unknown", "unknown@localhost" };

static inline int
ident_compare(const struct ident *i1, const struct ident *i2)
{
	if (!i1 || !i2)
		return (!!i1) - (!!i2);
	if (!i1->name || !i2->name)
		return (!!i1->name) - (!!i2->name);
	return strcmp(i1->name, i2->name);
}

static const char *
get_author_initials(const char *author)
{
	static char initials[AUTHOR_WIDTH * 6 + 1];
	size_t pos = 0;
	const char *end = strchr(author, '\0');

#define is_initial_sep(c) (isspace(c) || ispunct(c) || (c) == '@' || (c) == '-')

	memset(initials, 0, sizeof(initials));
	while (author < end) {
		unsigned char bytes;
		size_t i;

		while (author < end && is_initial_sep(*author))
			author++;

		bytes = utf8_char_length(author, end);
		if (bytes >= sizeof(initials) - 1 - pos)
			break;
		while (bytes--) {
			initials[pos++] = *author++;
		}

		i = pos;
		while (author < end && !is_initial_sep(*author)) {
			bytes = utf8_char_length(author, end);
			if (bytes >= sizeof(initials) - 1 - i) {
				while (author < end && !is_initial_sep(*author))
					author++;
				break;
			}
			while (bytes--) {
				initials[i++] = *author++;
			}
		}

		initials[i++] = 0;
	}

	return initials;
}

static const char *
get_email_user(const char *email)
{
	static char user[AUTHOR_WIDTH * 6 + 1];
	const char *end = strchr(email, '@');
	int length = end ? end - email : strlen(email);

	string_format(user, "%.*s%c", length, email, 0);
	return user;
}

#define author_trim(cols) (cols == 0 || cols > 10)

static const char *
mkauthor(const struct ident *ident, int cols, enum author author)
{
	bool trim = author_trim(cols);
	bool abbreviate = author == AUTHOR_ABBREVIATED || !trim;

	if (author == AUTHOR_NO || !ident)
		return "";
	if (author == AUTHOR_EMAIL && ident->email)
		return ident->email;
	if (author == AUTHOR_EMAIL_USER && ident->email)
		return get_email_user(ident->email);
	if (abbreviate && ident->name)
		return get_author_initials(ident->name);
	return ident->name;
}

static const char *
mkmode(mode_t mode)
{
	if (S_ISDIR(mode))
		return "drwxr-xr-x";
	else if (S_ISLNK(mode))
		return "lrwxrwxrwx";
	else if (S_ISGITLINK(mode))
		return "m---------";
	else if (S_ISREG(mode) && mode & S_IXUSR)
		return "-rwxr-xr-x";
	else if (S_ISREG(mode))
		return "-rw-r--r--";
	else
		return "----------";
}

#define FILENAME_ENUM(_) \
	_(FILENAME, NO), \
	_(FILENAME, ALWAYS), \
	_(FILENAME, AUTO)

DEFINE_ENUM(filename, FILENAME_ENUM);

#define IGNORE_SPACE_ENUM(_) \
	_(IGNORE_SPACE, NO), \
	_(IGNORE_SPACE, ALL), \
	_(IGNORE_SPACE, SOME), \
	_(IGNORE_SPACE, AT_EOL)

DEFINE_ENUM(ignore_space, IGNORE_SPACE_ENUM);

#define COMMIT_ORDER_ENUM(_) \
	_(COMMIT_ORDER, DEFAULT), \
	_(COMMIT_ORDER, TOPO), \
	_(COMMIT_ORDER, DATE), \
	_(COMMIT_ORDER, REVERSE)

DEFINE_ENUM(commit_order, COMMIT_ORDER_ENUM);

#define VIEW_INFO(_) \
	_(MAIN,   main,   ref_head), \
	_(DIFF,   diff,   ref_commit), \
	_(LOG,    log,    ref_head), \
	_(TREE,   tree,   ref_commit), \
	_(BLOB,   blob,   ref_blob), \
	_(BLAME,  blame,  ref_commit), \
	_(BRANCH, branch, ref_head), \
	_(HELP,   help,   ""), \
	_(PAGER,  pager,  ""), \
	_(STATUS, status, "status"), \
	_(STAGE,  stage,  ref_status), \
	_(STASH,  stash,  ref_stash)

/*
 * User requests
 */

#define VIEW_REQ(id, name, ref) REQ_(VIEW_##id, "Show " #name " view")

#define REQ_INFO \
	REQ_GROUP("View switching") \
	VIEW_INFO(VIEW_REQ), \
	\
	REQ_GROUP("View manipulation") \
	REQ_(ENTER,		"Enter current line and scroll"), \
	REQ_(BACK,		"Go back to the previous view state"), \
	REQ_(NEXT,		"Move to next"), \
	REQ_(PREVIOUS,		"Move to previous"), \
	REQ_(PARENT,		"Move to parent"), \
	REQ_(VIEW_NEXT,		"Move focus to next view"), \
	REQ_(REFRESH,		"Reload and refresh"), \
	REQ_(MAXIMIZE,		"Maximize the current view"), \
	REQ_(VIEW_CLOSE,	"Close the current view"), \
	REQ_(QUIT,		"Close all views and quit"), \
	\
	REQ_GROUP("View specific requests") \
	REQ_(STATUS_UPDATE,	"Update file status"), \
	REQ_(STATUS_REVERT,	"Revert file changes"), \
	REQ_(STATUS_MERGE,	"Merge file using external tool"), \
	REQ_(STAGE_UPDATE_LINE,	"Update single line"), \
	REQ_(STAGE_NEXT,	"Find next chunk to stage"), \
	REQ_(STAGE_SPLIT_CHUNK,	"Split the current chunk"), \
	REQ_(DIFF_CONTEXT_DOWN,	"Decrease the diff context"), \
	REQ_(DIFF_CONTEXT_UP,	"Increase the diff context"), \
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
	REQ_(SCROLL_FIRST_COL,	"Scroll to the first line columns"), \
	REQ_(SCROLL_LEFT,	"Scroll two columns left"), \
	REQ_(SCROLL_RIGHT,	"Scroll two columns right"), \
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
	REQ_GROUP("Option manipulation") \
	REQ_(OPTIONS,		"Open option menu"), \
	REQ_(TOGGLE_LINENO,	"Toggle line numbers"), \
	REQ_(TOGGLE_DATE,	"Toggle date display"), \
	REQ_(TOGGLE_AUTHOR,	"Toggle author display"), \
	REQ_(TOGGLE_REV_GRAPH,	"Toggle revision graph visualization"), \
	REQ_(TOGGLE_GRAPHIC,	"Toggle (line) graphics mode"), \
	REQ_(TOGGLE_FILENAME,	"Toggle file name display"), \
	REQ_(TOGGLE_REFS,	"Toggle reference display (tags/branches)"), \
	REQ_(TOGGLE_CHANGES,	"Toggle local changes display in the main view"), \
	REQ_(TOGGLE_SORT_ORDER,	"Toggle ascending/descending sort order"), \
	REQ_(TOGGLE_SORT_FIELD,	"Toggle field to sort by"), \
	REQ_(TOGGLE_IGNORE_SPACE,	"Toggle ignoring whitespace in diffs"), \
	REQ_(TOGGLE_COMMIT_ORDER,	"Toggle commit ordering"), \
	REQ_(TOGGLE_ID,		"Toggle commit ID display"), \
	REQ_(TOGGLE_FILES,	"Toggle file filtering"), \
	REQ_(TOGGLE_TITLE_OVERFLOW,	"Toggle highlighting of commit title overflow"), \
	REQ_(TOGGLE_FILE_SIZE,	"Toggle file size format"), \
	REQ_(TOGGLE_UNTRACKED_DIRS,	"Toggle display of files in untracked directories"), \
	REQ_(TOGGLE_VERTICAL_SPLIT,	"Toggle vertical split"), \
	\
	REQ_GROUP("Misc") \
	REQ_(EDIT,		"Open in editor"), \
	REQ_(PROMPT,		"Bring up the prompt"), \
	REQ_(SCREEN_REDRAW,	"Redraw the screen"), \
	REQ_(SHOW_VERSION,	"Show version information"), \
	REQ_(STOP_LOADING,	"Stop all loading views"), \
	REQ_(NONE,		"Do nothing")


/* User action requests. */
enum request {
#define REQ_GROUP(help)
#define REQ_(req, help) REQ_##req

	/* Offset all requests to avoid conflicts with ncurses getch values. */
	REQ_UNKNOWN = KEY_MAX + 1,
	REQ_OFFSET,
	REQ_INFO,

	/* Internal requests. */
	REQ_JUMP_COMMIT,
	REQ_SCROLL_WHEEL_DOWN,
	REQ_SCROLL_WHEEL_UP,

	/* Start of the run request IDs */
	REQ_RUN_REQUESTS

#undef	REQ_GROUP
#undef	REQ_
};

struct request_info {
	enum request request;
	const char *name;
	int namelen;
	const char *help;
};

static const struct request_info req_info[] = {
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
		if (enum_equals(req_info[i], name, namelen))
			return req_info[i].request;

	return REQ_UNKNOWN;
}


/*
 * Options
 */

/* Option and state variables. */
static enum graphic opt_line_graphics	= GRAPHIC_DEFAULT;
static enum date opt_date		= DATE_DEFAULT;
static enum author opt_author		= AUTHOR_FULL;
static enum filename opt_filename	= FILENAME_AUTO;
static enum file_size opt_file_size	= FILE_SIZE_DEFAULT;
static bool opt_rev_graph		= TRUE;
static bool opt_line_number		= FALSE;
static bool opt_show_refs		= TRUE;
static bool opt_show_changes		= TRUE;
static bool opt_untracked_dirs_content	= TRUE;
static bool opt_read_git_colors		= TRUE;
static bool opt_wrap_lines		= FALSE;
static bool opt_ignore_case		= FALSE;
static bool opt_focus_child		= TRUE;
static int opt_diff_context		= 3;
static char opt_diff_context_arg[9]	= "";
static enum ignore_space opt_ignore_space	= IGNORE_SPACE_NO;
static char opt_ignore_space_arg[22]	= "";
static enum commit_order opt_commit_order	= COMMIT_ORDER_DEFAULT;
static char opt_commit_order_arg[22]	= "";
static bool opt_notes			= TRUE;
static char opt_notes_arg[SIZEOF_STR]	= "--show-notes";
static int opt_num_interval		= 5;
static double opt_hscroll		= 0.50;
static double opt_scale_split_view	= 2.0 / 3.0;
static double opt_scale_vsplit_view	= 0.5;
static enum vertical_split opt_vertical_split	= VERTICAL_SPLIT_AUTO;
static int opt_tab_size			= 8;
static int opt_author_width		= AUTHOR_WIDTH;
static int opt_filename_width		= FILENAME_WIDTH;
static char opt_path[SIZEOF_STR]	= "";
static char opt_file[SIZEOF_STR]	= "";
static char opt_ref[SIZEOF_REF]		= "";
static unsigned long opt_goto_line	= 0;
static char opt_head[SIZEOF_REF]	= "";
static char opt_remote[SIZEOF_REF]	= "";
static iconv_t opt_iconv_out		= ICONV_NONE;
static char opt_search[SIZEOF_STR]	= "";
static char opt_cdup[SIZEOF_STR]	= "";
static char opt_prefix[SIZEOF_STR]	= "";
static char opt_git_dir[SIZEOF_STR]	= "";
static signed char opt_is_inside_work_tree	= -1; /* set to TRUE or FALSE */
static char opt_editor[SIZEOF_STR]	= "";
static bool opt_editor_lineno		= TRUE;
static FILE *opt_tty			= NULL;
static const char **opt_cmdline_argv	= NULL;
static const char **opt_diff_argv	= NULL;
static const char **opt_rev_argv	= NULL;
static const char **opt_file_argv	= NULL;
static const char **opt_blame_argv	= NULL;
static int opt_lineno			= 0;
static bool opt_show_id			= FALSE;
static int opt_id_cols			= ID_WIDTH;
static bool opt_file_filter		= TRUE;
static bool opt_show_title_overflow	= FALSE;
static int opt_title_overflow		= 50;
static char opt_env_lines[64]		= "";
static char opt_env_columns[64]		= "";
static char *opt_env[]			= { opt_env_lines, opt_env_columns, NULL };
static bool opt_mouse			= FALSE;
static int opt_scroll_wheel_lines	= 3;

#define is_initial_commit()	(!get_ref_head())
#define is_head_commit(rev)	(!strcmp((rev), "HEAD") || (get_ref_head() && !strncmp(rev, get_ref_head()->id, SIZEOF_REV - 1)))

static bool
vertical_split_is_enabled(void)
{
	if (opt_vertical_split == VERTICAL_SPLIT_AUTO) {
		int height, width;

		getmaxyx(stdscr, height, width);
		return width * opt_scale_vsplit_view > (height - 1) * 2;
	}

	return opt_vertical_split == VERTICAL_SPLIT_VERTICAL;
}

static inline int
load_refs(bool force)
{
	static bool loaded = FALSE;

	if (force)
		opt_head[0] = 0;
	else if (loaded)
		return OK;

	loaded = TRUE;
	return reload_refs(opt_git_dir, opt_remote, opt_head, sizeof(opt_head));
}

static inline void
update_diff_context_arg(int diff_context)
{
	if (!string_format(opt_diff_context_arg, "-U%u", diff_context))
		string_ncopy(opt_diff_context_arg, "-U3", 3);
}

#define ENUM_ARG(enum_name, arg_string) ENUM_MAP_ENTRY(arg_string, enum_name)

static const struct enum_map_entry ignore_space_arg_map[] = {
	ENUM_ARG(IGNORE_SPACE_NO,	""),
	ENUM_ARG(IGNORE_SPACE_ALL,	"--ignore-all-space"),
	ENUM_ARG(IGNORE_SPACE_SOME,	"--ignore-space-change"),
	ENUM_ARG(IGNORE_SPACE_AT_EOL,	"--ignore-space-at-eol"),
};

static inline void
update_ignore_space_arg()
{
	enum_copy_name(opt_ignore_space_arg, ignore_space_arg_map[opt_ignore_space]);
}

static const struct enum_map_entry commit_order_arg_map[] = {
	ENUM_ARG(COMMIT_ORDER_DEFAULT,	""),
	ENUM_ARG(COMMIT_ORDER_TOPO,	"--topo-order"),
	ENUM_ARG(COMMIT_ORDER_DATE,	"--date-order"),
	ENUM_ARG(COMMIT_ORDER_REVERSE,	"--reverse"),
};

static inline void
update_commit_order_arg()
{
	enum_copy_name(opt_commit_order_arg, commit_order_arg_map[opt_commit_order]);
}

static inline void
update_notes_arg()
{
	if (opt_notes) {
		string_copy(opt_notes_arg, "--show-notes");
	} else {
		/* Notes are disabled by default when passing --pretty args. */
		string_copy(opt_notes_arg, "");
	}
}

/*
 * Line-oriented content detection.
 */

#define LINE_INFO \
LINE(DIFF_HEADER,  "diff --",	COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(DIFF_CHUNK,   "@@",		COLOR_MAGENTA,	COLOR_DEFAULT,	0), \
LINE(DIFF_ADD,	   "+",			COLOR_GREEN,	COLOR_DEFAULT,	0), \
LINE(DIFF_ADD2,	   " +",		COLOR_GREEN,	COLOR_DEFAULT,	0), \
LINE(DIFF_DEL,	   "-",			COLOR_RED,	COLOR_DEFAULT,	0), \
LINE(DIFF_DEL2,	   " -",		COLOR_RED,	COLOR_DEFAULT,	0), \
LINE(DIFF_INDEX,	"index ",	  COLOR_BLUE,	COLOR_DEFAULT,	0), \
LINE(DIFF_OLDMODE,	"old file mode ", COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(DIFF_NEWMODE,	"new file mode ", COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(DIFF_DELETED_FILE_MODE, \
		    "deleted file mode ", COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(DIFF_COPY_FROM,	"copy from ",	  COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(DIFF_COPY_TO,	"copy to ",	  COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(DIFF_RENAME_FROM,	"rename from ",	  COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(DIFF_RENAME_TO,	"rename to ",	  COLOR_YELLOW,	COLOR_DEFAULT,	0), \
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
LINE(PP_REFLOG,	   "Reflog: ",		COLOR_RED,	COLOR_DEFAULT,	0), \
LINE(PP_REFLOGMSG, "Reflog message: ",	COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(STASH,	   "stash@{",		COLOR_MAGENTA,	COLOR_DEFAULT,	0), \
LINE(COMMIT,	   "commit ",		COLOR_GREEN,	COLOR_DEFAULT,	0), \
LINE(PARENT,	   "parent ",		COLOR_BLUE,	COLOR_DEFAULT,	0), \
LINE(TREE,	   "tree ",		COLOR_BLUE,	COLOR_DEFAULT,	0), \
LINE(AUTHOR,	   "author ",		COLOR_GREEN,	COLOR_DEFAULT,	0), \
LINE(COMMITTER,	   "committer ",	COLOR_MAGENTA,	COLOR_DEFAULT,	0), \
LINE(SIGNOFF,	   "    Signed-off-by", COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(ACKED,	   "    Acked-by",	COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(TESTED,	   "    Tested-by",	COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(REVIEWED,	   "    Reviewed-by",	COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(DEFAULT,	   "",			COLOR_DEFAULT,	COLOR_DEFAULT,	A_NORMAL), \
LINE(CURSOR,	   "",			COLOR_WHITE,	COLOR_GREEN,	A_BOLD), \
LINE(STATUS,	   "",			COLOR_GREEN,	COLOR_DEFAULT,	0), \
LINE(DELIMITER,	   "",			COLOR_MAGENTA,	COLOR_DEFAULT,	0), \
LINE(DATE,         "",			COLOR_BLUE,	COLOR_DEFAULT,	0), \
LINE(MODE,         "",			COLOR_CYAN,	COLOR_DEFAULT,	0), \
LINE(ID,	   "",			COLOR_MAGENTA,	COLOR_DEFAULT,	0), \
LINE(OVERFLOW,	   "",			COLOR_RED,	COLOR_DEFAULT,	0), \
LINE(FILENAME,     "",			COLOR_DEFAULT,	COLOR_DEFAULT,	0), \
LINE(FILE_SIZE,    "",			COLOR_DEFAULT,	COLOR_DEFAULT,	0), \
LINE(LINE_NUMBER,  "",			COLOR_CYAN,	COLOR_DEFAULT,	0), \
LINE(TITLE_BLUR,   "",			COLOR_WHITE,	COLOR_BLUE,	0), \
LINE(TITLE_FOCUS,  "",			COLOR_WHITE,	COLOR_BLUE,	A_BOLD), \
LINE(MAIN_COMMIT,  "",			COLOR_DEFAULT,	COLOR_DEFAULT,	0), \
LINE(MAIN_TAG,     "",			COLOR_MAGENTA,	COLOR_DEFAULT,	A_BOLD), \
LINE(MAIN_LOCAL_TAG,"",			COLOR_MAGENTA,	COLOR_DEFAULT,	0), \
LINE(MAIN_REMOTE,  "",			COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(MAIN_REPLACE, "",			COLOR_CYAN,	COLOR_DEFAULT,	0), \
LINE(MAIN_TRACKED, "",			COLOR_YELLOW,	COLOR_DEFAULT,	A_BOLD), \
LINE(MAIN_REF,     "",			COLOR_CYAN,	COLOR_DEFAULT,	0), \
LINE(MAIN_HEAD,    "",			COLOR_CYAN,	COLOR_DEFAULT,	A_BOLD), \
LINE(MAIN_REVGRAPH,"",			COLOR_MAGENTA,	COLOR_DEFAULT,	0), \
LINE(TREE_HEAD,    "",			COLOR_DEFAULT,	COLOR_DEFAULT,	A_BOLD), \
LINE(TREE_DIR,     "",			COLOR_YELLOW,	COLOR_DEFAULT,	A_NORMAL), \
LINE(TREE_FILE,    "",			COLOR_DEFAULT,	COLOR_DEFAULT,	A_NORMAL), \
LINE(STAT_HEAD,    "",			COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(STAT_SECTION, "",			COLOR_CYAN,	COLOR_DEFAULT,	0), \
LINE(STAT_NONE,    "",			COLOR_DEFAULT,	COLOR_DEFAULT,	0), \
LINE(STAT_STAGED,  "",			COLOR_MAGENTA,	COLOR_DEFAULT,	0), \
LINE(STAT_UNSTAGED,"",			COLOR_MAGENTA,	COLOR_DEFAULT,	0), \
LINE(STAT_UNTRACKED,"",			COLOR_MAGENTA,	COLOR_DEFAULT,	0), \
LINE(HELP_KEYMAP,  "",			COLOR_CYAN,	COLOR_DEFAULT,	0), \
LINE(HELP_GROUP,   "",			COLOR_BLUE,	COLOR_DEFAULT,	0), \
LINE(DIFF_STAT,		"",	  	COLOR_BLUE,	COLOR_DEFAULT,	0), \
LINE(PALETTE_0, "",			COLOR_MAGENTA,	COLOR_DEFAULT,	0), \
LINE(PALETTE_1, "",			COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(PALETTE_2, "",			COLOR_CYAN,	COLOR_DEFAULT,	0), \
LINE(PALETTE_3, "",			COLOR_GREEN,	COLOR_DEFAULT,	0), \
LINE(PALETTE_4, "",			COLOR_DEFAULT,	COLOR_DEFAULT,	0), \
LINE(PALETTE_5, "",			COLOR_WHITE,	COLOR_DEFAULT,	0), \
LINE(PALETTE_6, "",			COLOR_RED,	COLOR_DEFAULT,	0), \
LINE(GRAPH_COMMIT, "",			COLOR_BLUE,	COLOR_DEFAULT,	0)

enum line_type {
#define LINE(type, line, fg, bg, attr) \
	LINE_##type
	LINE_INFO,
	LINE_NONE
#undef	LINE
};

struct line_info {
	const char *name;	/* Option name. */
	int namelen;		/* Size of option name. */
	const char *line;	/* The start of line to match. */
	int linelen;		/* Size of string to match. */
	int fg, bg, attr;	/* Color and text attributes for the lines. */
	int color_pair;
};

static struct line_info line_info[] = {
#define LINE(type, line, fg, bg, attr) \
	{ #type, STRING_SIZE(#type), (line), STRING_SIZE(line), (fg), (bg), (attr) }
	LINE_INFO
#undef	LINE
};

static struct line_info **color_pair;
static size_t color_pairs;

static struct line_info *custom_color;
static size_t custom_colors;

DEFINE_ALLOCATOR(realloc_custom_color, struct line_info, 8)
DEFINE_ALLOCATOR(realloc_color_pair, struct line_info *, 8)

#define TO_CUSTOM_COLOR_TYPE(type)	(LINE_NONE + 1 + (type))
#define TO_CUSTOM_COLOR_OFFSET(type)	((type) - LINE_NONE - 1)

/* Color IDs must be 1 or higher. [GH #15] */
#define COLOR_ID(line_type)		((line_type) + 1)

static enum line_type
get_line_type(const char *line)
{
	int linelen = strlen(line);
	enum line_type type;

	for (type = 0; type < custom_colors; type++)
		/* Case insensitive search matches Signed-off-by lines better. */
		if (linelen >= custom_color[type].linelen &&
		    !strncasecmp(custom_color[type].line, line, custom_color[type].linelen))
			return TO_CUSTOM_COLOR_TYPE(type);

	for (type = 0; type < ARRAY_SIZE(line_info); type++)
		/* Case insensitive search matches Signed-off-by lines better. */
		if (linelen >= line_info[type].linelen &&
		    !strncasecmp(line_info[type].line, line, line_info[type].linelen))
			return type;

	return LINE_DEFAULT;
}

static enum line_type
get_line_type_from_ref(const struct ref *ref)
{
	if (ref->head)
		return LINE_MAIN_HEAD;
	else if (ref->ltag)
		return LINE_MAIN_LOCAL_TAG;
	else if (ref->tag)
		return LINE_MAIN_TAG;
	else if (ref->tracked)
		return LINE_MAIN_TRACKED;
	else if (ref->remote)
		return LINE_MAIN_REMOTE;
	else if (ref->replace)
		return LINE_MAIN_REPLACE;

	return LINE_MAIN_REF;
}

static inline struct line_info *
get_line(enum line_type type)
{
	if (type > LINE_NONE) {
		assert(TO_CUSTOM_COLOR_OFFSET(type) < custom_colors);
		return &custom_color[TO_CUSTOM_COLOR_OFFSET(type)];
	} else {
		assert(type < ARRAY_SIZE(line_info));
		return &line_info[type];
	}
}

static inline int
get_line_color(enum line_type type)
{
	return COLOR_ID(get_line(type)->color_pair);
}

static inline int
get_line_attr(enum line_type type)
{
	struct line_info *info = get_line(type);

	return COLOR_PAIR(COLOR_ID(info->color_pair)) | info->attr;
}

static struct line_info *
get_line_info(const char *name)
{
	size_t namelen = strlen(name);
	enum line_type type;

	for (type = 0; type < ARRAY_SIZE(line_info); type++)
		if (enum_equals(line_info[type], name, namelen))
			return &line_info[type];

	return NULL;
}

static struct line_info *
add_custom_color(const char *quoted_line)
{
	struct line_info *info;
	char *line;
	size_t linelen;

	if (!realloc_custom_color(&custom_color, custom_colors, 1))
		die("Failed to alloc custom line info");

	linelen = strlen(quoted_line) - 1;
	line = malloc(linelen);
	if (!line)
		return NULL;

	strncpy(line, quoted_line + 1, linelen);
	line[linelen - 1] = 0;

	info = &custom_color[custom_colors++];
	info->name = info->line = line;
	info->namelen = info->linelen = strlen(line);

	return info;
}

static void
init_line_info_color_pair(struct line_info *info, enum line_type type,
	int default_bg, int default_fg)
{
	int bg = info->bg == COLOR_DEFAULT ? default_bg : info->bg;
	int fg = info->fg == COLOR_DEFAULT ? default_fg : info->fg;
	int i;

	for (i = 0; i < color_pairs; i++) {
		if (color_pair[i]->fg == info->fg && color_pair[i]->bg == info->bg) {
			info->color_pair = i;
			return;
		}
	}

	if (!realloc_color_pair(&color_pair, color_pairs, 1))
		die("Failed to alloc color pair");

	color_pair[color_pairs] = info;
	info->color_pair = color_pairs++;
	init_pair(COLOR_ID(info->color_pair), fg, bg);
}

static void
init_colors(void)
{
	int default_bg = line_info[LINE_DEFAULT].bg;
	int default_fg = line_info[LINE_DEFAULT].fg;
	enum line_type type;

	start_color();

	if (assume_default_colors(default_fg, default_bg) == ERR) {
		default_bg = COLOR_BLACK;
		default_fg = COLOR_WHITE;
	}

	for (type = 0; type < ARRAY_SIZE(line_info); type++) {
		struct line_info *info = &line_info[type];

		init_line_info_color_pair(info, type, default_bg, default_fg);
	}

	for (type = 0; type < custom_colors; type++) {
		struct line_info *info = &custom_color[type];

		init_line_info_color_pair(info, TO_CUSTOM_COLOR_TYPE(type),
					  default_bg, default_fg);
	}
}

struct line {
	enum line_type type;
	unsigned int lineno:24;

	/* State flags */
	unsigned int selected:1;
	unsigned int dirty:1;
	unsigned int cleareol:1;
	unsigned int wrapped:1;

	unsigned int user_flags:6;
	void *data;		/* User data */
};


/*
 * Keys
 */

struct keybinding {
	int alias;
	enum request request;
};

struct keymap {
	const char *name;
	struct keymap *next;
	struct keybinding *data;
	size_t size;
	bool hidden;
};

static struct keymap generic_keymap = { "generic" };
#define is_generic_keymap(keymap) ((keymap) == &generic_keymap)

static struct keymap *keymaps = &generic_keymap;

static void
add_keymap(struct keymap *keymap)
{
	keymap->next = keymaps;
	keymaps = keymap;
}

static struct keymap *
get_keymap(const char *name)
{
	struct keymap *keymap = keymaps;

	while (keymap) {
		if (!strcasecmp(keymap->name, name))
			return keymap;
		keymap = keymap->next;
	}

	return NULL;
}


static void
add_keybinding(struct keymap *table, enum request request, int key)
{
	size_t i;

	for (i = 0; i < table->size; i++) {
		if (table->data[i].alias == key) {
			table->data[i].request = request;
			return;
		}
	}

	table->data = realloc(table->data, (table->size + 1) * sizeof(*table->data));
	if (!table->data)
		die("Failed to allocate keybinding");
	table->data[table->size].alias = key;
	table->data[table->size++].request = request;
}

/* Looks for a key binding first in the given map, then in the generic map, and
 * lastly in the default keybindings. */
static enum request
get_keybinding(struct keymap *keymap, int key)
{
	size_t i;

	for (i = 0; i < keymap->size; i++)
		if (keymap->data[i].alias == key)
			return keymap->data[i].request;

	for (i = 0; i < generic_keymap.size; i++)
		if (generic_keymap.data[i].alias == key)
			return generic_keymap.data[i].request;

	return (enum request) key;
}


struct key {
	const char *name;
	int value;
};

static const struct key key_table[] = {
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
	{ "PgUp",	KEY_PPAGE },
	{ "PageDown",	KEY_NPAGE },
	{ "PgDown",	KEY_NPAGE },
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

	if (strlen(name) == 3 && name[0] == '^' && name[1] == '[' && isprint(*name))
		return (int)name[2] + 0x80;
	if (strlen(name) == 2 && name[0] == '^' && isprint(*name))
		return (int)name[1] & 0x1f;
	if (strlen(name) == 1 && isprint(*name))
		return (int) *name;
	return ERR;
}

static const char *
get_key_name(int key_value)
{
	static char key_char[] = "'X'\0";
	const char *seq = NULL;
	int key;

	for (key = 0; key < ARRAY_SIZE(key_table); key++)
		if (key_table[key].value == key_value)
			seq = key_table[key].name;

	if (seq == NULL && key_value < 0x7f) {
		char *s = key_char + 1;

		if (key_value >= 0x20) {
			*s++ = key_value;
		} else {
			*s++ = '^';
			*s++ = 0x40 | (key_value & 0x1f);
		}
		*s++ = '\'';
		*s++ = '\0';
		seq = key_char;
	}

	return seq ? seq : "(no key)";
}

static bool
append_key(char *buf, size_t *pos, const struct keybinding *keybinding)
{
	const char *sep = *pos > 0 ? ", " : "";
	const char *keyname = get_key_name(keybinding->alias);

	return string_nformat(buf, BUFSIZ, pos, "%s%s", sep, keyname);
}

static bool
append_keymap_request_keys(char *buf, size_t *pos, enum request request,
			   struct keymap *keymap, bool all)
{
	int i;

	for (i = 0; i < keymap->size; i++) {
		if (keymap->data[i].request == request) {
			if (!append_key(buf, pos, &keymap->data[i]))
				return FALSE;
			if (!all)
				break;
		}
	}

	return TRUE;
}

#define get_view_key(view, request) get_keys(&(view)->ops->keymap, request, FALSE)

static const char *
get_keys(struct keymap *keymap, enum request request, bool all)
{
	static char buf[BUFSIZ];
	size_t pos = 0;

	buf[pos] = 0;

	if (!append_keymap_request_keys(buf, &pos, request, keymap, all))
		return "Too many keybindings!";
	if (pos > 0 && !all)
		return buf;

	if (!is_generic_keymap(keymap)) {
		/* Only the generic keymap includes the default keybindings when
		 * listing all keys. */
		if (all)
			return buf;

		if (!append_keymap_request_keys(buf, &pos, request, &generic_keymap, all))
			return "Too many keybindings!";
		if (pos)
			return buf;
	}

	return buf;
}

enum run_request_flag {
	RUN_REQUEST_DEFAULT	= 0,
	RUN_REQUEST_FORCE	= 1,
	RUN_REQUEST_SILENT	= 2,
	RUN_REQUEST_CONFIRM	= 4,
	RUN_REQUEST_EXIT	= 8,
	RUN_REQUEST_INTERNAL	= 16,
};

struct run_request {
	struct keymap *keymap;
	int key;
	const char **argv;
	bool silent;
	bool confirm;
	bool exit;
	bool internal;
};

static struct run_request *run_request;
static size_t run_requests;

DEFINE_ALLOCATOR(realloc_run_requests, struct run_request, 8)

static bool
add_run_request(struct keymap *keymap, int key, const char **argv, enum run_request_flag flags)
{
	bool force = flags & RUN_REQUEST_FORCE;
	struct run_request *req;

	if (!force && get_keybinding(keymap, key) != key)
		return TRUE;

	if (!realloc_run_requests(&run_request, run_requests, 1))
		return FALSE;

	if (!argv_copy(&run_request[run_requests].argv, argv))
		return FALSE;

	req = &run_request[run_requests++];
	req->silent = flags & RUN_REQUEST_SILENT;
	req->confirm = flags & RUN_REQUEST_CONFIRM;
	req->exit = flags & RUN_REQUEST_EXIT;
	req->internal = flags & RUN_REQUEST_INTERNAL;
	req->keymap = keymap;
	req->key = key;

	add_keybinding(keymap, REQ_RUN_REQUESTS + run_requests, key);
	return TRUE;
}

static struct run_request *
get_run_request(enum request request)
{
	if (request <= REQ_RUN_REQUESTS || request > REQ_RUN_REQUESTS + run_requests)
		return NULL;
	return &run_request[request - REQ_RUN_REQUESTS - 1];
}

/*
 * User config file handling.
 */

static const struct enum_map_entry color_map[] = {
#define COLOR_MAP(name) ENUM_MAP_ENTRY(#name, COLOR_##name)
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

static const struct enum_map_entry attr_map[] = {
#define ATTR_MAP(name) ENUM_MAP_ENTRY(#name, A_##name)
	ATTR_MAP(NORMAL),
	ATTR_MAP(BLINK),
	ATTR_MAP(BOLD),
	ATTR_MAP(DIM),
	ATTR_MAP(REVERSE),
	ATTR_MAP(STANDOUT),
	ATTR_MAP(UNDERLINE),
};

#define set_attribute(attr, name)	map_enum(attr, attr_map, name)

static enum status_code
parse_step(double *opt, const char *arg)
{
	*opt = atoi(arg);
	if (!strchr(arg, '%'))
		return SUCCESS;

	/* "Shift down" so 100% and 1 does not conflict. */
	*opt = (*opt - 1) / 100;
	if (*opt >= 1.0) {
		*opt = 0.99;
		return ERROR_INVALID_STEP_VALUE;
	}
	if (*opt < 0.0) {
		*opt = 1;
		return ERROR_INVALID_STEP_VALUE;
	}
	return SUCCESS;
}

static enum status_code
parse_int(int *opt, const char *arg, int min, int max)
{
	int value = atoi(arg);

	if (min <= value && value <= max) {
		*opt = value;
		return SUCCESS;
	}

	return ERROR_INTEGER_VALUE_OUT_OF_BOUND;
}

#define parse_id(opt, arg) \
	parse_int(opt, arg, 4, SIZEOF_REV - 1)

static bool
set_color(int *color, const char *name)
{
	if (map_enum(color, color_map, name))
		return TRUE;
	if (!prefixcmp(name, "color"))
		return parse_int(color, name + 5, 0, 255) == SUCCESS;
	/* Used when reading git colors. Git expects a plain int w/o prefix.  */
	return parse_int(color, name, 0, 255) == SUCCESS;
}

/* Wants: object fgcolor bgcolor [attribute] */
static enum status_code
option_color_command(int argc, const char *argv[])
{
	struct line_info *info;

	if (argc < 3)
		return ERROR_WRONG_NUMBER_OF_ARGUMENTS;

	if (*argv[0] == '"' || *argv[0] == '\'') {
		info = add_custom_color(argv[0]);
	} else {
		info = get_line_info(argv[0]);
	}
	if (!info) {
		static const struct enum_map_entry obsolete[] = {
			ENUM_MAP_ENTRY("main-delim",	LINE_DELIMITER),
			ENUM_MAP_ENTRY("main-date",	LINE_DATE),
			ENUM_MAP_ENTRY("main-author",	LINE_AUTHOR),
			ENUM_MAP_ENTRY("blame-id",	LINE_ID),
		};
		int index;

		if (!map_enum(&index, obsolete, argv[0]))
			return ERROR_UNKNOWN_COLOR_NAME;
		info = &line_info[index];
	}

	if (!set_color(&info->fg, argv[1]) ||
	    !set_color(&info->bg, argv[2]))
		return ERROR_UNKNOWN_COLOR;

	info->attr = 0;
	while (argc-- > 3) {
		int attr;

		if (!set_attribute(&attr, argv[argc]))
			return ERROR_UNKNOWN_ATTRIBUTE;
		info->attr |= attr;
	}

	return SUCCESS;
}

static enum status_code
parse_bool_matched(bool *opt, const char *arg, bool *matched)
{
	*opt = (!strcmp(arg, "1") || !strcmp(arg, "true") || !strcmp(arg, "yes"))
		? TRUE : FALSE;
	if (matched)
		*matched = *opt || (!strcmp(arg, "0") || !strcmp(arg, "false") || !strcmp(arg, "no"));
	return SUCCESS;
}

#define parse_bool(opt, arg) parse_bool_matched(opt, arg, NULL)

static enum status_code
parse_enum(unsigned int *opt, const char *arg, const struct enum_map *map)
{
	bool is_true;

	assert(map->size > 1);

	if (map_enum_do(map->entries, map->size, (int *) opt, arg))
		return SUCCESS;

	parse_bool(&is_true, arg);
	*opt = is_true ? map->entries[1].value : map->entries[0].value;
	return SUCCESS;
}

static enum status_code
parse_string(char *opt, const char *arg, size_t optsize)
{
	int arglen = strlen(arg);

	switch (arg[0]) {
	case '\"':
	case '\'':
		if (arglen == 1 || arg[arglen - 1] != arg[0])
			return ERROR_UNMATCHED_QUOTATION;
		arg += 1; arglen -= 2;
	default:
		string_ncopy_do(opt, optsize, arg, arglen);
		return SUCCESS;
	}
}

static enum status_code
parse_encoding(struct encoding **encoding_ref, const char *arg, bool priority)
{
	char buf[SIZEOF_STR];
	enum status_code code = parse_string(buf, arg, sizeof(buf));

	if (code == SUCCESS) {
		struct encoding *encoding = *encoding_ref;

		if (encoding && !priority)
			return code;
		encoding = encoding_open(buf);
		if (encoding)
			*encoding_ref = encoding;
	}

	return code;
}

static enum status_code
parse_args(const char ***args, const char *argv[])
{
	if (!argv_copy(args, argv))
		return ERROR_OUT_OF_MEMORY;
	return SUCCESS;
}

/* Wants: name = value */
static enum status_code
option_set_command(int argc, const char *argv[])
{
	if (argc < 3)
		return ERROR_WRONG_NUMBER_OF_ARGUMENTS;

	if (strcmp(argv[1], "="))
		return ERROR_NO_VALUE_ASSIGNED;

	if (!strcmp(argv[0], "blame-options"))
		return parse_args(&opt_blame_argv, argv + 2);

	if (!strcmp(argv[0], "diff-options"))
		return parse_args(&opt_diff_argv, argv + 2);

	if (argc != 3)
		return ERROR_WRONG_NUMBER_OF_ARGUMENTS;

	if (!strcmp(argv[0], "show-author"))
		return parse_enum(&opt_author, argv[2], author_map);

	if (!strcmp(argv[0], "show-date"))
		return parse_enum(&opt_date, argv[2], date_map);

	if (!strcmp(argv[0], "show-rev-graph"))
		return parse_bool(&opt_rev_graph, argv[2]);

	if (!strcmp(argv[0], "show-refs"))
		return parse_bool(&opt_show_refs, argv[2]);

	if (!strcmp(argv[0], "show-changes"))
		return parse_bool(&opt_show_changes, argv[2]);

	if (!strcmp(argv[0], "show-notes")) {
		bool matched = FALSE;
		enum status_code res = parse_bool_matched(&opt_notes, argv[2], &matched);

		if (res == SUCCESS && matched) {
			update_notes_arg();
			return res;
		}

		opt_notes = TRUE;
		strcpy(opt_notes_arg, "--show-notes=");
		res = parse_string(opt_notes_arg + 8, argv[2],
				   sizeof(opt_notes_arg) - 8);
		if (res == SUCCESS && opt_notes_arg[8] == '\0')
			opt_notes_arg[7] = '\0';
		return res;
	}

	if (!strcmp(argv[0], "show-line-numbers"))
		return parse_bool(&opt_line_number, argv[2]);

	if (!strcmp(argv[0], "line-graphics"))
		return parse_enum(&opt_line_graphics, argv[2], graphic_map);

	if (!strcmp(argv[0], "line-number-interval"))
		return parse_int(&opt_num_interval, argv[2], 1, 1024);

	if (!strcmp(argv[0], "author-width"))
		return parse_int(&opt_author_width, argv[2], 0, 1024);

	if (!strcmp(argv[0], "filename-width"))
		return parse_int(&opt_filename_width, argv[2], 0, 1024);

	if (!strcmp(argv[0], "show-filename"))
		return parse_enum(&opt_filename, argv[2], filename_map);

	if (!strcmp(argv[0], "show-file-size"))
		return parse_enum(&opt_file_size, argv[2], file_size_map);

	if (!strcmp(argv[0], "horizontal-scroll"))
		return parse_step(&opt_hscroll, argv[2]);

	if (!strcmp(argv[0], "split-view-height"))
		return parse_step(&opt_scale_split_view, argv[2]);

	if (!strcmp(argv[0], "vertical-split"))
		return parse_enum(&opt_vertical_split, argv[2], vertical_split_map);

	if (!strcmp(argv[0], "tab-size"))
		return parse_int(&opt_tab_size, argv[2], 1, 1024);

	if (!strcmp(argv[0], "diff-context") && !*opt_diff_context_arg) {
		enum status_code code = parse_int(&opt_diff_context, argv[2], 0, 999999);

		if (code == SUCCESS)
			update_diff_context_arg(opt_diff_context);
		return code;
	}

	if (!strcmp(argv[0], "ignore-space") && !*opt_ignore_space_arg) {
		enum status_code code = parse_enum(&opt_ignore_space, argv[2], ignore_space_map);

		if (code == SUCCESS)
			update_ignore_space_arg();
		return code;
	}

	if (!strcmp(argv[0], "commit-order") && !*opt_commit_order_arg) {
		enum status_code code = parse_enum(&opt_commit_order, argv[2], commit_order_map);

		if (code == SUCCESS)
			update_commit_order_arg();
		return code;
	}

	if (!strcmp(argv[0], "status-untracked-dirs"))
		return parse_bool(&opt_untracked_dirs_content, argv[2]);

	if (!strcmp(argv[0], "read-git-colors"))
		return parse_bool(&opt_read_git_colors, argv[2]);

	if (!strcmp(argv[0], "ignore-case"))
		return parse_bool(&opt_ignore_case, argv[2]);

	if (!strcmp(argv[0], "focus-child"))
		return parse_bool(&opt_focus_child, argv[2]);

	if (!strcmp(argv[0], "wrap-lines"))
		return parse_bool(&opt_wrap_lines, argv[2]);

	if (!strcmp(argv[0], "show-id"))
		return parse_bool(&opt_show_id, argv[2]);

	if (!strcmp(argv[0], "id-width"))
		return parse_id(&opt_id_cols, argv[2]);

	if (!strcmp(argv[0], "title-overflow")) {
		bool matched;
		enum status_code code;

		/*
		 * "title-overflow" is considered a boolint.
		 * We try to parse it as a boolean (and set the value to 50 if true),
		 * otherwise we parse it as an integer and use the given value.
		 */
		code = parse_bool_matched(&opt_show_title_overflow, argv[2], &matched);
		if (code == SUCCESS && matched) {
			if (opt_show_title_overflow)
				opt_title_overflow = 50;
		} else {
			code = parse_int(&opt_title_overflow, argv[2], 2, 1024);
			if (code == SUCCESS)
				opt_show_title_overflow = TRUE;
		}

		return code;
	}

	if (!strcmp(argv[0], "editor-line-number"))
		return parse_bool(&opt_editor_lineno, argv[2]);

	if (!strcmp(argv[0], "mouse"))
		return parse_bool(&opt_mouse, argv[2]);

	if (!strcmp(argv[0], "mouse-scroll"))
		return parse_int(&opt_scroll_wheel_lines, argv[2], 0, 1024);

	return ERROR_UNKNOWN_VARIABLE_NAME;
}

/* Wants: mode request key */
static enum status_code
option_bind_command(int argc, const char *argv[])
{
	enum request request;
	struct keymap *keymap;
	int key;

	if (argc < 3)
		return ERROR_WRONG_NUMBER_OF_ARGUMENTS;

	if (!(keymap = get_keymap(argv[0])))
		return ERROR_UNKNOWN_KEY_MAP;

	key = get_key_value(argv[1]);
	if (key == ERR)
		return ERROR_UNKNOWN_KEY;

	request = get_request(argv[2]);
	if (request == REQ_UNKNOWN) {
		static const struct enum_map_entry obsolete[] = {
			ENUM_MAP_ENTRY("cherry-pick",		REQ_NONE),
			ENUM_MAP_ENTRY("screen-resize",	REQ_NONE),
			ENUM_MAP_ENTRY("tree-parent",		REQ_PARENT),
		};
		int alias;

		if (map_enum(&alias, obsolete, argv[2])) {
			if (alias != REQ_NONE)
				add_keybinding(keymap, alias, key);
			return ERROR_OBSOLETE_REQUEST_NAME;
		}
	}

	if (request == REQ_UNKNOWN) {
		enum run_request_flag flags = RUN_REQUEST_FORCE;

		if (strchr("!?@<", *argv[2])) {
			while (*argv[2]) {
				if (*argv[2] == '@') {
					flags |= RUN_REQUEST_SILENT;
				} else if (*argv[2] == '?') {
					flags |= RUN_REQUEST_CONFIRM;
				} else if (*argv[2] == '<') {
					flags |= RUN_REQUEST_EXIT;
				} else if (*argv[2] != '!') {
					break;
				}
				argv[2]++;
			}

		} else if (*argv[2] == ':') {
			argv[2]++;
			flags |= RUN_REQUEST_INTERNAL;

		} else {
			return ERROR_UNKNOWN_REQUEST_NAME;
		}

		return add_run_request(keymap, key, argv + 2, flags)
			? SUCCESS : ERROR_OUT_OF_MEMORY;
	}

	add_keybinding(keymap, request, key);

	return SUCCESS;
}


static enum status_code load_option_file(const char *path);

static enum status_code
option_source_command(int argc, const char *argv[])
{
	if (argc < 1)
		return ERROR_WRONG_NUMBER_OF_ARGUMENTS;

	return load_option_file(argv[0]);
}

static enum status_code
set_option(const char *opt, char *value)
{
	const char *argv[SIZEOF_ARG];
	int argc = 0;

	if (!argv_from_string(argv, &argc, value))
		return ERROR_TOO_MANY_OPTION_ARGUMENTS;

	if (!strcmp(opt, "color"))
		return option_color_command(argc, argv);

	if (!strcmp(opt, "set"))
		return option_set_command(argc, argv);

	if (!strcmp(opt, "bind"))
		return option_bind_command(argc, argv);

	if (!strcmp(opt, "source"))
		return option_source_command(argc, argv);

	return ERROR_UNKNOWN_OPTION_COMMAND;
}

struct config_state {
	const char *path;
	int lineno;
	bool errors;
};

static int
read_option(char *opt, size_t optlen, char *value, size_t valuelen, void *data)
{
	struct config_state *config = data;
	enum status_code status = ERROR_NO_OPTION_VALUE;

	config->lineno++;

	/* Check for comment markers, since read_properties() will
	 * only ensure opt and value are split at first " \t". */
	optlen = strcspn(opt, "#");
	if (optlen == 0)
		return OK;

	if (opt[optlen] == 0) {
		/* Look for comment endings in the value. */
		size_t len = strcspn(value, "#");

		if (len < valuelen) {
			valuelen = len;
			value[valuelen] = 0;
		}

		status = set_option(opt, value);
	}

	if (status != SUCCESS) {
		warn("%s line %d: %s near '%.*s'", config->path, config->lineno,
		     get_status_message(status), (int) optlen, opt);
		config->errors = TRUE;
	}

	/* Always keep going if errors are encountered. */
	return OK;
}

static enum status_code
load_option_file(const char *path)
{
	struct config_state config = { path, 0, FALSE };
	struct io io;
	char buf[SIZEOF_STR];

	/* Do not read configuration from stdin if set to "" */
	if (!path || !strlen(path))
		return SUCCESS;

	if (!prefixcmp(path, "~/")) {
		const char *home = getenv("HOME");

		if (!home || !string_format(buf, "%s/%s", home, path + 2))
			return ERROR_HOME_UNRESOLVABLE;
		path = buf;
	}

	/* It's OK that the file doesn't exist. */
	if (!io_open(&io, "%s", path))
		return ERROR_FILE_DOES_NOT_EXIST;

	if (io_load(&io, " \t", read_option, &config) == ERR ||
	    config.errors == TRUE)
		warn("Errors while loading %s.", path);
	return SUCCESS;
}

static int
load_options(void)
{
	const char *tigrc_user = getenv("TIGRC_USER");
	const char *tigrc_system = getenv("TIGRC_SYSTEM");
	const char *tig_diff_opts = getenv("TIG_DIFF_OPTS");
	const bool diff_opts_from_args = !!opt_diff_argv;

	if (!tigrc_system)
		tigrc_system = SYSCONFDIR "/tigrc";
	load_option_file(tigrc_system);

	if (!tigrc_user)
		tigrc_user = "~/.tigrc";
	load_option_file(tigrc_user);

	if (!diff_opts_from_args && tig_diff_opts && *tig_diff_opts) {
		static const char *diff_opts[SIZEOF_ARG] = { NULL };
		char buf[SIZEOF_STR];
		int argc = 0;

		if (!string_format(buf, "%s", tig_diff_opts) ||
		    !argv_from_string(diff_opts, &argc, buf))
			die("TIG_DIFF_OPTS contains too many arguments");
		else if (!argv_copy(&opt_diff_argv, diff_opts))
			die("Failed to format TIG_DIFF_OPTS arguments");
	}

	return OK;
}


/*
 * The viewer
 */

struct view;
struct view_ops;

/* The display array of active views and the index of the current view. */
static struct view *display[2];
static WINDOW *display_win[2];
static WINDOW *display_title[2];
static WINDOW *display_sep;

static unsigned int current_view;

#define foreach_displayed_view(view, i) \
	for (i = 0; i < ARRAY_SIZE(display) && (view = display[i]); i++)

#define displayed_views()	(display[1] != NULL ? 2 : 1)

/* Current head and commit ID */
static char ref_blob[SIZEOF_REF]	= "";
static char ref_commit[SIZEOF_REF]	= "HEAD";
static char ref_head[SIZEOF_REF]	= "HEAD";
static char ref_branch[SIZEOF_REF]	= "";
static char ref_status[SIZEOF_STR]	= "";
static char ref_stash[SIZEOF_REF]	= "";

enum view_flag {
	VIEW_NO_FLAGS = 0,
	VIEW_ALWAYS_LINENO	= 1 << 0,
	VIEW_CUSTOM_STATUS	= 1 << 1,
	VIEW_ADD_DESCRIBE_REF	= 1 << 2,
	VIEW_ADD_PAGER_REFS	= 1 << 3,
	VIEW_OPEN_DIFF		= 1 << 4,
	VIEW_NO_REF		= 1 << 5,
	VIEW_NO_GIT_DIR		= 1 << 6,
	VIEW_DIFF_LIKE		= 1 << 7,
	VIEW_SEND_CHILD_ENTER	= 1 << 9,
	VIEW_FILE_FILTER	= 1 << 10,
	VIEW_LOG_LIKE		= 1 << 11,
	VIEW_STATUS_LIKE	= 1 << 12,
	VIEW_REFRESH		= 1 << 13,
};

#define view_has_flags(view, flag)	((view)->ops->flags & (flag))

struct position {
	unsigned long offset;	/* Offset of the window top */
	unsigned long col;	/* Offset from the window side. */
	unsigned long lineno;	/* Current line number */
};

struct view {
	const char *name;	/* View name */
	const char *id;		/* Points to either of ref_{head,commit,blob} */

	struct view_ops *ops;	/* View operations */

	char ref[SIZEOF_REF];	/* Hovered commit reference */
	char vid[SIZEOF_REF];	/* View ID. Set to id member when updating. */

	int height, width;	/* The width and height of the main window */
	WINDOW *win;		/* The main window */

	/* Navigation */
	struct position pos;	/* Current position. */
	struct position prev_pos; /* Previous position. */

	/* Searching */
	char grep[SIZEOF_STR];	/* Search string */
	regex_t *regex;		/* Pre-compiled regexp */

	/* If non-NULL, points to the view that opened this view. If this view
	 * is closed tig will switch back to the parent view. */
	struct view *parent;
	struct view *prev;

	/* Buffering */
	size_t lines;		/* Total number of lines */
	struct line *line;	/* Line index */
	unsigned int digits;	/* Number of digits in the lines member. */

	/* Number of lines with custom status, not to be counted in the
	 * view title. */
	unsigned int custom_lines;

	/* Drawing */
	struct line *curline;	/* Line currently being drawn. */
	enum line_type curtype;	/* Attribute currently used for drawing. */
	unsigned long col;	/* Column when drawing. */
	bool has_scrolled;	/* View was scrolled. */
	bool force_redraw;	/* Whether to force a redraw after reading. */

	/* Loading */
	const char **argv;	/* Shell command arguments. */
	const char *dir;	/* Directory from which to execute. */
	struct io io;
	struct io *pipe;
	time_t start_time;
	time_t update_secs;
	struct encoding *encoding;
	bool unrefreshable;

	/* Private data */
	void *private;
};

enum open_flags {
	OPEN_DEFAULT = 0,	/* Use default view switching. */
	OPEN_STDIN = 1,		/* Open in pager mode. */
	OPEN_FORWARD_STDIN = 2,	/* Forward stdin to I/O process. */
	OPEN_SPLIT = 4,		/* Split current view. */
	OPEN_RELOAD = 8,	/* Reload view even if it is the current. */
	OPEN_REFRESH = 16,	/* Refresh view using previous command. */
	OPEN_PREPARED = 32,	/* Open already prepared command. */
	OPEN_EXTRA = 64,	/* Open extra data from command. */

	OPEN_PAGER_MODE = OPEN_STDIN | OPEN_FORWARD_STDIN,
};

#define open_in_pager_mode(flags) ((flags) & OPEN_PAGER_MODE)
#define open_from_stdin(flags) ((flags) & OPEN_STDIN)

struct view_ops {
	/* What type of content being displayed. Used in the title bar. */
	const char *type;
	/* What keymap does this view have */
	struct keymap keymap;
	/* Flags to control the view behavior. */
	enum view_flag flags;
	/* Size of private data. */
	size_t private_size;
	/* Open and reads in all view content. */
	bool (*open)(struct view *view, enum open_flags flags);
	/* Read one line; updates view->line. */
	bool (*read)(struct view *view, char *data);
	/* Draw one line; @lineno must be < view->height. */
	bool (*draw)(struct view *view, struct line *line, unsigned int lineno);
	/* Depending on view handle a special requests. */
	enum request (*request)(struct view *view, enum request request, struct line *line);
	/* Search for regexp in a line. */
	bool (*grep)(struct view *view, struct line *line);
	/* Select line */
	void (*select)(struct view *view, struct line *line);
	/* Release resources when reloading the view */
	void (*done)(struct view *view);
};

#define VIEW_OPS(id, name, ref) name##_ops
static struct view_ops VIEW_INFO(VIEW_OPS);

static struct view views[] = {
#define VIEW_DATA(id, name, ref) \
	{ #name, ref, &name##_ops }
	VIEW_INFO(VIEW_DATA)
};

#define VIEW(req) 	(&views[(req) - REQ_OFFSET - 1])

#define foreach_view(view, i) \
	for (i = 0; i < ARRAY_SIZE(views) && (view = &views[i]); i++)

#define view_is_displayed(view) \
	(view == display[0] || view == display[1])

#define view_has_line(view, line_) \
	((view)->line <= (line_) && (line_) < (view)->line + (view)->lines)

static bool
forward_request_to_child(struct view *child, enum request request)
{
	return displayed_views() == 2 && view_is_displayed(child) &&
		!strcmp(child->vid, child->id);
}

static enum request
view_request(struct view *view, enum request request)
{
	if (!view || !view->lines)
		return request;

	if (request == REQ_ENTER && !opt_focus_child &&
	    view_has_flags(view, VIEW_SEND_CHILD_ENTER)) {
		struct view *child = display[1];

	    	if (forward_request_to_child(child, request)) {
			view_request(child, request);
			return REQ_NONE;
		}
	}

	if (request == REQ_REFRESH && view->unrefreshable) {
		report("This view can not be refreshed");
		return REQ_NONE;
	}

	return view->ops->request(view, request, &view->line[view->pos.lineno]);
}

/*
 * View drawing.
 */

static inline void
set_view_attr(struct view *view, enum line_type type)
{
	if (!view->curline->selected && view->curtype != type) {
		(void) wattrset(view->win, get_line_attr(type));
		wchgat(view->win, -1, 0, get_line_color(type), NULL);
		view->curtype = type;
	}
}

#define VIEW_MAX_LEN(view) ((view)->width + (view)->pos.col - (view)->col)

static bool
draw_chars(struct view *view, enum line_type type, const char *string,
	   int max_len, bool use_tilde)
{
	int len = 0;
	int col = 0;
	int trimmed = FALSE;
	size_t skip = view->pos.col > view->col ? view->pos.col - view->col : 0;

	if (max_len <= 0)
		return VIEW_MAX_LEN(view) <= 0;

	if (opt_iconv_out != ICONV_NONE) {
		string = encoding_iconv(opt_iconv_out, string);
		if (!string)
			return VIEW_MAX_LEN(view) <= 0;
	}

	len = utf8_length(&string, skip, &col, max_len, &trimmed, use_tilde, opt_tab_size);

	set_view_attr(view, type);
	if (len > 0) {
		waddnstr(view->win, string, len);

		if (trimmed && use_tilde) {
			set_view_attr(view, LINE_DELIMITER);
			waddch(view->win, '~');
			col++;
		}
	}

	view->col += col;
	return VIEW_MAX_LEN(view) <= 0;
}

static bool
draw_space(struct view *view, enum line_type type, int max, int spaces)
{
	static char space[] = "                    ";

	spaces = MIN(max, spaces);

	while (spaces > 0) {
		int len = MIN(spaces, sizeof(space) - 1);

		if (draw_chars(view, type, space, len, FALSE))
			return TRUE;
		spaces -= len;
	}

	return VIEW_MAX_LEN(view) <= 0;
}

static bool
draw_text_expanded(struct view *view, enum line_type type, const char *string, int max_len, bool use_tilde)
{
	static char text[SIZEOF_STR];

	do {
		size_t pos = string_expand(text, sizeof(text), string, opt_tab_size);

		if (draw_chars(view, type, text, max_len, use_tilde))
			return TRUE;
		string += pos;
	} while (*string);

	return VIEW_MAX_LEN(view) <= 0;
}

static bool
draw_text(struct view *view, enum line_type type, const char *string)
{
	return draw_text_expanded(view, type, string, VIEW_MAX_LEN(view), TRUE);
}

static bool
draw_text_overflow(struct view *view, const char *text, bool on, int overflow, enum line_type type)
{
	if (on) {
		int max = MIN(VIEW_MAX_LEN(view), overflow);
		int len = strlen(text);

		if (draw_text_expanded(view, type, text, max, max < overflow))
			return TRUE;

		text = len > overflow ? text + overflow : "";
		type = LINE_OVERFLOW;
	}

	if (*text && draw_text(view, type, text))
		return TRUE;

	return VIEW_MAX_LEN(view) <= 0;
}

#define draw_commit_title(view, text, offset) \
	draw_text_overflow(view, text, opt_show_title_overflow, opt_title_overflow + offset, LINE_DEFAULT)

static bool PRINTF_LIKE(3, 4)
draw_formatted(struct view *view, enum line_type type, const char *format, ...)
{
	char text[SIZEOF_STR];
	int retval;

	FORMAT_BUFFER(text, sizeof(text), format, retval, TRUE);
	return retval >= 0 ? draw_text(view, type, text) : VIEW_MAX_LEN(view) <= 0;
}

static bool
draw_graphic(struct view *view, enum line_type type, const chtype graphic[], size_t size, bool separator)
{
	size_t skip = view->pos.col > view->col ? view->pos.col - view->col : 0;
	int max = VIEW_MAX_LEN(view);
	int i;

	if (max < size)
		size = max;

	set_view_attr(view, type);
	/* Using waddch() instead of waddnstr() ensures that
	 * they'll be rendered correctly for the cursor line. */
	for (i = skip; i < size; i++)
		waddch(view->win, graphic[i]);

	view->col += size;
	if (separator) {
		if (size < max && skip <= size)
			waddch(view->win, ' ');
		view->col++;
	}

	return VIEW_MAX_LEN(view) <= 0;
}

enum align {
	ALIGN_LEFT,
	ALIGN_RIGHT
};

static bool
draw_field(struct view *view, enum line_type type, const char *text, int width, enum align align, bool trim)
{
	int max = MIN(VIEW_MAX_LEN(view), width + 1);
	int col = view->col;

	if (!text)
		return draw_space(view, type, max, max);

	if (align == ALIGN_RIGHT) {
		int textlen = strlen(text);
		int leftpad = max - textlen - 1;

		if (leftpad > 0) {
	    		if (draw_space(view, type, leftpad, leftpad))
				return TRUE;
			max -= leftpad;
			col += leftpad;;
		}
	}

	return draw_chars(view, type, text, max - 1, trim)
	    || draw_space(view, LINE_DEFAULT, max - (view->col - col), max);
}

static bool
draw_date(struct view *view, struct time *time)
{
	const char *date = mkdate(time, opt_date);
	int cols = opt_date == DATE_SHORT ? DATE_SHORT_WIDTH : DATE_WIDTH;

	if (opt_date == DATE_NO)
		return FALSE;

	return draw_field(view, LINE_DATE, date, cols, ALIGN_LEFT, FALSE);
}

static bool
draw_author(struct view *view, const struct ident *author)
{
	bool trim = author_trim(opt_author_width);
	const char *text = mkauthor(author, opt_author_width, opt_author);

	if (opt_author == AUTHOR_NO)
		return FALSE;

	return draw_field(view, LINE_AUTHOR, text, opt_author_width, ALIGN_LEFT, trim);
}

static bool
draw_id_custom(struct view *view, enum line_type type, const char *id, int width)
{
	return draw_field(view, type, id, width, ALIGN_LEFT, FALSE);
}

static bool
draw_id(struct view *view, const char *id)
{
	if (!opt_show_id)
		return FALSE;

	return draw_id_custom(view, LINE_ID, id, opt_id_cols);
}

static bool
draw_filename(struct view *view, const char *filename, bool auto_enabled)
{
	bool trim = filename && strlen(filename) >= opt_filename_width;

	if (opt_filename == FILENAME_NO)
		return FALSE;

	if (opt_filename == FILENAME_AUTO && !auto_enabled)
		return FALSE;

	return draw_field(view, LINE_FILENAME, filename, opt_filename_width, ALIGN_LEFT, trim);
}

static bool
draw_file_size(struct view *view, unsigned long size, int width, bool pad)
{
	const char *str = pad ? NULL : mkfilesize(size, opt_file_size);

	if (!width || opt_file_size == FILE_SIZE_NO)
		return FALSE;

	return draw_field(view, LINE_FILE_SIZE, str, width, ALIGN_RIGHT, FALSE);
}

static bool
draw_mode(struct view *view, mode_t mode)
{
	const char *str = mkmode(mode);

	return draw_field(view, LINE_MODE, str, STRING_SIZE("-rw-r--r--"), ALIGN_LEFT, FALSE);
}

static bool
draw_lineno(struct view *view, unsigned int lineno)
{
	char number[10];
	int digits3 = view->digits < 3 ? 3 : view->digits;
	int max = MIN(VIEW_MAX_LEN(view), digits3);
	char *text = NULL;
	chtype separator = opt_line_graphics ? ACS_VLINE : '|';

	if (!opt_line_number)
		return FALSE;

	lineno += view->pos.offset + 1;
	if (lineno == 1 || (lineno % opt_num_interval) == 0) {
		static char fmt[] = "%1ld";

		fmt[1] = '0' + (view->digits <= 9 ? digits3 : 1);
		if (string_format(number, fmt, lineno))
			text = number;
	}
	if (text)
		draw_chars(view, LINE_LINE_NUMBER, text, max, TRUE);
	else
		draw_space(view, LINE_LINE_NUMBER, max, digits3);
	return draw_graphic(view, LINE_DEFAULT, &separator, 1, TRUE);
}

static bool
draw_refs(struct view *view, struct ref_list *refs)
{
	size_t i;

	if (!opt_show_refs || !refs)
		return FALSE;

	for (i = 0; i < refs->size; i++) {
		struct ref *ref = refs->refs[i];
		enum line_type type = get_line_type_from_ref(ref);

		if (draw_formatted(view, type, "[%s]", ref->name))
			return TRUE;

		if (draw_text(view, LINE_DEFAULT, " "))
			return TRUE;
	}

	return FALSE;
}

static bool
draw_view_line(struct view *view, unsigned int lineno)
{
	struct line *line;
	bool selected = (view->pos.offset + lineno == view->pos.lineno);

	assert(view_is_displayed(view));

	if (view->pos.offset + lineno >= view->lines)
		return FALSE;

	line = &view->line[view->pos.offset + lineno];

	wmove(view->win, lineno, 0);
	if (line->cleareol)
		wclrtoeol(view->win);
	view->col = 0;
	view->curline = line;
	view->curtype = LINE_NONE;
	line->selected = FALSE;
	line->dirty = line->cleareol = 0;

	if (selected) {
		set_view_attr(view, LINE_CURSOR);
		line->selected = TRUE;
		view->ops->select(view, line);
	}

	return view->ops->draw(view, line, lineno);
}

static void
redraw_view_dirty(struct view *view)
{
	bool dirty = FALSE;
	int lineno;

	for (lineno = 0; lineno < view->height; lineno++) {
		if (view->pos.offset + lineno >= view->lines)
			break;
		if (!view->line[view->pos.offset + lineno].dirty)
			continue;
		dirty = TRUE;
		if (!draw_view_line(view, lineno))
			break;
	}

	if (!dirty)
		return;
	wnoutrefresh(view->win);
}

static void
redraw_view_from(struct view *view, int lineno)
{
	assert(0 <= lineno && lineno < view->height);

	for (; lineno < view->height; lineno++) {
		if (!draw_view_line(view, lineno))
			break;
	}

	wnoutrefresh(view->win);
}

static void
redraw_view(struct view *view)
{
	werase(view->win);
	redraw_view_from(view, 0);
}


static void
update_view_title(struct view *view)
{
	WINDOW *window = display[0] == view ? display_title[0] : display_title[1];
	struct line *line = &view->line[view->pos.lineno];
	unsigned int view_lines, lines;

	assert(view_is_displayed(view));

	if (view == display[current_view])
		wbkgdset(window, get_line_attr(LINE_TITLE_FOCUS));
	else
		wbkgdset(window, get_line_attr(LINE_TITLE_BLUR));

	werase(window);
	mvwprintw(window, 0, 0, "[%s]", view->name);

	if (*view->ref) {
		wprintw(window, " %s", view->ref);
	}

	if (!view_has_flags(view, VIEW_CUSTOM_STATUS) && view_has_line(view, line) &&
	    line->lineno) {
		wprintw(window, " - %s %d of %zd",
					   view->ops->type,
					   line->lineno,
					   view->lines - view->custom_lines);
	}

	if (view->pipe) {
		time_t secs = time(NULL) - view->start_time;

		/* Three git seconds are a long time ... */
		if (secs > 2)
			wprintw(window, " loading %lds", secs);
	}

	view_lines = view->pos.offset + view->height;
	lines = view->lines ? MIN(view_lines, view->lines) * 100 / view->lines : 0;
	mvwprintw(window, 0, view->width - count_digits(lines) - 1, "%d%%", lines);

	wnoutrefresh(window);
}

static int
apply_step(double step, int value)
{
	if (step >= 1)
		return (int) step;
	value *= step + 0.01;
	return value ? value : 1;
}

static void
apply_horizontal_split(struct view *base, struct view *view)
{
	view->width   = base->width;
	view->height  = apply_step(opt_scale_split_view, base->height);
	view->height  = MAX(view->height, MIN_VIEW_HEIGHT);
	view->height  = MIN(view->height, base->height - MIN_VIEW_HEIGHT);
	base->height -= view->height;
}

static void
apply_vertical_split(struct view *base, struct view *view)
{
	view->height = base->height;
	view->width  = apply_step(opt_scale_vsplit_view, base->width);
	view->width  = MAX(view->width, MIN_VIEW_WIDTH);
	view->width  = MIN(view->width, base->width - MIN_VIEW_WIDTH);
	base->width -= view->width;
}

static void
redraw_display_separator(bool clear)
{
	if (displayed_views() > 1 && vertical_split_is_enabled()) {
		chtype separator = opt_line_graphics ? ACS_VLINE : '|';

		if (clear)
			wclear(display_sep);
		wbkgd(display_sep, separator + get_line_attr(LINE_TITLE_BLUR));
		wnoutrefresh(display_sep);
	}
}

static void
resize_display(void)
{
	int x, y, i;
	struct view *base = display[0];
	struct view *view = display[1] ? display[1] : display[0];
	bool vsplit;

	/* Setup window dimensions */

	getmaxyx(stdscr, base->height, base->width);
	string_format(opt_env_columns, "COLUMNS=%d", base->width);
	string_format(opt_env_lines, "LINES=%d", base->height);

	/* Make room for the status window. */
	base->height -= 1;

	vsplit = vertical_split_is_enabled();

	if (view != base) {
		if (vsplit) {
			apply_vertical_split(base, view);

			/* Make room for the separator bar. */
			view->width -= 1;
		} else {
			apply_horizontal_split(base, view);
		}

		/* Make room for the title bar. */
		view->height -= 1;
	}

	/* Make room for the title bar. */
	base->height -= 1;

	x = y = 0;

	foreach_displayed_view (view, i) {
		if (!display_win[i]) {
			display_win[i] = newwin(view->height, view->width, y, x);
			if (!display_win[i])
				die("Failed to create %s view", view->name);

			scrollok(display_win[i], FALSE);

			display_title[i] = newwin(1, view->width, y + view->height, x);
			if (!display_title[i])
				die("Failed to create title window");

		} else {
			wresize(display_win[i], view->height, view->width);
			mvwin(display_win[i], y, x);
			wresize(display_title[i], 1, view->width);
			mvwin(display_title[i], y + view->height, x);
		}

		if (i > 0 && vsplit) {
			if (!display_sep) {
				display_sep = newwin(view->height, 1, 0, x - 1);
				if (!display_sep)
					die("Failed to create separator window");

			} else {
				wresize(display_sep, view->height, 1);
				mvwin(display_sep, 0, x - 1);
			}
		}

		view->win = display_win[i];

		if (vsplit)
			x += view->width + 1;
		else
			y += view->height + 1;
	}

	redraw_display_separator(FALSE);
}

static void
redraw_display(bool clear)
{
	struct view *view;
	int i;

	foreach_displayed_view (view, i) {
		if (clear)
			wclear(view->win);
		redraw_view(view);
		update_view_title(view);
	}

	redraw_display_separator(clear);
}

/*
 * Option management
 */

#define VIEW_FLAG_RESET_DISPLAY	((enum view_flag) -1)

#define TOGGLE_MENU_INFO(_) \
	_(LINENO,    '.', "line numbers",      &opt_line_number, NULL, VIEW_NO_FLAGS), \
	_(DATE,      'D', "dates",             &opt_date, date_map, VIEW_NO_FLAGS), \
	_(AUTHOR,    'A', "author",            &opt_author, author_map, VIEW_NO_FLAGS), \
	_(GRAPHIC,   '~', "graphics",          &opt_line_graphics, graphic_map, VIEW_NO_FLAGS), \
	_(REV_GRAPH, 'g', "revision graph",    &opt_rev_graph, NULL, VIEW_LOG_LIKE), \
	_(FILENAME,  '#', "file names",        &opt_filename, filename_map, VIEW_NO_FLAGS), \
	_(FILE_SIZE, '*', "file sizes",        &opt_file_size, file_size_map, VIEW_NO_FLAGS), \
	_(IGNORE_SPACE, 'W', "space changes",  &opt_ignore_space, ignore_space_map, VIEW_DIFF_LIKE), \
	_(COMMIT_ORDER, 'l', "commit order",   &opt_commit_order, commit_order_map, VIEW_LOG_LIKE), \
	_(REFS,      'F', "reference display", &opt_show_refs, NULL, VIEW_NO_FLAGS), \
	_(CHANGES,   'C', "local change display", &opt_show_changes, NULL, VIEW_NO_FLAGS), \
	_(ID,        'X', "commit ID display", &opt_show_id, NULL, VIEW_NO_FLAGS), \
	_(FILES,     '%', "file filtering",    &opt_file_filter, NULL, VIEW_DIFF_LIKE | VIEW_LOG_LIKE), \
	_(TITLE_OVERFLOW, '$', "commit title overflow display", &opt_show_title_overflow, NULL, VIEW_NO_FLAGS), \
	_(UNTRACKED_DIRS, 'd', "untracked directory info", &opt_untracked_dirs_content, NULL, VIEW_STATUS_LIKE), \
	_(VERTICAL_SPLIT, '|', "view split",   &opt_vertical_split, vertical_split_map, VIEW_FLAG_RESET_DISPLAY), \

static enum view_flag
toggle_option(struct view *view, enum request request, char msg[SIZEOF_STR])
{
	const struct {
		enum request request;
		const struct enum_map *map;
		enum view_flag reload_flags;
	} data[] = {
#define DEFINE_TOGGLE_DATA(id, key, help, value, map, vflags) { REQ_TOGGLE_ ## id, map, vflags  }
		TOGGLE_MENU_INFO(DEFINE_TOGGLE_DATA)
	};
	const struct menu_item menu[] = {
#define DEFINE_TOGGLE_MENU(id, key, help, value, map, vflags) { key, help, value }
		TOGGLE_MENU_INFO(DEFINE_TOGGLE_MENU)
		{ 0 }
	};
	int i = 0;

	if (request == REQ_OPTIONS) {
		if (!prompt_menu("Toggle option", menu, &i))
			return VIEW_NO_FLAGS;
	} else {
		while (i < ARRAY_SIZE(data) && data[i].request != request)
			i++;
		if (i >= ARRAY_SIZE(data))
			die("Invalid request (%d)", request);
	}

	if (data[i].map != NULL) {
		unsigned int *opt = menu[i].data;

		*opt = (*opt + 1) % data[i].map->size;
		if (data[i].map == ignore_space_map) {
			update_ignore_space_arg();
			string_format_size(msg, SIZEOF_STR,
				"Ignoring %s %s", enum_name(data[i].map->entries[*opt]), menu[i].text);

		} else if (data[i].map == commit_order_map) {
			update_commit_order_arg();
			string_format_size(msg, SIZEOF_STR,
				"Using %s %s", enum_name(data[i].map->entries[*opt]), menu[i].text);

		} else {
			string_format_size(msg, SIZEOF_STR,
				"Displaying %s %s", enum_name(data[i].map->entries[*opt]), menu[i].text);
		}

	} else {
		bool *option = menu[i].data;

		*option = !*option;
		string_format_size(msg, SIZEOF_STR,
			"%sabling %s", *option ? "En" : "Dis", menu[i].text);
	}

	return data[i].reload_flags;
}


/*
 * Navigation
 */

static bool
goto_view_line(struct view *view, unsigned long offset, unsigned long lineno)
{
	if (lineno >= view->lines)
		lineno = view->lines > 0 ? view->lines - 1 : 0;

	if (offset > lineno || offset + view->height <= lineno) {
		unsigned long half = view->height / 2;

		if (lineno > half)
			offset = lineno - half;
		else
			offset = 0;
	}

	if (offset != view->pos.offset || lineno != view->pos.lineno) {
		view->pos.offset = offset;
		view->pos.lineno = lineno;
		return TRUE;
	}

	return FALSE;
}

/* Scrolling backend */
static void
do_scroll_view(struct view *view, int lines)
{
	bool redraw_current_line = FALSE;

	/* The rendering expects the new offset. */
	view->pos.offset += lines;

	assert(0 <= view->pos.offset && view->pos.offset < view->lines);
	assert(lines);

	/* Move current line into the view. */
	if (view->pos.lineno < view->pos.offset) {
		view->pos.lineno = view->pos.offset;
		redraw_current_line = TRUE;
	} else if (view->pos.lineno >= view->pos.offset + view->height) {
		view->pos.lineno = view->pos.offset + view->height - 1;
		redraw_current_line = TRUE;
	}

	assert(view->pos.offset <= view->pos.lineno && view->pos.lineno < view->lines);

	/* Redraw the whole screen if scrolling is pointless. */
	if (view->height < ABS(lines)) {
		redraw_view(view);

	} else {
		int line = lines > 0 ? view->height - lines : 0;
		int end = line + ABS(lines);

		scrollok(view->win, TRUE);
		wscrl(view->win, lines);
		scrollok(view->win, FALSE);

		while (line < end && draw_view_line(view, line))
			line++;

		if (redraw_current_line)
			draw_view_line(view, view->pos.lineno - view->pos.offset);
		wnoutrefresh(view->win);
	}

	view->has_scrolled = TRUE;
	report_clear();
}

/* Scroll frontend */
static void
scroll_view(struct view *view, enum request request)
{
	int lines = 1;

	assert(view_is_displayed(view));

	if (request == REQ_SCROLL_WHEEL_DOWN || request == REQ_SCROLL_WHEEL_UP)
		lines = opt_scroll_wheel_lines;

	switch (request) {
	case REQ_SCROLL_FIRST_COL:
		view->pos.col = 0;
		redraw_view_from(view, 0);
		report_clear();
		return;
	case REQ_SCROLL_LEFT:
		if (view->pos.col == 0) {
			report("Cannot scroll beyond the first column");
			return;
		}
		if (view->pos.col <= apply_step(opt_hscroll, view->width))
			view->pos.col = 0;
		else
			view->pos.col -= apply_step(opt_hscroll, view->width);
		redraw_view_from(view, 0);
		report_clear();
		return;
	case REQ_SCROLL_RIGHT:
		view->pos.col += apply_step(opt_hscroll, view->width);
		redraw_view(view);
		report_clear();
		return;
	case REQ_SCROLL_PAGE_DOWN:
		lines = view->height;
	case REQ_SCROLL_WHEEL_DOWN:
	case REQ_SCROLL_LINE_DOWN:
		if (view->pos.offset + lines > view->lines)
			lines = view->lines - view->pos.offset;

		if (lines == 0 || view->pos.offset + view->height >= view->lines) {
			report("Cannot scroll beyond the last line");
			return;
		}
		break;

	case REQ_SCROLL_PAGE_UP:
		lines = view->height;
	case REQ_SCROLL_LINE_UP:
	case REQ_SCROLL_WHEEL_UP:
		if (lines > view->pos.offset)
			lines = view->pos.offset;

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
		steps = -view->pos.lineno;
		break;

	case REQ_MOVE_LAST_LINE:
		steps = view->lines - view->pos.lineno - 1;
		break;

	case REQ_MOVE_PAGE_UP:
		steps = view->height > view->pos.lineno
		      ? -view->pos.lineno : -view->height;
		break;

	case REQ_MOVE_PAGE_DOWN:
		steps = view->pos.lineno + view->height >= view->lines
		      ? view->lines - view->pos.lineno - 1 : view->height;
		break;

	case REQ_MOVE_UP:
	case REQ_PREVIOUS:
		steps = -1;
		break;

	case REQ_MOVE_DOWN:
	case REQ_NEXT:
		steps = 1;
		break;

	default:
		die("request %d not handled in switch", request);
	}

	if (steps <= 0 && view->pos.lineno == 0) {
		report("Cannot move beyond the first line");
		return;

	} else if (steps >= 0 && view->pos.lineno + 1 >= view->lines) {
		report("Cannot move beyond the last line");
		return;
	}

	/* Move the current line */
	view->pos.lineno += steps;
	assert(0 <= view->pos.lineno && view->pos.lineno < view->lines);

	/* Check whether the view needs to be scrolled */
	if (view->pos.lineno < view->pos.offset ||
	    view->pos.lineno >= view->pos.offset + view->height) {
		scroll_steps = steps;
		if (steps < 0 && -steps > view->pos.offset) {
			scroll_steps = -view->pos.offset;

		} else if (steps > 0) {
			if (view->pos.lineno == view->lines - 1 &&
			    view->lines > view->height) {
				scroll_steps = view->lines - view->pos.offset - 1;
				if (scroll_steps >= view->height)
					scroll_steps -= view->height - 1;
			}
		}
	}

	if (!view_is_displayed(view)) {
		view->pos.offset += scroll_steps;
		assert(0 <= view->pos.offset && view->pos.offset < view->lines);
		view->ops->select(view, &view->line[view->pos.lineno]);
		return;
	}

	/* Repaint the old "current" line if we be scrolling */
	if (ABS(steps) < view->height)
		draw_view_line(view, view->pos.lineno - steps - view->pos.offset);

	if (scroll_steps) {
		do_scroll_view(view, scroll_steps);
		return;
	}

	/* Draw the current line */
	draw_view_line(view, view->pos.lineno - view->pos.offset);

	wnoutrefresh(view->win);
	report_clear();
}


/*
 * Searching
 */

static void search_view(struct view *view, enum request request);

static bool
grep_text(struct view *view, const char *text[])
{
	regmatch_t pmatch;
	size_t i;

	for (i = 0; text[i]; i++)
		if (*text[i] && !regexec(view->regex, text[i], 1, &pmatch, 0))
			return TRUE;
	return FALSE;
}

static void
select_view_line(struct view *view, unsigned long lineno)
{
	struct position old = view->pos;

	if (goto_view_line(view, view->pos.offset, lineno)) {
		if (view_is_displayed(view)) {
			if (old.offset != view->pos.offset) {
				redraw_view(view);
			} else {
				draw_view_line(view, old.lineno - view->pos.offset);
				draw_view_line(view, view->pos.lineno - view->pos.offset);
				wnoutrefresh(view->win);
			}
		} else {
			view->ops->select(view, &view->line[view->pos.lineno]);
		}
	}
}

static void
find_next(struct view *view, enum request request)
{
	unsigned long lineno = view->pos.lineno;
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
		if (view->ops->grep(view, &view->line[lineno])) {
			select_view_line(view, lineno);
			report("Line %ld matches '%s'", lineno + 1, view->grep);
			return;
		}
	}

	report("No match found for '%s'", view->grep);
}

static void
search_view(struct view *view, enum request request)
{
	int regex_err;
	int regex_flags = opt_ignore_case ? REG_ICASE : 0;

	if (view->regex) {
		regfree(view->regex);
		*view->grep = 0;
	} else {
		view->regex = calloc(1, sizeof(*view->regex));
		if (!view->regex)
			return;
	}

	regex_err = regcomp(view->regex, opt_search, REG_EXTENDED | regex_flags);
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

static inline bool
check_position(struct position *pos)
{
	return pos->lineno || pos->col || pos->offset;
}

static inline void
clear_position(struct position *pos)
{
	memset(pos, 0, sizeof(*pos));
}

static void
reset_view(struct view *view)
{
	int i;

	if (view->ops->done)
		view->ops->done(view);

	for (i = 0; i < view->lines; i++)
		free(view->line[i].data);
	free(view->line);

	view->prev_pos = view->pos;
	clear_position(&view->pos);

	view->line = NULL;
	view->lines  = 0;
	view->vid[0] = 0;
	view->custom_lines = 0;
	view->update_secs = 0;
}

struct format_context {
	struct view *view;
	char buf[SIZEOF_STR];
	size_t bufpos;
	bool file_filter;
};

static bool
format_expand_arg(struct format_context *format, const char *name, const char *end)
{
	static struct {
		const char *name;
		size_t namelen;
		const char *value;
		const char *value_if_empty;
	} vars[] = {
#define FORMAT_VAR(name, value, value_if_empty) \
	{ name, STRING_SIZE(name), value, value_if_empty }
		FORMAT_VAR("%(directory)",	opt_path,	"."),
		FORMAT_VAR("%(file)",		opt_file,	""),
		FORMAT_VAR("%(ref)",		opt_ref,	"HEAD"),
		FORMAT_VAR("%(head)",		ref_head,	""),
		FORMAT_VAR("%(commit)",		ref_commit,	""),
		FORMAT_VAR("%(blob)",		ref_blob,	""),
		FORMAT_VAR("%(branch)",		ref_branch,	""),
		FORMAT_VAR("%(stash)",		ref_stash,	""),
	};
	int i;

	if (!prefixcmp(name, "%(prompt")) {
		const char *prompt = "Command argument: ";
		char msgbuf[SIZEOF_STR];
		const char *value;
		const char *msgstart = name + STRING_SIZE("%(prompt");
		int msglen = end - msgstart - 1;

		if (end && msglen > 0 && string_format(msgbuf, "%.*s", msglen, msgstart)) {
			const char *msg = msgbuf;

			while (isspace(*msg))
				msg++;
			if (*msg)
				prompt = msg;
		}

		value = read_prompt(prompt);
		if (value == NULL)
			return FALSE;
		return string_format_from(format->buf, &format->bufpos, "%s", value);
	}

	for (i = 0; i < ARRAY_SIZE(vars); i++) {
		const char *value;

		if (strncmp(name, vars[i].name, vars[i].namelen))
			continue;

		if (vars[i].value == opt_file && !format->file_filter)
			return TRUE;

		value = *vars[i].value ? vars[i].value : vars[i].value_if_empty;
		if (!*value)
			return TRUE;

		return string_format_from(format->buf, &format->bufpos, "%s", value);
	}

	report("Unknown replacement: `%s`", name);
	return FALSE;
}

static bool
format_append_arg(struct format_context *format, const char ***dst_argv, const char *arg)
{
	memset(format->buf, 0, sizeof(format->buf));
	format->bufpos = 0;

	while (arg) {
		char *var = strstr(arg, "%(");
		int len = var ? var - arg : strlen(arg);
		char *next = var ? strchr(var, ')') + 1 : NULL;

		if (len && !string_format_from(format->buf, &format->bufpos, "%.*s", len, arg))
			return FALSE;

		if (var && !format_expand_arg(format, var, next))
			return FALSE;

		arg = next;
	}

	return argv_append(dst_argv, format->buf);
}

static bool
format_append_argv(struct format_context *format, const char ***dst_argv, const char *src_argv[])
{
	int argc;

	if (!src_argv)
		return TRUE;

	for (argc = 0; src_argv[argc]; argc++)
		if (!format_append_arg(format, dst_argv, src_argv[argc]))
			return FALSE;

	return src_argv[argc] == NULL;
}

static bool
format_argv(struct view *view, const char ***dst_argv, const char *src_argv[], bool first, bool file_filter)
{
	struct format_context format = { view, "", 0, file_filter };
	int argc;

	argv_free(*dst_argv);

	for (argc = 0; src_argv[argc]; argc++) {
		const char *arg = src_argv[argc];

		if (!strcmp(arg, "%(fileargs)")) {
			if (file_filter && !argv_append_array(dst_argv, opt_file_argv))
				break;

		} else if (!strcmp(arg, "%(diffargs)")) {
			if (!format_append_argv(&format, dst_argv, opt_diff_argv))
				break;

		} else if (!strcmp(arg, "%(blameargs)")) {
			if (!format_append_argv(&format, dst_argv, opt_blame_argv))
				break;

		} else if (!strcmp(arg, "%(cmdlineargs)")) {
			if (!format_append_argv(&format, dst_argv, opt_cmdline_argv))
				break;

		} else if (!strcmp(arg, "%(revargs)") ||
			   (first && !strcmp(arg, "%(commit)"))) {
			if (!argv_append_array(dst_argv, opt_rev_argv))
				break;

		} else if (!format_append_arg(&format, dst_argv, arg)) {
			break;
		}
	}

	return src_argv[argc] == NULL;
}

static bool
restore_view_position(struct view *view)
{
	/* A view without a previous view is the first view */
	if (!view->prev && opt_lineno && opt_lineno <= view->lines) {
		select_view_line(view, opt_lineno - 1);
		opt_lineno = 0;
	}

	/* Ensure that the view position is in a valid state. */
	if (!check_position(&view->prev_pos) ||
	    (view->pipe && view->lines <= view->prev_pos.lineno))
		return goto_view_line(view, view->pos.offset, view->pos.lineno);

	/* Changing the view position cancels the restoring. */
	/* FIXME: Changing back to the first line is not detected. */
	if (check_position(&view->pos)) {
		clear_position(&view->prev_pos);
		return FALSE;
	}

	if (goto_view_line(view, view->prev_pos.offset, view->prev_pos.lineno) &&
	    view_is_displayed(view))
		werase(view->win);

	view->pos.col = view->prev_pos.col;
	clear_position(&view->prev_pos);

	return TRUE;
}

static void
end_update(struct view *view, bool force)
{
	if (!view->pipe)
		return;
	while (!view->ops->read(view, NULL))
		if (!force)
			return;
	if (force)
		io_kill(view->pipe);
	io_done(view->pipe);
	view->pipe = NULL;
}

static void
setup_update(struct view *view, const char *vid)
{
	reset_view(view);
	/* XXX: Do not use string_copy_rev(), it copies until first space. */
	string_ncopy(view->vid, vid, strlen(vid));
	view->pipe = &view->io;
	view->start_time = time(NULL);
}

static bool
begin_update(struct view *view, const char *dir, const char **argv, enum open_flags flags)
{
	bool extra = !!(flags & (OPEN_EXTRA));
	bool reload = !!(flags & (OPEN_RELOAD | OPEN_REFRESH | OPEN_PREPARED | OPEN_EXTRA | OPEN_PAGER_MODE));
	bool refresh = flags & (OPEN_REFRESH | OPEN_PREPARED | OPEN_STDIN);
	bool forward_stdin = flags & OPEN_FORWARD_STDIN;
	enum io_type io_type = forward_stdin ? IO_RD_STDIN : IO_RD;

	if ((!reload && !strcmp(view->vid, view->id)) ||
	    ((flags & OPEN_REFRESH) && view->unrefreshable))
		return TRUE;

	if (view->pipe) {
		if (extra)
			io_done(view->pipe);
		else
			end_update(view, TRUE);
	}

	view->unrefreshable = open_in_pager_mode(flags);

	if (!refresh && argv) {
		bool file_filter = !view_has_flags(view, VIEW_FILE_FILTER) || opt_file_filter;

		view->dir = dir;
		if (!format_argv(view, &view->argv, argv, !view->prev, file_filter)) {
			report("Failed to format %s arguments", view->name);
			return FALSE;
		}

		/* Put the current ref_* value to the view title ref
		 * member. This is needed by the blob view. Most other
		 * views sets it automatically after loading because the
		 * first line is a commit line. */
		string_copy_rev(view->ref, view->id);
	}

	if (view->argv && view->argv[0] &&
	    !io_run(&view->io, io_type, view->dir, opt_env, view->argv)) {
		report("Failed to open %s view", view->name);
		return FALSE;
	}

	if (open_from_stdin(flags)) {
		if (!io_open(&view->io, "%s", ""))
			die("Failed to open stdin");
	}

	if (!extra)
		setup_update(view, view->id);

	return TRUE;
}

static bool
update_view(struct view *view)
{
	char *line;
	/* Clear the view and redraw everything since the tree sorting
	 * might have rearranged things. */
	bool redraw = view->lines == 0;
	bool can_read = TRUE;
	struct encoding *encoding = view->encoding ? view->encoding : default_encoding;

	if (!view->pipe)
		return TRUE;

	if (!io_can_read(view->pipe, FALSE)) {
		if (view->lines == 0 && view_is_displayed(view)) {
			time_t secs = time(NULL) - view->start_time;

			if (secs > 1 && secs > view->update_secs) {
				if (view->update_secs == 0)
					redraw_view(view);
				update_view_title(view);
				view->update_secs = secs;
			}
		}
		return TRUE;
	}

	for (; (line = io_get(view->pipe, '\n', can_read)); can_read = FALSE) {
		if (encoding) {
			line = encoding_convert(encoding, line);
		}

		if (!view->ops->read(view, line)) {
			report("Allocation failure");
			end_update(view, TRUE);
			return FALSE;
		}
	}

	{
		int digits = count_digits(view->lines);

		/* Keep the displayed view in sync with line number scaling. */
		if (digits != view->digits) {
			view->digits = digits;
			if (opt_line_number || view_has_flags(view, VIEW_ALWAYS_LINENO))
				redraw = TRUE;
		}
	}

	if (io_error(view->pipe)) {
		report("Failed to read: %s", io_strerror(view->pipe));
		end_update(view, TRUE);

	} else if (io_eof(view->pipe)) {
		end_update(view, FALSE);
	}

	if (restore_view_position(view))
		redraw = TRUE;

	if (!view_is_displayed(view))
		return TRUE;

	if (redraw || view->force_redraw)
		redraw_view_from(view, 0);
	else
		redraw_view_dirty(view);
	view->force_redraw = FALSE;

	/* Update the title _after_ the redraw so that if the redraw picks up a
	 * commit reference in view->ref it'll be available here. */
	update_view_title(view);
	return TRUE;
}

DEFINE_ALLOCATOR(realloc_lines, struct line, 256)

static struct line *
add_line_at(struct view *view, unsigned long pos, const void *data, enum line_type type, size_t data_size, bool custom)
{
	struct line *line;
	unsigned long lineno;

	if (!realloc_lines(&view->line, view->lines, 1))
		return NULL;

	if (data_size) {
		void *alloc_data = calloc(1, data_size);

		if (!alloc_data)
			return NULL;

		if (data)
			memcpy(alloc_data, data, data_size);
		data = alloc_data;
	}

	if (pos < view->lines) {
		view->lines++;
		line = view->line + pos;
		lineno = line->lineno;

		memmove(line + 1, line, (view->lines - pos) * sizeof(*view->line));
		while (pos < view->lines) {
			view->line[pos].lineno++;
			view->line[pos++].dirty = 1;
		}
	} else {
		line = &view->line[view->lines++];
		lineno = view->lines - view->custom_lines;
	}

	memset(line, 0, sizeof(*line));
	line->type = type;
	line->data = (void *) data;
	line->dirty = 1;

	if (custom)
		view->custom_lines++;
	else
		line->lineno = lineno;

	return line;
}

static struct line *
add_line(struct view *view, const void *data, enum line_type type, size_t data_size, bool custom)
{
	return add_line_at(view, view->lines, data, type, data_size, custom);
}

static struct line *
add_line_alloc_(struct view *view, void **ptr, enum line_type type, size_t data_size, bool custom)
{
	struct line *line = add_line(view, NULL, type, data_size, custom);

	if (line)
		*ptr = line->data;
	return line;
}

#define add_line_alloc(view, data_ptr, type, extra_size, custom) \
	add_line_alloc_(view, (void **) data_ptr, type, sizeof(**data_ptr) + extra_size, custom)

static struct line *
add_line_nodata(struct view *view, enum line_type type)
{
	return add_line(view, NULL, type, 0, FALSE);
}

static struct line *
add_line_text(struct view *view, const char *text, enum line_type type)
{
	return add_line(view, text, type, strlen(text) + 1, FALSE);
}

static struct line * PRINTF_LIKE(3, 4)
add_line_format(struct view *view, enum line_type type, const char *fmt, ...)
{
	char buf[SIZEOF_STR];
	int retval;

	FORMAT_BUFFER(buf, sizeof(buf), fmt, retval, FALSE);
	return retval >= 0 ? add_line_text(view, buf, type) : NULL;
}

/*
 * View opening
 */

static void
split_view(struct view *prev, struct view *view)
{
	display[1] = view;
	current_view = opt_focus_child ? 1 : 0;
	view->parent = prev;
	resize_display();

	if (prev->pos.lineno - prev->pos.offset >= prev->height) {
		/* Take the title line into account. */
		int lines = prev->pos.lineno - prev->pos.offset - prev->height + 1;

		/* Scroll the view that was split if the current line is
		 * outside the new limited view. */
		do_scroll_view(prev, lines);
	}

	if (view != prev && view_is_displayed(prev)) {
		/* "Blur" the previous view. */
		update_view_title(prev);
	}
}

static void
maximize_view(struct view *view, bool redraw)
{
	memset(display, 0, sizeof(display));
	current_view = 0;
	display[current_view] = view;
	resize_display();
	if (redraw) {
		redraw_display(FALSE);
		report_clear();
	}
}

static void
load_view(struct view *view, struct view *prev, enum open_flags flags)
{
	if (view->pipe)
		end_update(view, TRUE);
	if (view->ops->private_size) {
		if (!view->private)
			view->private = calloc(1, view->ops->private_size);
		else
			memset(view->private, 0, view->ops->private_size);
	}

	/* When prev == view it means this is the first loaded view. */
	if (prev && view != prev) {
		view->prev = prev;
	}

	if (!view->ops->open(view, flags))
		return;

	if (prev) {
		bool split = !!(flags & OPEN_SPLIT);

		if (split) {
			split_view(prev, view);
		} else {
			maximize_view(view, FALSE);
		}
	}

	restore_view_position(view);

	if (view->pipe && view->lines == 0) {
		/* Clear the old view and let the incremental updating refill
		 * the screen. */
		werase(view->win);
		if (!(flags & (OPEN_RELOAD | OPEN_REFRESH)))
			clear_position(&view->prev_pos);
		report_clear();
	} else if (view_is_displayed(view)) {
		redraw_view(view);
		report_clear();
	}
}

#define refresh_view(view) load_view(view, NULL, OPEN_REFRESH)
#define reload_view(view) load_view(view, NULL, OPEN_RELOAD)

static void
open_view(struct view *prev, enum request request, enum open_flags flags)
{
	bool reload = !!(flags & (OPEN_RELOAD | OPEN_PREPARED));
	struct view *view = VIEW(request);
	int nviews = displayed_views();

	assert(flags ^ OPEN_REFRESH);

	if (view == prev && nviews == 1 && !reload) {
		report("Already in %s view", view->name);
		return;
	}

	if (!view_has_flags(view, VIEW_NO_GIT_DIR) && !opt_git_dir[0]) {
		report("The %s view is disabled in pager view", view->name);
		return;
	}

	load_view(view, prev ? prev : view, flags);
}

static void
open_argv(struct view *prev, struct view *view, const char *argv[], const char *dir, enum open_flags flags)
{
	enum request request = view - views + REQ_OFFSET + 1;

	if (view->pipe)
		end_update(view, TRUE);
	view->dir = dir;

	if (!argv_copy(&view->argv, argv)) {
		report("Failed to open %s view: %s", view->name, io_strerror(&view->io));
	} else {
		open_view(prev, request, flags | OPEN_PREPARED);
	}
}

static bool
open_external_viewer(const char *argv[], const char *dir, bool confirm, const char *notice)
{
	bool ok;

	def_prog_mode();           /* save current tty modes */
	endwin();                  /* restore original tty modes */
	ok = io_run_fg(argv, dir);
	if (confirm) {
		if (!ok && *notice)
			fprintf(stderr, "%s", notice);
		fprintf(stderr, "Press Enter to continue");
		getc(opt_tty);
	}
	reset_prog_mode();
	redraw_display(TRUE);
	return ok;
}

static void
open_mergetool(const char *file)
{
	const char *mergetool_argv[] = { "git", "mergetool", file, NULL };

	open_external_viewer(mergetool_argv, opt_cdup, TRUE, "");
}

#define EDITOR_LINENO_MSG \
	"*** Your editor reported an error while opening the file.\n" \
	"*** This is probably because it doesn't support the line\n" \
	"*** number argument added automatically. The line number\n" \
	"*** has been disabled for now. You can permanently disable\n" \
	"*** it by adding the following line to ~/.tigrc\n" \
	"***	set editor-line-number = no\n"

static void
open_editor(const char *file, unsigned int lineno)
{
	const char *editor_argv[SIZEOF_ARG + 3] = { "vi", file, NULL };
	char editor_cmd[SIZEOF_STR];
	char lineno_cmd[SIZEOF_STR];
	const char *editor;
	int argc = 0;

	editor = getenv("GIT_EDITOR");
	if (!editor && *opt_editor)
		editor = opt_editor;
	if (!editor)
		editor = getenv("VISUAL");
	if (!editor)
		editor = getenv("EDITOR");
	if (!editor)
		editor = "vi";

	string_ncopy(editor_cmd, editor, strlen(editor));
	if (!argv_from_string_no_quotes(editor_argv, &argc, editor_cmd)) {
		report("Failed to read editor command");
		return;
	}

	if (lineno && opt_editor_lineno && string_format(lineno_cmd, "+%u", lineno))
		editor_argv[argc++] = lineno_cmd;
	editor_argv[argc] = file;
	if (!open_external_viewer(editor_argv, opt_cdup, TRUE, EDITOR_LINENO_MSG))
		opt_editor_lineno = FALSE;
}

static enum request run_prompt_command(struct view *view, char *cmd);

static enum request
open_run_request(struct view *view, enum request request)
{
	struct run_request *req = get_run_request(request);
	const char **argv = NULL;
	bool confirmed = FALSE;

	request = REQ_NONE;

	if (!req) {
		report("Unknown run request");
		return request;
	}

	if (format_argv(view, &argv, req->argv, FALSE, TRUE)) {
		if (req->internal) {
			char cmd[SIZEOF_STR];

			if (argv_to_string(argv, cmd, sizeof(cmd), " ")) {
				request = run_prompt_command(view, cmd);
			}
		}
		else {
			confirmed = !req->confirm;

			if (req->confirm) {
				char cmd[SIZEOF_STR], prompt[SIZEOF_STR];
				const char *and_exit = req->exit ? " and exit" : "";

				if (argv_to_string(argv, cmd, sizeof(cmd), " ") &&
				    string_format(prompt, "Run `%s`%s?", cmd, and_exit) &&
				    prompt_yesno(prompt)) {
					confirmed = TRUE;
				}
			}

			if (confirmed && argv_remove_quotes(argv)) {
				if (req->silent)
					io_run_bg(argv);
				else
					open_external_viewer(argv, NULL, !req->exit, "");
			}
		}
	}

	if (argv)
		argv_free(argv);
	free(argv);

	if (request == REQ_NONE) {
		if (req->confirm && !confirmed)
			request = REQ_NONE;

		else if (req->exit)
			request = REQ_QUIT;

		else if (view_has_flags(view, VIEW_REFRESH) && !view->unrefreshable)
			request = REQ_REFRESH;
	}
	return request;
}

/*
 * User request switch noodle
 */

static int
view_driver(struct view *view, enum request request)
{
	int i;

	if (request == REQ_NONE)
		return TRUE;

	if (request >= REQ_RUN_REQUESTS) {
		request = open_run_request(view, request);

		// exit quickly rather than going through view_request and back
		if (request == REQ_QUIT)
			return FALSE;
	}

	request = view_request(view, request);
	if (request == REQ_NONE)
		return TRUE;

	switch (request) {
	case REQ_MOVE_UP:
	case REQ_MOVE_DOWN:
	case REQ_MOVE_PAGE_UP:
	case REQ_MOVE_PAGE_DOWN:
	case REQ_MOVE_FIRST_LINE:
	case REQ_MOVE_LAST_LINE:
		move_view(view, request);
		break;

	case REQ_SCROLL_FIRST_COL:
	case REQ_SCROLL_LEFT:
	case REQ_SCROLL_RIGHT:
	case REQ_SCROLL_LINE_DOWN:
	case REQ_SCROLL_LINE_UP:
	case REQ_SCROLL_PAGE_DOWN:
	case REQ_SCROLL_PAGE_UP:
	case REQ_SCROLL_WHEEL_DOWN:
	case REQ_SCROLL_WHEEL_UP:
		scroll_view(view, request);
		break;

	case REQ_VIEW_MAIN:
	case REQ_VIEW_DIFF:
	case REQ_VIEW_LOG:
	case REQ_VIEW_TREE:
	case REQ_VIEW_HELP:
	case REQ_VIEW_BRANCH:
	case REQ_VIEW_BLAME:
	case REQ_VIEW_BLOB:
	case REQ_VIEW_STATUS:
	case REQ_VIEW_STAGE:
	case REQ_VIEW_PAGER:
	case REQ_VIEW_STASH:
		open_view(view, request, OPEN_DEFAULT);
		break;

	case REQ_NEXT:
	case REQ_PREVIOUS:
		if (view->parent) {
			int line;

			view = view->parent;
			line = view->pos.lineno;
			view_request(view, request);
			move_view(view, request);
			if (view_is_displayed(view))
				update_view_title(view);
			if (line != view->pos.lineno)
				view_request(view, REQ_ENTER);
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
		report_clear();
		break;
	}
	case REQ_REFRESH:
		report("Refreshing is not supported by the %s view", view->name);
		break;

	case REQ_PARENT:
		report("Moving to parent is not supported by the the %s view", view->name);
		break;

	case REQ_BACK:
		report("Going back is not supported for by %s view", view->name);
		break;

	case REQ_MAXIMIZE:
		if (displayed_views() == 2)
			maximize_view(view, TRUE);
		break;

	case REQ_OPTIONS:
	case REQ_TOGGLE_LINENO:
	case REQ_TOGGLE_DATE:
	case REQ_TOGGLE_AUTHOR:
	case REQ_TOGGLE_FILENAME:
	case REQ_TOGGLE_GRAPHIC:
	case REQ_TOGGLE_REV_GRAPH:
	case REQ_TOGGLE_REFS:
	case REQ_TOGGLE_CHANGES:
	case REQ_TOGGLE_IGNORE_SPACE:
	case REQ_TOGGLE_ID:
	case REQ_TOGGLE_FILES:
	case REQ_TOGGLE_TITLE_OVERFLOW:
	case REQ_TOGGLE_FILE_SIZE:
	case REQ_TOGGLE_UNTRACKED_DIRS:
	case REQ_TOGGLE_VERTICAL_SPLIT:
		{
			char action[SIZEOF_STR] = "";
			enum view_flag flags = toggle_option(view, request, action);
	
			if (flags == VIEW_FLAG_RESET_DISPLAY) {
				resize_display();
				redraw_display(TRUE);
			} else {
				foreach_displayed_view(view, i) {
					if (view_has_flags(view, flags) && !view->unrefreshable)
						reload_view(view);
					else
						redraw_view(view);
				}
			}

			if (*action)
				report("%s", action);
		}
		break;

	case REQ_TOGGLE_SORT_FIELD:
	case REQ_TOGGLE_SORT_ORDER:
		report("Sorting is not yet supported for the %s view", view->name);
		break;

	case REQ_DIFF_CONTEXT_UP:
	case REQ_DIFF_CONTEXT_DOWN:
		report("Changing the diff context is not yet supported for the %s view", view->name);
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
		foreach_view(view, i) {
			if (view->pipe)
				report("Stopped loading the %s view", view->name),
			end_update(view, TRUE);
		}
		break;

	case REQ_SHOW_VERSION:
		report("tig-%s (built %s)", TIG_VERSION, __DATE__);
		return TRUE;

	case REQ_SCREEN_REDRAW:
		redraw_display(TRUE);
		break;

	case REQ_EDIT:
		report("Nothing to edit");
		break;

	case REQ_ENTER:
		report("Nothing to enter");
		break;

	case REQ_VIEW_CLOSE:
		/* XXX: Mark closed views by letting view->prev point to the
		 * view itself. Parents to closed view should never be
		 * followed. */
		if (view->prev && view->prev != view) {
			maximize_view(view->prev, TRUE);
			view->prev = view;
			break;
		}
		/* Fall-through */
	case REQ_QUIT:
		return FALSE;

	default:
		report("Unknown key, press %s for help",
		       get_view_key(view, REQ_VIEW_HELP));
		return TRUE;
	}

	return TRUE;
}


/*
 * View backend utilities
 */

enum sort_field {
	ORDERBY_NAME,
	ORDERBY_DATE,
	ORDERBY_AUTHOR,
};

struct sort_state {
	const enum sort_field *fields;
	size_t size, current;
	bool reverse;
};

#define SORT_STATE(fields) { fields, ARRAY_SIZE(fields), 0 }
#define get_sort_field(state) ((state).fields[(state).current])
#define sort_order(state, result) ((state).reverse ? -(result) : (result))

static void
sort_view(struct view *view, enum request request, struct sort_state *state,
	  int (*compare)(const void *, const void *))
{
	switch (request) {
	case REQ_TOGGLE_SORT_FIELD:
		state->current = (state->current + 1) % state->size;
		break;

	case REQ_TOGGLE_SORT_ORDER:
		state->reverse = !state->reverse;
		break;
	default:
		die("Not a sort request");
	}

	qsort(view->line, view->lines, sizeof(*view->line), compare);
	redraw_view(view);
}

static bool
update_diff_context(enum request request)
{
	int diff_context = opt_diff_context;

	switch (request) {
	case REQ_DIFF_CONTEXT_UP:
		opt_diff_context += 1;
		update_diff_context_arg(opt_diff_context);
		break;

	case REQ_DIFF_CONTEXT_DOWN:
		if (opt_diff_context == 0) {
			report("Diff context cannot be less than zero");
			break;
		}
		opt_diff_context -= 1;
		update_diff_context_arg(opt_diff_context);
		break;

	default:
		die("Not a diff context request");
	}

	return diff_context != opt_diff_context;
}

DEFINE_ALLOCATOR(realloc_paths, const char *, 256)

/* Small cache to reduce memory consumption. It uses binary search to
 * lookup or find place to position new entries. No entries are ever
 * freed. */
static const char *
get_path(const char *path)
{
	static const char **paths;
	static size_t paths_size;
	int from = 0, to = paths_size - 1;
	char *entry;

	while (from <= to) {
		size_t pos = (to + from) / 2;
		int cmp = strcmp(path, paths[pos]);

		if (!cmp)
			return paths[pos];

		if (cmp < 0)
			to = pos - 1;
		else
			from = pos + 1;
	}

	if (!realloc_paths(&paths, paths_size, 1))
		return NULL;
	entry = strdup(path);
	if (!entry)
		return NULL;

	memmove(paths + from + 1, paths + from, (paths_size - from) * sizeof(*paths));
	paths[from] = entry;
	paths_size++;

	return entry;
}

DEFINE_ALLOCATOR(realloc_authors, struct ident *, 256)

/* Small author cache to reduce memory consumption. It uses binary
 * search to lookup or find place to position new entries. No entries
 * are ever freed. */
static struct ident *
get_author(const char *name, const char *email)
{
	static struct ident **authors;
	static size_t authors_size;
	int from = 0, to = authors_size - 1;
	struct ident *ident;

	while (from <= to) {
		size_t pos = (to + from) / 2;
		int cmp = strcmp(name, authors[pos]->name);

		if (!cmp)
			return authors[pos];

		if (cmp < 0)
			to = pos - 1;
		else
			from = pos + 1;
	}

	if (!realloc_authors(&authors, authors_size, 1))
		return NULL;
	ident = calloc(1, sizeof(*ident));
	if (!ident)
		return NULL;
	ident->name = strdup(name);
	ident->email = strdup(email);
	if (!ident->name || !ident->email) {
		free((void *) ident->name);
		free(ident);
		return NULL;
	}

	memmove(authors + from + 1, authors + from, (authors_size - from) * sizeof(*authors));
	authors[from] = ident;
	authors_size++;

	return ident;
}

static void
parse_timesec(struct time *time, const char *sec)
{
	time->sec = (time_t) atol(sec);
}

static void
parse_timezone(struct time *time, const char *zone)
{
	long tz;

	tz  = ('0' - zone[1]) * 60 * 60 * 10;
	tz += ('0' - zone[2]) * 60 * 60;
	tz += ('0' - zone[3]) * 60 * 10;
	tz += ('0' - zone[4]) * 60;

	if (zone[0] == '-')
		tz = -tz;

	time->tz = tz;
	time->sec -= tz;
}

/* Parse author lines where the name may be empty:
 *	author  <email@address.tld> 1138474660 +0100
 */
static void
parse_author_line(char *ident, const struct ident **author, struct time *time)
{
	char *nameend = strchr(ident, '<');
	char *emailend = strchr(ident, '>');
	const char *name, *email = "";

	if (nameend && emailend)
		*nameend = *emailend = 0;
	name = chomp_string(ident);
	if (nameend)
		email = chomp_string(nameend + 1);
	if (!*name)
		name = *email ? email : unknown_ident.name;
	if (!*email)
		email = *name ? name : unknown_ident.email;

	*author = get_author(name, email);

	/* Parse epoch and timezone */
	if (time && emailend && emailend[1] == ' ') {
		char *secs = emailend + 2;
		char *zone = strchr(secs, ' ');

		parse_timesec(time, secs);

		if (zone && strlen(zone) == STRING_SIZE(" +0700"))
			parse_timezone(time, zone + 1);
	}
}

static struct line *
find_line_by_type(struct view *view, struct line *line, enum line_type type, int direction)
{
	for (; view_has_line(view, line); line += direction)
		if (line->type == type)
			return line;

	return NULL;
}

#define find_prev_line_by_type(view, line, type) \
	find_line_by_type(view, line, type, -1)

#define find_next_line_by_type(view, line, type) \
	find_line_by_type(view, line, type, 1)

/*
 * View history
 */

struct view_state {
	struct view_state *prev;	/* Entry below this in the stack */
	struct position position;	/* View position to restore */
	void *data;			/* View specific state */
};

struct view_history {
	size_t state_alloc;
	struct view_state *stack;
	struct position position;
};

static bool
view_history_is_empty(struct view_history *history)
{
	return !history->stack;
}

static struct view_state *
push_view_history_state(struct view_history *history, struct position *position, void *data)
{
	struct view_state *state = history->stack;

	if (state && data && history->state_alloc &&
	    !memcmp(state->data, data, history->state_alloc))
		return NULL;

	state = calloc(1, sizeof(*state) + history->state_alloc);
	if (!state)
		return NULL;

	state->prev = history->stack;
	history->stack = state;
	clear_position(&history->position);
	state->position = *position;
	state->data = &state[1];
	if (data && history->state_alloc)
		memcpy(state->data, data, history->state_alloc);
	return state;
}

static bool
pop_view_history_state(struct view_history *history, struct position *position, void *data)
{
	struct view_state *state = history->stack;

	if (view_history_is_empty(history))
		return FALSE;

	history->position = state->position;
	history->stack = state->prev;

	if (data && history->state_alloc)
		memcpy(data, state->data, history->state_alloc);
	if (position)
		*position = state->position;

	free(state);
	return TRUE;
}

static void
reset_view_history(struct view_history *history)
{
	while (pop_view_history_state(history, NULL, NULL))
		;
}

/*
 * Blame
 */

struct blame_commit {
	char id[SIZEOF_REV];		/* SHA1 ID. */
	char title[128];		/* First line of the commit message. */
	const struct ident *author;	/* Author of the commit. */
	struct time time;		/* Date from the author ident. */
	const char *filename;		/* Name of file. */
	char parent_id[SIZEOF_REV];	/* Parent/previous SHA1 ID. */
	const char *parent_filename;	/* Parent/previous name of file. */
};

struct blame_header {
	char id[SIZEOF_REV];		/* SHA1 ID. */
	size_t orig_lineno;
	size_t lineno;
	size_t group;
};

static bool
parse_number(const char **posref, size_t *number, size_t min, size_t max)
{
	const char *pos = *posref;

	*posref = NULL;
	pos = strchr(pos + 1, ' ');
	if (!pos || !isdigit(pos[1]))
		return FALSE;
	*number = atoi(pos + 1);
	if (*number < min || *number > max)
		return FALSE;

	*posref = pos;
	return TRUE;
}

static bool
parse_blame_header(struct blame_header *header, const char *text, size_t max_lineno)
{
	const char *pos = text + SIZEOF_REV - 2;

	if (strlen(text) <= SIZEOF_REV || pos[1] != ' ')
		return FALSE;

	string_ncopy(header->id, text, SIZEOF_REV);

	if (!parse_number(&pos, &header->orig_lineno, 1, 9999999) ||
	    !parse_number(&pos, &header->lineno, 1, max_lineno) ||
	    !parse_number(&pos, &header->group, 1, max_lineno - header->lineno + 1))
		return FALSE;

	return TRUE;
}

static bool
match_blame_header(const char *name, char **line)
{
	size_t namelen = strlen(name);
	bool matched = !strncmp(name, *line, namelen);

	if (matched)
		*line += namelen;

	return matched;
}

static bool
parse_blame_info(struct blame_commit *commit, char *line)
{
	if (match_blame_header("author ", &line)) {
		parse_author_line(line, &commit->author, NULL);

	} else if (match_blame_header("author-time ", &line)) {
		parse_timesec(&commit->time, line);

	} else if (match_blame_header("author-tz ", &line)) {
		parse_timezone(&commit->time, line);

	} else if (match_blame_header("summary ", &line)) {
		string_ncopy(commit->title, line, strlen(line));

	} else if (match_blame_header("previous ", &line)) {
		if (strlen(line) <= SIZEOF_REV)
			return FALSE;
		string_copy_rev(commit->parent_id, line);
		line += SIZEOF_REV;
		commit->parent_filename = get_path(line);
		if (!commit->parent_filename)
			return TRUE;

	} else if (match_blame_header("filename ", &line)) {
		commit->filename = get_path(line);
		return TRUE;
	}

	return FALSE;
}

/*
 * Pager backend
 */

static bool
pager_draw(struct view *view, struct line *line, unsigned int lineno)
{
	if (draw_lineno(view, lineno))
		return TRUE;

	if (line->wrapped && draw_text(view, LINE_DELIMITER, "+"))
		return TRUE;

	draw_text(view, line->type, line->data);
	return TRUE;
}

static bool
add_describe_ref(char *buf, size_t *bufpos, const char *commit_id, const char *sep)
{
	const char *describe_argv[] = { "git", "describe", commit_id, NULL };
	char ref[SIZEOF_STR];

	if (!io_run_buf(describe_argv, ref, sizeof(ref)) || !*ref)
		return TRUE;

	/* This is the only fatal call, since it can "corrupt" the buffer. */
	if (!string_nformat(buf, SIZEOF_STR, bufpos, "%s%s", sep, ref))
		return FALSE;

	return TRUE;
}

static void
add_pager_refs(struct view *view, const char *commit_id)
{
	char buf[SIZEOF_STR];
	struct ref_list *list;
	size_t bufpos = 0, i;
	const char *sep = "Refs: ";
	bool is_tag = FALSE;

	list = get_ref_list(commit_id);
	if (!list) {
		if (view_has_flags(view, VIEW_ADD_DESCRIBE_REF))
			goto try_add_describe_ref;
		return;
	}

	for (i = 0; i < list->size; i++) {
		struct ref *ref = list->refs[i];
		const char *fmt = ref->tag    ? "%s[%s]" :
		                  ref->remote ? "%s<%s>" : "%s%s";

		if (!string_format_from(buf, &bufpos, fmt, sep, ref->name))
			return;
		sep = ", ";
		if (ref->tag)
			is_tag = TRUE;
	}

	if (!is_tag && view_has_flags(view, VIEW_ADD_DESCRIBE_REF)) {
try_add_describe_ref:
		/* Add <tag>-g<commit_id> "fake" reference. */
		if (!add_describe_ref(buf, &bufpos, commit_id, sep))
			return;
	}

	if (bufpos == 0)
		return;

	add_line_text(view, buf, LINE_PP_REFS);
}

static struct line *
pager_wrap_line(struct view *view, const char *data, enum line_type type)
{
	size_t first_line = 0;
	bool has_first_line = FALSE;
	size_t datalen = strlen(data);
	size_t lineno = 0;

	while (datalen > 0 || !has_first_line) {
		bool wrapped = !!first_line;
		size_t linelen = string_expanded_length(data, datalen, opt_tab_size, view->width - !!wrapped);
		struct line *line;
		char *text;

		line = add_line(view, NULL, type, linelen + 1, wrapped);
		if (!line)
			break;
		if (!has_first_line) {
			first_line = view->lines - 1;
			has_first_line = TRUE;
		}

		if (!wrapped)
			lineno = line->lineno;

		line->wrapped = wrapped;
		line->lineno = lineno;
		text = line->data;
		if (linelen)
			strncpy(text, data, linelen);
		text[linelen] = 0;

		datalen -= linelen;
		data += linelen;
	}

	return has_first_line ? &view->line[first_line] : NULL;
}

static bool
pager_common_read(struct view *view, const char *data, enum line_type type)
{
	struct line *line;

	if (!data)
		return TRUE;

	if (opt_wrap_lines) {
		line = pager_wrap_line(view, data, type);
	} else {
		line = add_line_text(view, data, type);
	}

	if (!line)
		return FALSE;

	if (line->type == LINE_COMMIT && view_has_flags(view, VIEW_ADD_PAGER_REFS))
		add_pager_refs(view, data + STRING_SIZE("commit "));

	return TRUE;
}

static bool
pager_read(struct view *view, char *data)
{
	if (!data)
		return TRUE;

	return pager_common_read(view, data, get_line_type(data));
}

static enum request
pager_request(struct view *view, enum request request, struct line *line)
{
	int split = 0;

	if (request != REQ_ENTER)
		return request;

	if (line->type == LINE_COMMIT && view_has_flags(view, VIEW_OPEN_DIFF)) {
		open_view(view, REQ_VIEW_DIFF, OPEN_SPLIT);
		split = 1;
	}

	/* Always scroll the view even if it was split. That way
	 * you can use Enter to scroll through the log view and
	 * split open each commit diff. */
	scroll_view(view, REQ_SCROLL_LINE_DOWN);

	/* FIXME: A minor workaround. Scrolling the view will call report_clear()
	 * but if we are scrolling a non-current view this won't properly
	 * update the view title. */
	if (split)
		update_view_title(view);

	return REQ_NONE;
}

static bool
pager_grep(struct view *view, struct line *line)
{
	const char *text[] = { line->data, NULL };

	return grep_text(view, text);
}

static void
pager_select(struct view *view, struct line *line)
{
	if (line->type == LINE_COMMIT) {
		string_copy_rev_from_commit_line(ref_commit, line->data);
		if (!view_has_flags(view, VIEW_NO_REF))
			string_copy_rev(view->ref, ref_commit);
	}
}

struct log_state {
	/* Used for tracking when we need to recalculate the previous
	 * commit, for example when the user scrolls up or uses the page
	 * up/down in the log view. */
	int last_lineno;
	enum line_type last_type;
};

static void
log_select(struct view *view, struct line *line)
{
	struct log_state *state = view->private;
	int last_lineno = state->last_lineno;

	if (!last_lineno || abs(last_lineno - line->lineno) > 1
	    || (state->last_type == LINE_COMMIT && last_lineno > line->lineno)) {
		const struct line *commit_line = find_prev_line_by_type(view, line, LINE_COMMIT);

		if (commit_line)
			string_copy_rev_from_commit_line(view->ref, commit_line->data);
	}

	if (line->type == LINE_COMMIT && !view_has_flags(view, VIEW_NO_REF)) {
		string_copy_rev_from_commit_line(view->ref, (char *)line->data);
	}
	string_copy_rev(ref_commit, view->ref);
	state->last_lineno = line->lineno;
	state->last_type = line->type;
}

static bool
pager_open(struct view *view, enum open_flags flags)
{
	if (!open_from_stdin(flags) && !view->lines) {
		report("No pager content, press %s to run command from prompt",
			get_view_key(view, REQ_PROMPT));
		return FALSE;
	}

	return begin_update(view, NULL, NULL, flags);
}

static struct view_ops pager_ops = {
	"line",
	{ "pager" },
	VIEW_OPEN_DIFF | VIEW_NO_REF | VIEW_NO_GIT_DIR,
	0,
	pager_open,
	pager_read,
	pager_draw,
	pager_request,
	pager_grep,
	pager_select,
};

static bool
log_open(struct view *view, enum open_flags flags)
{
	static const char *log_argv[] = {
		"git", "log", encoding_arg, "--no-color", "--cc", "--stat", "-n100", "%(head)", "--", NULL
	};

	return begin_update(view, NULL, log_argv, flags);
}

static enum request
log_request(struct view *view, enum request request, struct line *line)
{
	switch (request) {
	case REQ_REFRESH:
		load_refs(TRUE);
		refresh_view(view);
		return REQ_NONE;

	case REQ_ENTER:
		if (!display[1] || strcmp(display[1]->vid, view->ref))
			open_view(view, REQ_VIEW_DIFF, OPEN_SPLIT);
		return REQ_NONE;

	default:
		return request;
	}
}

static struct view_ops log_ops = {
	"line",
	{ "log" },
	VIEW_ADD_PAGER_REFS | VIEW_OPEN_DIFF | VIEW_SEND_CHILD_ENTER | VIEW_LOG_LIKE | VIEW_REFRESH,
	sizeof(struct log_state),
	log_open,
	pager_read,
	pager_draw,
	log_request,
	pager_grep,
	log_select,
};

struct diff_state {
	bool after_commit_title;
	bool after_diff;
	bool reading_diff_stat;
	bool combined_diff;
};

#define DIFF_LINE_COMMIT_TITLE 1

static bool
diff_open(struct view *view, enum open_flags flags)
{
	static const char *diff_argv[] = {
		"git", "show", encoding_arg, "--pretty=fuller", "--root",
			"--patch-with-stat",
			opt_notes_arg, opt_diff_context_arg, opt_ignore_space_arg,
			"%(diffargs)", "%(cmdlineargs)", "--no-color", "%(commit)",
			"--", "%(fileargs)", NULL
	};

	return begin_update(view, NULL, diff_argv, flags);
}

static bool
diff_common_read(struct view *view, const char *data, struct diff_state *state)
{
	enum line_type type = get_line_type(data);

	if (!view->lines && type != LINE_COMMIT)
		state->reading_diff_stat = TRUE;

	if (state->combined_diff && !state->after_diff && data[0] == ' ' && data[1] != ' ')
		state->reading_diff_stat = TRUE;

	if (state->reading_diff_stat) {
		size_t len = strlen(data);
		char *pipe = strchr(data, '|');
		bool has_histogram = data[len - 1] == '-' || data[len - 1] == '+';
		bool has_bin_diff = pipe && strstr(pipe, "Bin") && strstr(pipe, "->");
		bool has_rename = data[len - 1] == '0' && (strstr(data, "=>") || !strncmp(data, " ...", 4));
		bool has_no_change = pipe && strstr(pipe, " 0");

		if (pipe && (has_histogram || has_bin_diff || has_rename || has_no_change)) {
			return add_line_text(view, data, LINE_DIFF_STAT) != NULL;
		} else {
			state->reading_diff_stat = FALSE;
		}

	} else if (!strcmp(data, "---")) {
		state->reading_diff_stat = TRUE;
	}

	if (!state->after_commit_title && !prefixcmp(data, "    ")) {
		struct line *line = add_line_text(view, data, LINE_DEFAULT);

		if (line)
			line->user_flags |= DIFF_LINE_COMMIT_TITLE;
		state->after_commit_title = TRUE;
		return line != NULL;
	}

	if (type == LINE_DIFF_HEADER) {
		const int len = line_info[LINE_DIFF_HEADER].linelen;

		state->after_diff = TRUE;
		if (!strncmp(data + len, "combined ", strlen("combined ")) ||
		    !strncmp(data + len, "cc ", strlen("cc ")))
			state->combined_diff = TRUE;

	} else if (type == LINE_PP_MERGE) {
		state->combined_diff = TRUE;
	}

	/* ADD2 and DEL2 are only valid in combined diff hunks */
	if (!state->combined_diff && (type == LINE_DIFF_ADD2 || type == LINE_DIFF_DEL2))
		type = LINE_DEFAULT;

	return pager_common_read(view, data, type);
}

static bool
diff_find_stat_entry(struct view *view, struct line *line, enum line_type type)
{
	struct line *marker = find_next_line_by_type(view, line, type);

	return marker &&
		line == find_prev_line_by_type(view, marker, LINE_DIFF_HEADER);
}

static enum request
diff_common_enter(struct view *view, enum request request, struct line *line)
{
	if (line->type == LINE_DIFF_STAT) {
		int file_number = 0;

		while (view_has_line(view, line) && line->type == LINE_DIFF_STAT) {
			file_number++;
			line--;
		}

		for (line = view->line; view_has_line(view, line); line++) {
			line = find_next_line_by_type(view, line, LINE_DIFF_HEADER);
			if (!line)
				break;

			if (diff_find_stat_entry(view, line, LINE_DIFF_INDEX)
			    || diff_find_stat_entry(view, line, LINE_DIFF_SIMILARITY)) {
				if (file_number == 1) {
					break;
				}
				file_number--;
			}
		}

		if (!line) {
			report("Failed to find file diff");
			return REQ_NONE;
		}

		select_view_line(view, line - view->line);
		report_clear();
		return REQ_NONE;

	} else {
		return pager_request(view, request, line);
	}
}

static bool
diff_common_draw_part(struct view *view, enum line_type *type, char **text, char c, enum line_type next_type)
{
	char *sep = strchr(*text, c);

	if (sep != NULL) {
		*sep = 0;
		draw_text(view, *type, *text);
		*sep = c;
		*text = sep;
		*type = next_type;
	}

	return sep != NULL;
}

static bool
diff_common_draw(struct view *view, struct line *line, unsigned int lineno)
{
	char *text = line->data;
	enum line_type type = line->type;

	if (draw_lineno(view, lineno))
		return TRUE;

	if (line->wrapped && draw_text(view, LINE_DELIMITER, "+"))
		return TRUE;

	if (type == LINE_DIFF_STAT) {
		diff_common_draw_part(view, &type, &text, '|', LINE_DEFAULT);
		if (diff_common_draw_part(view, &type, &text, 'B', LINE_DEFAULT)) {
			/* Handle binary diffstat: Bin <deleted> -> <added> bytes */
			diff_common_draw_part(view, &type, &text, ' ', LINE_DIFF_DEL);
			diff_common_draw_part(view, &type, &text, '-', LINE_DEFAULT);
			diff_common_draw_part(view, &type, &text, ' ', LINE_DIFF_ADD);
			diff_common_draw_part(view, &type, &text, 'b', LINE_DEFAULT);

		} else {
			diff_common_draw_part(view, &type, &text, '+', LINE_DIFF_ADD);
			diff_common_draw_part(view, &type, &text, '-', LINE_DIFF_DEL);
		}
	}

	if (line->user_flags & DIFF_LINE_COMMIT_TITLE)
		draw_commit_title(view, text, 4);
	else
		draw_text(view, type, text);
	return TRUE;
}

static bool
diff_read(struct view *view, char *data)
{
	struct diff_state *state = view->private;

	if (!data) {
		/* Fall back to retry if no diff will be shown. */
		if (view->lines == 0 && opt_file_argv) {
			int pos = argv_size(view->argv)
				- argv_size(opt_file_argv) - 1;

			if (pos > 0 && !strcmp(view->argv[pos], "--")) {
				for (; view->argv[pos]; pos++) {
					free((void *) view->argv[pos]);
					view->argv[pos] = NULL;
				}

				if (view->pipe)
					io_done(view->pipe);
				if (io_run(&view->io, IO_RD, view->dir, opt_env, view->argv))
					return FALSE;
			}
		}
		return TRUE;
	}

	return diff_common_read(view, data, state);
}

static bool
diff_blame_line(const char *ref, const char *file, unsigned long lineno,
		struct blame_header *header, struct blame_commit *commit)
{
	char line_arg[SIZEOF_STR];
	const char *blame_argv[] = {
		"git", "blame", encoding_arg, "-p", line_arg, ref, "--", file, NULL
	};
	struct io io;
	bool ok = FALSE;
	char *buf;

	if (!string_format(line_arg, "-L%ld,+1", lineno))
		return FALSE;

	if (!io_run(&io, IO_RD, opt_cdup, opt_env, blame_argv))
		return FALSE;

	while ((buf = io_get(&io, '\n', TRUE))) {
		if (header) {
			if (!parse_blame_header(header, buf, 9999999))
				break;
			header = NULL;

		} else if (parse_blame_info(commit, buf)) {
			ok = commit->filename != NULL;
			break;
		}
	}

	if (io_error(&io))
		ok = FALSE;

	io_done(&io);
	return ok;
}

struct chunk_header_position {
	unsigned long position;
	unsigned long lines;
};

struct chunk_header {
	struct chunk_header_position old;
	struct chunk_header_position new;
};

static bool
parse_ulong(const char **pos_ptr, unsigned long *value, const char *skip)
{
	const char *start = *pos_ptr;
	char *end;

	if (!isdigit(*start))
		return 0;

	*value = strtoul(start, &end, 10);
	if (end == start)
		return FALSE;

	start = end;
	while (skip && *start && strchr(skip, *start))
		start++;
	*pos_ptr = start;
	return TRUE;
}

static bool
parse_chunk_header(struct chunk_header *header, const char *line)
{
	memset(header, 0, sizeof(*header));

	if (prefixcmp(line, "@@ -"))
		return FALSE;

	line += STRING_SIZE("@@ -");

	return  parse_ulong(&line, &header->old.position, ",") &&
		parse_ulong(&line, &header->old.lines, " +") &&
		parse_ulong(&line, &header->new.position, ",") &&
		parse_ulong(&line, &header->new.lines, NULL);
}

static unsigned int
diff_get_lineno(struct view *view, struct line *line)
{
	const struct line *header, *chunk;
	unsigned int lineno;
	struct chunk_header chunk_header;

	/* Verify that we are after a diff header and one of its chunks */
	header = find_prev_line_by_type(view, line, LINE_DIFF_HEADER);
	chunk = find_prev_line_by_type(view, line, LINE_DIFF_CHUNK);
	if (!header || !chunk || chunk < header)
		return 0;

	/*
	 * In a chunk header, the number after the '+' sign is the number of its
	 * following line, in the new version of the file. We increment this
	 * number for each non-deletion line, until the given line position.
	 */
	if (!parse_chunk_header(&chunk_header, chunk->data))
		return 0;

	lineno = chunk_header.new.position;
	chunk++;
	while (chunk++ < line)
		if (chunk->type != LINE_DIFF_DEL)
			lineno++;

	return lineno;
}

static bool
parse_chunk_lineno(unsigned long *lineno, const char *chunk, int marker)
{
	struct chunk_header chunk_header;

	*lineno = 0;

	if (!parse_chunk_header(&chunk_header, chunk))
		return FALSE;

	*lineno = marker == '-' ? chunk_header.old.position : chunk_header.new.position;
	return TRUE;
}

static enum request
diff_trace_origin(struct view *view, struct line *line)
{
	struct line *diff = find_prev_line_by_type(view, line, LINE_DIFF_HEADER);
	struct line *chunk = find_prev_line_by_type(view, line, LINE_DIFF_CHUNK);
	const char *chunk_data;
	int chunk_marker = line->type == LINE_DIFF_DEL ? '-' : '+';
	unsigned long lineno = 0;
	const char *file = NULL;
	char ref[SIZEOF_REF];
	struct blame_header header;
	struct blame_commit commit;

	if (!diff || !chunk || chunk == line) {
		report("The line to trace must be inside a diff chunk");
		return REQ_NONE;
	}

	for (; diff < line && !file; diff++) {
		const char *data = diff->data;

		if (!prefixcmp(data, "--- a/")) {
			file = data + STRING_SIZE("--- a/");
			break;
		}
	}

	if (diff == line || !file) {
		report("Failed to read the file name");
		return REQ_NONE;
	}

	chunk_data = chunk->data;

	if (!parse_chunk_lineno(&lineno, chunk_data, chunk_marker)) {
		report("Failed to read the line number");
		return REQ_NONE;
	}

	if (lineno == 0) {
		report("This is the origin of the line");
		return REQ_NONE;
	}

	for (chunk += 1; chunk < line; chunk++) {
		if (chunk->type == LINE_DIFF_ADD) {
			lineno += chunk_marker == '+';
		} else if (chunk->type == LINE_DIFF_DEL) {
			lineno += chunk_marker == '-';
		} else {
			lineno++;
		}
	}

	if (chunk_marker == '+')
		string_copy(ref, view->vid);
	else
		string_format(ref, "%s^", view->vid);

	if (!diff_blame_line(ref, file, lineno, &header, &commit)) {
		report("Failed to read blame data");
		return REQ_NONE;
	}

	string_ncopy(opt_file, commit.filename, strlen(commit.filename));
	string_copy(opt_ref, header.id);
	opt_goto_line = header.orig_lineno - 1;

	return REQ_VIEW_BLAME;
}

static const char *
diff_get_pathname(struct view *view, struct line *line)
{
	const struct line *header;
	const char *dst = NULL;
	const char *prefixes[] = { " b/", "cc ", "combined " };
	int i;

	header = find_prev_line_by_type(view, line, LINE_DIFF_HEADER);
	if (!header)
		return NULL;

	for (i = 0; i < ARRAY_SIZE(prefixes) && !dst; i++)
		dst = strstr(header->data, prefixes[i]);

	return dst ? dst + strlen(prefixes[--i]) : NULL;
}

static enum request
diff_common_edit(struct view *view, enum request request, struct line *line)
{
	const char *file = diff_get_pathname(view, line);
	char path[SIZEOF_STR];
	bool has_path = file && string_format(path, "%s%s", opt_cdup, file);

	if (has_path && access(path, R_OK)) {
		report("Failed to open file: %s", file);
		return REQ_NONE;
	}

	open_editor(file, diff_get_lineno(view, line));
	return REQ_NONE;
}

static enum request
diff_request(struct view *view, enum request request, struct line *line)
{
	switch (request) {
	case REQ_VIEW_BLAME:
		return diff_trace_origin(view, line);

	case REQ_DIFF_CONTEXT_UP:
	case REQ_DIFF_CONTEXT_DOWN:
		if (!update_diff_context(request))
			return REQ_NONE;
		reload_view(view);
		return REQ_NONE;


	case REQ_EDIT:
		return diff_common_edit(view, request, line);

	case REQ_ENTER:
		return diff_common_enter(view, request, line);

	case REQ_REFRESH:
		if (string_rev_is_null(view->vid))
			refresh_view(view);
		else
			reload_view(view);
		return REQ_NONE;

	default:
		return pager_request(view, request, line);
	}
}

static void
diff_select(struct view *view, struct line *line)
{
	if (line->type == LINE_DIFF_STAT) {
		string_format(view->ref, "Press '%s' to jump to file diff",
			      get_view_key(view, REQ_ENTER));
	} else {
		const char *file = diff_get_pathname(view, line);

		if (file) {
			string_format(view->ref, "Changes to '%s'", file);
			string_format(opt_file, "%s", file);
			ref_blob[0] = 0;
		} else {
			string_ncopy(view->ref, view->id, strlen(view->id));
			pager_select(view, line);
		}
	}
}

static struct view_ops diff_ops = {
	"line",
	{ "diff" },
	VIEW_DIFF_LIKE | VIEW_ADD_DESCRIBE_REF | VIEW_ADD_PAGER_REFS | VIEW_FILE_FILTER | VIEW_REFRESH,
	sizeof(struct diff_state),
	diff_open,
	diff_read,
	diff_common_draw,
	diff_request,
	pager_grep,
	diff_select,
};

/*
 * Help backend
 */

static bool
help_draw(struct view *view, struct line *line, unsigned int lineno)
{
	if (line->type == LINE_HELP_KEYMAP) {
		struct keymap *keymap = line->data;

		draw_formatted(view, line->type, "[%c] %s bindings",
			       keymap->hidden ? '+' : '-', keymap->name);
		return TRUE;
	} else {
		return pager_draw(view, line, lineno);
	}
}

static bool
help_open_keymap_title(struct view *view, struct keymap *keymap)
{
	add_line(view, keymap, LINE_HELP_KEYMAP, 0, FALSE);
	return keymap->hidden;
}

static void
help_open_keymap(struct view *view, struct keymap *keymap)
{
	const char *group = NULL;
	char buf[SIZEOF_STR];
	bool add_title = TRUE;
	int i;

	for (i = 0; i < ARRAY_SIZE(req_info); i++) {
		const char *key = NULL;

		if (req_info[i].request == REQ_NONE)
			continue;

		if (!req_info[i].request) {
			group = req_info[i].help;
			continue;
		}

		key = get_keys(keymap, req_info[i].request, TRUE);
		if (!key || !*key)
			continue;

		if (add_title && help_open_keymap_title(view, keymap))
			return;
		add_title = FALSE;

		if (group) {
			add_line_text(view, group, LINE_HELP_GROUP);
			group = NULL;
		}

		add_line_format(view, LINE_DEFAULT, "    %-25s %-20s %s", key,
				enum_name(req_info[i]), req_info[i].help);
	}

	group = "External commands:";

	for (i = 0; i < run_requests; i++) {
		struct run_request *req = get_run_request(REQ_NONE + i + 1);
		const char *key;

		if (!req || req->keymap != keymap)
			continue;

		key = get_key_name(req->key);
		if (!*key)
			key = "(no key defined)";

		if (add_title && help_open_keymap_title(view, keymap))
			return;
		add_title = FALSE;

		if (group) {
			add_line_text(view, group, LINE_HELP_GROUP);
			group = NULL;
		}

		if (!argv_to_string(req->argv, buf, sizeof(buf), " "))
			return;

		add_line_format(view, LINE_DEFAULT, "    %-25s `%s`", key, buf);
	}
}

static bool
help_open(struct view *view, enum open_flags flags)
{
	struct keymap *keymap;

	reset_view(view);
	add_line_text(view, "Quick reference for tig keybindings:", LINE_DEFAULT);
	add_line_text(view, "", LINE_DEFAULT);

	for (keymap = keymaps; keymap; keymap = keymap->next)
		help_open_keymap(view, keymap);

	return TRUE;
}

static enum request
help_request(struct view *view, enum request request, struct line *line)
{
	switch (request) {
	case REQ_ENTER:
		if (line->type == LINE_HELP_KEYMAP) {
			struct keymap *keymap = line->data;

			keymap->hidden = !keymap->hidden;
			refresh_view(view);
		}

		return REQ_NONE;
	default:
		return pager_request(view, request, line);
	}
}

static void
help_done(struct view *view)
{
	int i;

	for (i = 0; i < view->lines; i++)
		if (view->line[i].type == LINE_HELP_KEYMAP)
			view->line[i].data = NULL;
}

static struct view_ops help_ops = {
	"line",
	{ "help" },
	VIEW_NO_GIT_DIR,
	0,
	help_open,
	NULL,
	help_draw,
	help_request,
	pager_grep,
	pager_select,
	help_done,
};


/*
 * Tree backend
 */

/* The top of the path stack. */
static struct view_history tree_view_history = { sizeof(char *) };

static void
pop_tree_stack_entry(struct position *position)
{
	char *path_position = NULL;

	pop_view_history_state(&tree_view_history, position, &path_position);
	path_position[0] = 0;
}

static void
push_tree_stack_entry(const char *name, struct position *position)
{
	size_t pathlen = strlen(opt_path);
	char *path_position = opt_path + pathlen;
	struct view_state *state = push_view_history_state(&tree_view_history, position, &path_position);

	if (!state)
		return;

	if (!string_format_from(opt_path, &pathlen, "%s/", name)) {
		pop_tree_stack_entry(NULL);
		return;
	}

	clear_position(position);
}

/* Parse output from git-ls-tree(1):
 *
 * 100644 blob 95925677ca47beb0b8cce7c0e0011bcc3f61470f  213045	tig.c
 */

#define SIZEOF_TREE_ATTR \
	STRING_SIZE("100644 blob f931e1d229c3e185caad4449bf5b66ed72462657\t")

#define SIZEOF_TREE_MODE \
	STRING_SIZE("100644 ")

#define TREE_ID_OFFSET \
	STRING_SIZE("100644 blob ")

#define tree_path_is_parent(path)	(!strcmp("..", (path)))

struct tree_entry {
	char id[SIZEOF_REV];
	char commit[SIZEOF_REV];
	mode_t mode;
	struct time time;		/* Date from the author ident. */
	const struct ident *author;	/* Author of the commit. */
	unsigned long size;
	char name[1];
};

struct tree_state {
	char commit[SIZEOF_REV];
	const struct ident *author;
	struct time author_time;
	int size_width;
	bool read_date;
};

static const char *
tree_path(const struct line *line)
{
	return ((struct tree_entry *) line->data)->name;
}

static int
tree_compare_entry(const struct line *line1, const struct line *line2)
{
	if (line1->type != line2->type)
		return line1->type == LINE_TREE_DIR ? -1 : 1;
	return strcmp(tree_path(line1), tree_path(line2));
}

static const enum sort_field tree_sort_fields[] = {
	ORDERBY_NAME, ORDERBY_DATE, ORDERBY_AUTHOR
};
static struct sort_state tree_sort_state = SORT_STATE(tree_sort_fields);

static int
tree_compare(const void *l1, const void *l2)
{
	const struct line *line1 = (const struct line *) l1;
	const struct line *line2 = (const struct line *) l2;
	const struct tree_entry *entry1 = ((const struct line *) l1)->data;
	const struct tree_entry *entry2 = ((const struct line *) l2)->data;

	if (line1->type == LINE_TREE_HEAD)
		return -1;
	if (line2->type == LINE_TREE_HEAD)
		return 1;

	switch (get_sort_field(tree_sort_state)) {
	case ORDERBY_DATE:
		return sort_order(tree_sort_state, timecmp(&entry1->time, &entry2->time));

	case ORDERBY_AUTHOR:
		return sort_order(tree_sort_state, ident_compare(entry1->author, entry2->author));

	case ORDERBY_NAME:
	default:
		return sort_order(tree_sort_state, tree_compare_entry(line1, line2));
	}
}


static struct line *
tree_entry(struct view *view, enum line_type type, const char *path,
	   const char *mode, const char *id, unsigned long size)
{
	bool custom = type == LINE_TREE_HEAD || tree_path_is_parent(path);
	struct tree_entry *entry;
	struct line *line = add_line_alloc(view, &entry, type, strlen(path), custom);

	if (!line)
		return NULL;

	strncpy(entry->name, path, strlen(path));
	if (mode)
		entry->mode = strtoul(mode, NULL, 8);
	if (id)
		string_copy_rev(entry->id, id);
	entry->size = size;

	return line;
}

static bool
tree_read_date(struct view *view, char *text, struct tree_state *state)
{
	if (!text && state->read_date) {
		state->read_date = FALSE;
		return TRUE;

	} else if (!text) {
		/* Find next entry to process */
		const char *log_file[] = {
			"git", "log", encoding_arg, "--no-color", "--pretty=raw",
				"--cc", "--raw", view->id, "--", "%(directory)", NULL
		};

		if (!view->lines) {
			tree_entry(view, LINE_TREE_HEAD, opt_path, NULL, NULL, 0);
			tree_entry(view, LINE_TREE_DIR, "..", "040000", view->ref, 0);
			report("Tree is empty");
			return TRUE;
		}

		if (!begin_update(view, opt_cdup, log_file, OPEN_EXTRA)) {
			report("Failed to load tree data");
			return TRUE;
		}

		state->read_date = TRUE;
		return FALSE;

	} else if (*text == 'c' && get_line_type(text) == LINE_COMMIT) {
		string_copy_rev_from_commit_line(state->commit, text);

	} else if (*text == 'a' && get_line_type(text) == LINE_AUTHOR) {
		parse_author_line(text + STRING_SIZE("author "),
				  &state->author, &state->author_time);

	} else if (*text == ':') {
		char *pos;
		size_t annotated = 1;
		size_t i;

		pos = strchr(text, '\t');
		if (!pos)
			return TRUE;
		text = pos + 1;
		if (*opt_path && !strncmp(text, opt_path, strlen(opt_path)))
			text += strlen(opt_path);
		pos = strchr(text, '/');
		if (pos)
			*pos = 0;

		for (i = 1; i < view->lines; i++) {
			struct line *line = &view->line[i];
			struct tree_entry *entry = line->data;

			annotated += !!entry->author;
			if (entry->author || strcmp(entry->name, text))
				continue;

			string_copy_rev(entry->commit, state->commit);
			entry->author = state->author;
			entry->time = state->author_time;
			line->dirty = 1;
			break;
		}

		if (annotated == view->lines)
			io_kill(view->pipe);
	}
	return TRUE;
}

static inline size_t
parse_size(const char *text, int *max_digits)
{
	size_t size = 0;
	int digits = 0;

	while (*text == ' ')
		text++;

	while (isdigit(*text)) {
		size = (size * 10) + (*text++ - '0');
		digits++;
	}

	if (digits > *max_digits)
		*max_digits = digits;

	return size;
}

static bool
tree_read(struct view *view, char *text)
{
	struct tree_state *state = view->private;
	struct tree_entry *data;
	struct line *entry, *line;
	enum line_type type;
	size_t textlen = text ? strlen(text) : 0;
	const char *attr_offset = text + SIZEOF_TREE_ATTR;
	char *path;
	size_t size;

	if (state->read_date || !text)
		return tree_read_date(view, text, state);

	if (textlen <= SIZEOF_TREE_ATTR)
		return FALSE;
	if (view->lines == 0 &&
	    !tree_entry(view, LINE_TREE_HEAD, opt_path, NULL, NULL, 0))
		return FALSE;

	size = parse_size(attr_offset, &state->size_width);
	path = strchr(attr_offset, '\t');
	if (!path)
		return FALSE;
	path++;

	/* Strip the path part ... */
	if (*opt_path) {
		size_t pathlen = textlen - SIZEOF_TREE_ATTR;
		size_t striplen = strlen(opt_path);

		if (pathlen > striplen)
			memmove(path, path + striplen,
				pathlen - striplen + 1);

		/* Insert "link" to parent directory. */
		if (view->lines == 1 &&
		    !tree_entry(view, LINE_TREE_DIR, "..", "040000", view->ref, 0))
			return FALSE;
	}

	type = text[SIZEOF_TREE_MODE] == 't' ? LINE_TREE_DIR : LINE_TREE_FILE;
	entry = tree_entry(view, type, path, text, text + TREE_ID_OFFSET, size);
	if (!entry)
		return FALSE;
	data = entry->data;

	/* Skip "Directory ..." and ".." line. */
	for (line = &view->line[1 + !!*opt_path]; line < entry; line++) {
		if (tree_compare_entry(line, entry) <= 0)
			continue;

		memmove(line + 1, line, (entry - line) * sizeof(*entry));

		line->data = data;
		line->type = type;
		for (; line <= entry; line++)
			line->dirty = line->cleareol = 1;
		return TRUE;
	}

	/* Move the current line to the first tree entry. */
	if (!check_position(&view->prev_pos) && !check_position(&view->pos))
		goto_view_line(view, 0, 1);

	return TRUE;
}

static bool
tree_draw(struct view *view, struct line *line, unsigned int lineno)
{
	struct tree_state *state = view->private;
	struct tree_entry *entry = line->data;

	if (line->type == LINE_TREE_HEAD) {
		if (draw_text(view, line->type, "Directory path /"))
			return TRUE;
	} else {
		if (draw_mode(view, entry->mode))
			return TRUE;

		if (draw_author(view, entry->author))
			return TRUE;

		if (draw_file_size(view, entry->size, state->size_width,
				   line->type != LINE_TREE_FILE))
			return TRUE;

		if (draw_date(view, &entry->time))
			return TRUE;

		if (draw_id(view, entry->commit))
			return TRUE;
	}

	draw_text(view, line->type, entry->name);
	return TRUE;
}

static void
open_blob_editor(const char *id, const char *name, unsigned int lineno)
{
	const char *blob_argv[] = { "git", "cat-file", "blob", id, NULL };
	char file[SIZEOF_STR];
	int fd;

	if (!name)
		name = "unknown";

	if (!string_format(file, "%s/tigblob.XXXXXX.%s", get_temp_dir(), name)) {
		report("Temporary file name is too long");
		return;
	}

	fd = mkstemps(file, strlen(name) + 1);

	if (fd == -1)
		report("Failed to create temporary file");
	else if (!io_run_append(blob_argv, fd))
		report("Failed to save blob data to file");
	else
		open_editor(file, lineno);
	if (fd != -1)
		unlink(file);
}

static enum request
tree_request(struct view *view, enum request request, struct line *line)
{
	enum open_flags flags;
	struct tree_entry *entry = line->data;

	switch (request) {
	case REQ_VIEW_BLAME:
		if (line->type != LINE_TREE_FILE) {
			report("Blame only supported for files");
			return REQ_NONE;
		}

		string_copy(opt_ref, view->vid);
		return request;

	case REQ_EDIT:
		if (line->type != LINE_TREE_FILE) {
			report("Edit only supported for files");
		} else if (!is_head_commit(view->vid)) {
			open_blob_editor(entry->id, entry->name, 0);
		} else {
			open_editor(opt_file, 0);
		}
		return REQ_NONE;

	case REQ_TOGGLE_SORT_FIELD:
	case REQ_TOGGLE_SORT_ORDER:
		sort_view(view, request, &tree_sort_state, tree_compare);
		return REQ_NONE;

	case REQ_PARENT:
	case REQ_BACK:
		if (!*opt_path) {
			/* quit view if at top of tree */
			return REQ_VIEW_CLOSE;
		}
		/* fake 'cd  ..' */
		line = &view->line[1];
		break;

	case REQ_ENTER:
		break;

	default:
		return request;
	}

	/* Cleanup the stack if the tree view is at a different tree. */
	if (!*opt_path)
		reset_view_history(&tree_view_history);

	switch (line->type) {
	case LINE_TREE_DIR:
		/* Depending on whether it is a subdirectory or parent link
		 * mangle the path buffer. */
		if (line == &view->line[1] && *opt_path) {
			pop_tree_stack_entry(&view->pos);

		} else {
			const char *basename = tree_path(line);

			push_tree_stack_entry(basename, &view->pos);
		}

		/* Trees and subtrees share the same ID, so they are not not
		 * unique like blobs. */
		flags = OPEN_RELOAD;
		request = REQ_VIEW_TREE;
		break;

	case LINE_TREE_FILE:
		flags = view_is_displayed(view) ? OPEN_SPLIT : OPEN_DEFAULT;
		request = REQ_VIEW_BLOB;
		break;

	default:
		return REQ_NONE;
	}

	open_view(view, request, flags);

	return REQ_NONE;
}

static bool
tree_grep(struct view *view, struct line *line)
{
	struct tree_entry *entry = line->data;
	const char *text[] = {
		entry->name,
		mkauthor(entry->author, opt_author_width, opt_author),
		mkdate(&entry->time, opt_date),
		NULL
	};

	return grep_text(view, text);
}

static void
tree_select(struct view *view, struct line *line)
{
	struct tree_entry *entry = line->data;

	if (line->type == LINE_TREE_HEAD) {
		string_format(view->ref, "Files in /%s", opt_path);
		return;
	}

	if (line->type == LINE_TREE_DIR && tree_path_is_parent(entry->name)) {
		string_copy(view->ref, "Open parent directory");
		ref_blob[0] = 0;
		return;
	}

	if (line->type == LINE_TREE_FILE) {
		string_copy_rev(ref_blob, entry->id);
		string_format(opt_file, "%s%s", opt_path, tree_path(line));
	}

	string_copy_rev(view->ref, entry->id);
}

static bool
tree_open(struct view *view, enum open_flags flags)
{
	static const char *tree_argv[] = {
		"git", "ls-tree", "-l", "%(commit)", "%(directory)", NULL
	};

	if (string_rev_is_null(ref_commit)) {
		report("No tree exists for this commit");
		return FALSE;
	}

	if (view->lines == 0 && opt_prefix[0]) {
		char *pos = opt_prefix;

		while (pos && *pos) {
			char *end = strchr(pos, '/');

			if (end)
				*end = 0;
			push_tree_stack_entry(pos, &view->pos);
			pos = end;
			if (end) {
				*end = '/';
				pos++;
			}
		}

	} else if (strcmp(view->vid, view->id)) {
		opt_path[0] = 0;
	}

	return begin_update(view, opt_cdup, tree_argv, flags);
}

static struct view_ops tree_ops = {
	"file",
	{ "tree" },
	VIEW_SEND_CHILD_ENTER,
	sizeof(struct tree_state),
	tree_open,
	tree_read,
	tree_draw,
	tree_request,
	tree_grep,
	tree_select,
};

static bool
blob_open(struct view *view, enum open_flags flags)
{
	static const char *blob_argv[] = {
		"git", "cat-file", "blob", "%(blob)", NULL
	};

	if (!ref_blob[0] && opt_file[0]) {
		const char *commit = ref_commit[0] ? ref_commit : "HEAD";
		char blob_spec[SIZEOF_STR];
		const char *rev_parse_argv[] = {
			"git", "rev-parse", blob_spec, NULL
		};

		if (!string_format(blob_spec, "%s:%s", commit, opt_file) ||
		    !io_run_buf(rev_parse_argv, ref_blob, sizeof(ref_blob))) {
			report("Failed to resolve blob from file name");
			return FALSE;
		}
	}

	if (!ref_blob[0]) {
		report("No file chosen, press %s to open tree view",
			get_view_key(view, REQ_VIEW_TREE));
		return FALSE;
	}

	view->encoding = get_path_encoding(opt_file, default_encoding);

	return begin_update(view, NULL, blob_argv, flags);
}

static bool
blob_read(struct view *view, char *line)
{
	if (!line)
		return TRUE;
	return add_line_text(view, line, LINE_DEFAULT) != NULL;
}

static enum request
blob_request(struct view *view, enum request request, struct line *line)
{
	switch (request) {
	case REQ_VIEW_BLAME:
		if (view->parent)
			string_copy(opt_ref, view->parent->vid);
		return request;

	case REQ_EDIT:
		open_blob_editor(view->vid, NULL, (line - view->line) + 1);
		return REQ_NONE;
	default:
		return pager_request(view, request, line);
	}
}

static struct view_ops blob_ops = {
	"line",
	{ "blob" },
	VIEW_NO_FLAGS,
	0,
	blob_open,
	blob_read,
	pager_draw,
	blob_request,
	pager_grep,
	pager_select,
};

/*
 * Blame backend
 *
 * Loading the blame view is a two phase job:
 *
 *  1. File content is read either using opt_file from the
 *     filesystem or using git-cat-file.
 *  2. Then blame information is incrementally added by
 *     reading output from git-blame.
 */

struct blame_history_state {
	char id[SIZEOF_REV];		/* SHA1 ID. */
	const char *filename;		/* Name of file. */
};

static struct view_history blame_view_history = { sizeof(struct blame_history_state) };

struct blame {
	struct blame_commit *commit;
	unsigned long lineno;
	char text[1];
};

struct blame_state {
	struct blame_commit *commit;
	int blamed;
	bool done_reading;
	bool auto_filename_display;
	/* The history state for the current view is cached in the view
	 * state so it always matches what was used to load the current blame
	 * view. */
	struct blame_history_state history_state;
};

static bool
blame_detect_filename_display(struct view *view)
{
	bool show_filenames = FALSE;
	const char *filename = NULL;
	int i;

	if (opt_blame_argv) {
		for (i = 0; opt_blame_argv[i]; i++) {
			if (prefixcmp(opt_blame_argv[i], "-C"))
				continue;

			show_filenames = TRUE;
		}
	}

	for (i = 0; i < view->lines; i++) {
		struct blame *blame = view->line[i].data;

		if (blame->commit && blame->commit->id[0]) {
			if (!filename)
				filename = blame->commit->filename;
			else if (strcmp(filename, blame->commit->filename))
				show_filenames = TRUE;
		}
	}

	return show_filenames;
}

static bool
blame_open(struct view *view, enum open_flags flags)
{
	struct blame_state *state = view->private;
	const char *file_argv[] = { opt_cdup, opt_file , NULL };
	char path[SIZEOF_STR];
	size_t i;

	if (!opt_file[0]) {
		report("No file chosen, press %s to open tree view",
			get_view_key(view, REQ_VIEW_TREE));
		return FALSE;
	}

	if (!view->prev && *opt_prefix && !(flags & (OPEN_RELOAD | OPEN_REFRESH))) {
		string_copy(path, opt_file);
		if (!string_format(opt_file, "%s%s", opt_prefix, path)) {
			report("Failed to setup the blame view");
			return FALSE;
		}
	}

	if (*opt_ref || !begin_update(view, opt_cdup, file_argv, flags)) {
		const char *blame_cat_file_argv[] = {
			"git", "cat-file", "blob", "%(ref):%(file)", NULL
		};

		if (!begin_update(view, opt_cdup, blame_cat_file_argv, flags))
			return FALSE;
	}

	/* First pass: remove multiple references to the same commit. */
	for (i = 0; i < view->lines; i++) {
		struct blame *blame = view->line[i].data;

		if (blame->commit && blame->commit->id[0])
			blame->commit->id[0] = 0;
		else
			blame->commit = NULL;
	}

	/* Second pass: free existing references. */
	for (i = 0; i < view->lines; i++) {
		struct blame *blame = view->line[i].data;

		if (blame->commit)
			free(blame->commit);
	}

	if (!(flags & OPEN_RELOAD))
		reset_view_history(&blame_view_history);
	string_copy_rev(state->history_state.id, opt_ref);
	state->history_state.filename = get_path(opt_file);
	if (!state->history_state.filename)
		return FALSE;
	string_format(view->vid, "%s", opt_file);
	string_format(view->ref, "%s ...", opt_file);

	return TRUE;
}

static struct blame_commit *
get_blame_commit(struct view *view, const char *id)
{
	size_t i;

	for (i = 0; i < view->lines; i++) {
		struct blame *blame = view->line[i].data;

		if (!blame->commit)
			continue;

		if (!strncmp(blame->commit->id, id, SIZEOF_REV - 1))
			return blame->commit;
	}

	{
		struct blame_commit *commit = calloc(1, sizeof(*commit));

		if (commit)
			string_ncopy(commit->id, id, SIZEOF_REV);
		return commit;
	}
}

static struct blame_commit *
read_blame_commit(struct view *view, const char *text, struct blame_state *state)
{
	struct blame_header header;
	struct blame_commit *commit;
	struct blame *blame;

	if (!parse_blame_header(&header, text, view->lines))
		return NULL;

	commit = get_blame_commit(view, text);
	if (!commit)
		return NULL;

	state->blamed += header.group;
	while (header.group--) {
		struct line *line = &view->line[header.lineno + header.group - 1];

		blame = line->data;
		blame->commit = commit;
		blame->lineno = header.orig_lineno + header.group - 1;
		line->dirty = 1;
	}

	return commit;
}

static bool
blame_read_file(struct view *view, const char *text, struct blame_state *state)
{
	if (!text) {
		const char *blame_argv[] = {
			"git", "blame", encoding_arg, "%(blameargs)", "--incremental",
				*opt_ref ? opt_ref : "--incremental", "--", opt_file, NULL
		};

		if (view->lines == 0 && !view->prev)
			die("No blame exist for %s", view->vid);

		if (view->lines == 0 || !begin_update(view, opt_cdup, blame_argv, OPEN_EXTRA)) {
			report("Failed to load blame data");
			return TRUE;
		}

		if (opt_goto_line > 0) {
			select_view_line(view, opt_goto_line);
			opt_goto_line = 0;
		}

		state->done_reading = TRUE;
		return FALSE;

	} else {
		size_t textlen = strlen(text);
		struct blame *blame;

		if (!add_line_alloc(view, &blame, LINE_ID, textlen, FALSE))
			return FALSE;

		blame->commit = NULL;
		strncpy(blame->text, text, textlen);
		blame->text[textlen] = 0;
		return TRUE;
	}
}

static bool
blame_read(struct view *view, char *line)
{
	struct blame_state *state = view->private;

	if (!state->done_reading)
		return blame_read_file(view, line, state);

	if (!line) {
		state->auto_filename_display = blame_detect_filename_display(view);
		string_format(view->ref, "%s", view->vid);
		if (view_is_displayed(view)) {
			update_view_title(view);
			redraw_view_from(view, 0);
		}
		return TRUE;
	}

	if (!state->commit) {
		state->commit = read_blame_commit(view, line, state);
		string_format(view->ref, "%s %2zd%%", view->vid,
			      view->lines ? state->blamed * 100 / view->lines : 0);

	} else if (parse_blame_info(state->commit, line)) {
		if (!state->commit->filename)
			return FALSE;
		state->commit = NULL;
	}

	return TRUE;
}

static bool
blame_draw(struct view *view, struct line *line, unsigned int lineno)
{
	struct blame_state *state = view->private;
	struct blame *blame = line->data;
	struct time *time = NULL;
	const char *id = NULL, *filename = NULL;
	const struct ident *author = NULL;
	enum line_type id_type = LINE_ID;
	static const enum line_type blame_colors[] = {
		LINE_PALETTE_0,
		LINE_PALETTE_1,
		LINE_PALETTE_2,
		LINE_PALETTE_3,
		LINE_PALETTE_4,
		LINE_PALETTE_5,
		LINE_PALETTE_6,
	};

#define BLAME_COLOR(i) \
	(blame_colors[(i) % ARRAY_SIZE(blame_colors)])

	if (blame->commit && blame->commit->filename) {
		id = blame->commit->id;
		author = blame->commit->author;
		filename = blame->commit->filename;
		time = &blame->commit->time;
		id_type = BLAME_COLOR((long) blame->commit);
	}

	if (draw_date(view, time))
		return TRUE;

	if (draw_author(view, author))
		return TRUE;

	if (draw_filename(view, filename, state->auto_filename_display))
		return TRUE;

	if (draw_id_custom(view, id_type, id, opt_id_cols))
		return TRUE;

	if (draw_lineno(view, lineno))
		return TRUE;

	draw_text(view, LINE_DEFAULT, blame->text);
	return TRUE;
}

static bool
check_blame_commit(struct blame *blame, bool check_null_id)
{
	if (!blame->commit)
		report("Commit data not loaded yet");
	else if (check_null_id && string_rev_is_null(blame->commit->id))
		report("No commit exist for the selected line");
	else
		return TRUE;
	return FALSE;
}

static void
setup_blame_parent_line(struct view *view, struct blame *blame)
{
	char from[SIZEOF_REF + SIZEOF_STR];
	char to[SIZEOF_REF + SIZEOF_STR];
	const char *diff_tree_argv[] = {
		"git", "diff", encoding_arg, "--no-textconv", "--no-extdiff",
			"--no-color", "-U0", from, to, "--", NULL
	};
	struct io io;
	int parent_lineno = -1;
	int blamed_lineno = -1;
	char *line;

	if (!string_format(from, "%s:%s", opt_ref, opt_file) ||
	    !string_format(to, "%s:%s", blame->commit->id, blame->commit->filename) ||
	    !io_run(&io, IO_RD, NULL, opt_env, diff_tree_argv))
		return;

	while ((line = io_get(&io, '\n', TRUE))) {
		if (*line == '@') {
			char *pos = strchr(line, '+');

			parent_lineno = atoi(line + 4);
			if (pos)
				blamed_lineno = atoi(pos + 1);

		} else if (*line == '+' && parent_lineno != -1) {
			if (blame->lineno == blamed_lineno - 1 &&
			    !strcmp(blame->text, line + 1)) {
				view->pos.lineno = parent_lineno ? parent_lineno - 1 : 0;
				break;
			}
			blamed_lineno++;
		}
	}

	io_done(&io);
}

static void
blame_go_forward(struct view *view, struct blame *blame, bool parent)
{
	struct blame_state *state = view->private;
	struct blame_history_state *history_state = &state->history_state;
	struct blame_commit *commit = blame->commit;
	const char *id = parent ? commit->parent_id : commit->id;
	const char *filename = parent ? commit->parent_filename : commit->filename;

	if (!*id && parent) {
		report("The selected commit has no parents");
		return;
	}

	if (!strcmp(history_state->id, id) && !strcmp(history_state->filename, filename)) {
		report("The selected commit is already displayed");
		return;
	}

	if (!push_view_history_state(&blame_view_history, &view->pos, history_state)) {
		report("Failed to save current view state");
		return;
	}

	string_ncopy(opt_ref, id, sizeof(commit->id));
	string_ncopy(opt_file, filename, strlen(filename));
	if (parent)
		setup_blame_parent_line(view, blame);
	opt_goto_line = blame->lineno;
	reload_view(view);
}

static void
blame_go_back(struct view *view)
{
	struct blame_history_state history_state;

	if (!pop_view_history_state(&blame_view_history, &view->pos, &history_state)) {
		report("Already at start of history");
		return;
	}

	string_copy(opt_ref, history_state.id);
	string_ncopy(opt_file, history_state.filename, strlen(history_state.filename));
	opt_goto_line = view->pos.lineno;
	reload_view(view);
}

static enum request
blame_request(struct view *view, enum request request, struct line *line)
{
	enum open_flags flags = view_is_displayed(view) ? OPEN_SPLIT : OPEN_DEFAULT;
	struct blame *blame = line->data;

	switch (request) {
	case REQ_VIEW_BLAME:
	case REQ_PARENT:
		if (!check_blame_commit(blame, TRUE))
			break;
		blame_go_forward(view, blame, request == REQ_PARENT);
		break;

	case REQ_BACK:
		blame_go_back(view);
		break;

	case REQ_ENTER:
		if (!check_blame_commit(blame, FALSE))
			break;

		if (view_is_displayed(VIEW(REQ_VIEW_DIFF)) &&
		    !strcmp(blame->commit->id, VIEW(REQ_VIEW_DIFF)->ref))
			break;

		if (string_rev_is_null(blame->commit->id)) {
			struct view *diff = VIEW(REQ_VIEW_DIFF);
			const char *diff_parent_argv[] = {
				GIT_DIFF_BLAME(encoding_arg,
					opt_diff_context_arg,
					opt_ignore_space_arg, view->vid)
			};
			const char *diff_no_parent_argv[] = {
				GIT_DIFF_BLAME_NO_PARENT(encoding_arg,
					opt_diff_context_arg,
					opt_ignore_space_arg, view->vid)
			};
			const char **diff_index_argv = *blame->commit->parent_id
				? diff_parent_argv : diff_no_parent_argv;

			open_argv(view, diff, diff_index_argv, NULL, flags);
			if (diff->pipe)
				string_copy_rev(diff->ref, NULL_ID);
		} else {
			open_view(view, REQ_VIEW_DIFF, flags);
		}
		break;

	default:
		return request;
	}

	return REQ_NONE;
}

static bool
blame_grep(struct view *view, struct line *line)
{
	struct blame *blame = line->data;
	struct blame_commit *commit = blame->commit;
	const char *text[] = {
		blame->text,
		commit ? commit->title : "",
		commit ? commit->id : "",
		commit ? mkauthor(commit->author, opt_author_width, opt_author) : "",
		commit ? mkdate(&commit->time, opt_date) : "",
		NULL
	};

	return grep_text(view, text);
}

static void
blame_select(struct view *view, struct line *line)
{
	struct blame *blame = line->data;
	struct blame_commit *commit = blame->commit;

	if (!commit)
		return;

	if (string_rev_is_null(commit->id))
		string_ncopy(ref_commit, "HEAD", 4);
	else
		string_copy_rev(ref_commit, commit->id);
}

static struct view_ops blame_ops = {
	"line",
	{ "blame" },
	VIEW_ALWAYS_LINENO | VIEW_SEND_CHILD_ENTER,
	sizeof(struct blame_state),
	blame_open,
	blame_read,
	blame_draw,
	blame_request,
	blame_grep,
	blame_select,
};

/*
 * Branch backend
 */

struct branch {
	const struct ident *author;	/* Author of the last commit. */
	struct time time;		/* Date of the last activity. */
	char title[128];		/* First line of the commit message. */
	const struct ref *ref;		/* Name and commit ID information. */
};

static const struct ref branch_all;
#define BRANCH_ALL_NAME	"All branches"
#define branch_is_all(branch) ((branch)->ref == &branch_all)

static const enum sort_field branch_sort_fields[] = {
	ORDERBY_NAME, ORDERBY_DATE, ORDERBY_AUTHOR
};
static struct sort_state branch_sort_state = SORT_STATE(branch_sort_fields);

struct branch_state {
	char id[SIZEOF_REV];
	size_t max_ref_length;
};

static int
branch_compare(const void *l1, const void *l2)
{
	const struct branch *branch1 = ((const struct line *) l1)->data;
	const struct branch *branch2 = ((const struct line *) l2)->data;

	if (branch_is_all(branch1))
		return -1;
	else if (branch_is_all(branch2))
		return 1;

	switch (get_sort_field(branch_sort_state)) {
	case ORDERBY_DATE:
		return sort_order(branch_sort_state, timecmp(&branch1->time, &branch2->time));

	case ORDERBY_AUTHOR:
		return sort_order(branch_sort_state, ident_compare(branch1->author, branch2->author));

	case ORDERBY_NAME:
	default:
		return sort_order(branch_sort_state, strcmp(branch1->ref->name, branch2->ref->name));
	}
}

static bool
branch_draw(struct view *view, struct line *line, unsigned int lineno)
{
	struct branch_state *state = view->private;
	struct branch *branch = line->data;
	enum line_type type = branch_is_all(branch) ? LINE_DEFAULT : get_line_type_from_ref(branch->ref);
	const char *branch_name = branch_is_all(branch) ? BRANCH_ALL_NAME : branch->ref->name;

	if (draw_lineno(view, lineno))
		return TRUE;

	if (draw_date(view, &branch->time))
		return TRUE;

	if (draw_author(view, branch->author))
		return TRUE;

	if (draw_field(view, type, branch_name, state->max_ref_length, ALIGN_LEFT, FALSE))
		return TRUE;

	if (draw_id(view, branch->ref->id))
		return TRUE;

	draw_text(view, LINE_DEFAULT, branch->title);
	return TRUE;
}

static enum request
branch_request(struct view *view, enum request request, struct line *line)
{
	struct branch *branch = line->data;

	switch (request) {
	case REQ_REFRESH:
		load_refs(TRUE);
		refresh_view(view);
		return REQ_NONE;

	case REQ_TOGGLE_SORT_FIELD:
	case REQ_TOGGLE_SORT_ORDER:
		sort_view(view, request, &branch_sort_state, branch_compare);
		return REQ_NONE;

	case REQ_ENTER:
	{
		const struct ref *ref = branch->ref;
		const char *all_branches_argv[] = {
			GIT_MAIN_LOG(encoding_arg, "", branch_is_all(branch) ? "--all" : ref->name, "")
		};
		struct view *main_view = VIEW(REQ_VIEW_MAIN);

		open_argv(view, main_view, all_branches_argv, NULL, OPEN_SPLIT);
		return REQ_NONE;
	}
	case REQ_JUMP_COMMIT:
	{
		int lineno;

		for (lineno = 0; lineno < view->lines; lineno++) {
			struct branch *branch = view->line[lineno].data;

			if (!strncasecmp(branch->ref->id, opt_search, strlen(opt_search))) {
				select_view_line(view, lineno);
				report_clear();
				return REQ_NONE;
			}
		}
	}
	default:
		return request;
	}
}

static bool
branch_read(struct view *view, char *line)
{
	struct branch_state *state = view->private;
	const char *title = NULL;
	const struct ident *author = NULL;
	struct time time = {};
	size_t i;

	if (!line)
		return TRUE;

	switch (get_line_type(line)) {
	case LINE_COMMIT:
		string_copy_rev_from_commit_line(state->id, line);
		return TRUE;

	case LINE_AUTHOR:
		parse_author_line(line + STRING_SIZE("author "), &author, &time);
		break;

	default:
		title = line + STRING_SIZE("title ");
	}

	for (i = 0; i < view->lines; i++) {
		struct branch *branch = view->line[i].data;

		if (strcmp(branch->ref->id, state->id))
			continue;

		if (author) {
			branch->author = author;
			branch->time = time;
		}

		if (title)
			string_expand(branch->title, sizeof(branch->title), title, 1);

		view->line[i].dirty = TRUE;
	}

	return TRUE;
}

static bool
branch_open_visitor(void *data, const struct ref *ref)
{
	struct view *view = data;
	struct branch_state *state = view->private;
	struct branch *branch;
	bool is_all = ref == &branch_all;
	size_t ref_length;

	if (ref->tag || ref->ltag)
		return TRUE;

	if (!add_line_alloc(view, &branch, LINE_DEFAULT, 0, is_all))
		return FALSE;

	ref_length = is_all ? STRING_SIZE(BRANCH_ALL_NAME) : strlen(ref->name);
	if (ref_length > state->max_ref_length)
		state->max_ref_length = ref_length;

	branch->ref = ref;
	return TRUE;
}

static bool
branch_open(struct view *view, enum open_flags flags)
{
	const char *branch_log[] = {
		"git", "log", encoding_arg, "--no-color", "--date=raw",
			"--pretty=format:commit %H%nauthor %an <%ae> %ad%ntitle %s",
			"--all", "--simplify-by-decoration", NULL
	};

	if (!begin_update(view, NULL, branch_log, OPEN_RELOAD)) {
		report("Failed to load branch data");
		return FALSE;
	}

	branch_open_visitor(view, &branch_all);
	foreach_ref(branch_open_visitor, view);

	return TRUE;
}

static bool
branch_grep(struct view *view, struct line *line)
{
	struct branch *branch = line->data;
	const char *text[] = {
		branch->ref->name,
		mkauthor(branch->author, opt_author_width, opt_author),
		NULL
	};

	return grep_text(view, text);
}

static void
branch_select(struct view *view, struct line *line)
{
	struct branch *branch = line->data;

	if (branch_is_all(branch)) {
		string_copy(view->ref, BRANCH_ALL_NAME);
		return;
	}
	string_copy_rev(view->ref, branch->ref->id);
	string_copy_rev(ref_commit, branch->ref->id);
	string_copy_rev(ref_head, branch->ref->id);
	string_copy_rev(ref_branch, branch->ref->name);
}

static struct view_ops branch_ops = {
	"branch",
	{ "branch" },
	VIEW_REFRESH,
	sizeof(struct branch_state),
	branch_open,
	branch_read,
	branch_draw,
	branch_request,
	branch_grep,
	branch_select,
};

/*
 * Status backend
 */

struct status {
	char status;
	struct {
		mode_t mode;
		char rev[SIZEOF_REV];
		char name[SIZEOF_STR];
	} old;
	struct {
		mode_t mode;
		char rev[SIZEOF_REV];
		char name[SIZEOF_STR];
	} new;
};

static char status_onbranch[SIZEOF_STR];
static struct status stage_status;
static enum line_type stage_line_type;

DEFINE_ALLOCATOR(realloc_ints, int, 32)

/* This should work even for the "On branch" line. */
static inline bool
status_has_none(struct view *view, struct line *line)
{
	return view_has_line(view, line) && !line[1].data;
}

/* Get fields from the diff line:
 * :100644 100644 06a5d6ae9eca55be2e0e585a152e6b1336f2b20e 0000000000000000000000000000000000000000 M
 */
static inline bool
status_get_diff(struct status *file, const char *buf, size_t bufsize)
{
	const char *old_mode = buf +  1;
	const char *new_mode = buf +  8;
	const char *old_rev  = buf + 15;
	const char *new_rev  = buf + 56;
	const char *status   = buf + 97;

	if (bufsize < 98 ||
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

	file->old.name[0] = file->new.name[0] = 0;

	return TRUE;
}

static bool
status_run(struct view *view, const char *argv[], char status, enum line_type type)
{
	struct status *unmerged = NULL;
	char *buf;
	struct io io;

	if (!io_run(&io, IO_RD, opt_cdup, opt_env, argv))
		return FALSE;

	add_line_nodata(view, type);

	while ((buf = io_get(&io, 0, TRUE))) {
		struct status *file = unmerged;

		if (!file) {
			if (!add_line_alloc(view, &file, type, 0, FALSE))
				goto error_out;
		}

		/* Parse diff info part. */
		if (status) {
			file->status = status;
			if (status == 'A')
				string_copy(file->old.rev, NULL_ID);

		} else if (!file->status || file == unmerged) {
			if (!status_get_diff(file, buf, strlen(buf)))
				goto error_out;

			buf = io_get(&io, 0, TRUE);
			if (!buf)
				break;

			/* Collapse all modified entries that follow an
			 * associated unmerged entry. */
			if (unmerged == file) {
				unmerged->status = 'U';
				unmerged = NULL;
			} else if (file->status == 'U') {
				unmerged = file;
			}
		}

		/* Grab the old name for rename/copy. */
		if (!*file->old.name &&
		    (file->status == 'R' || file->status == 'C')) {
			string_ncopy(file->old.name, buf, strlen(buf));

			buf = io_get(&io, 0, TRUE);
			if (!buf)
				break;
		}

		/* git-ls-files just delivers a NUL separated list of
		 * file names similar to the second half of the
		 * git-diff-* output. */
		string_ncopy(file->new.name, buf, strlen(buf));
		if (!*file->old.name)
			string_copy(file->old.name, file->new.name);
		file = NULL;
	}

	if (io_error(&io)) {
error_out:
		io_done(&io);
		return FALSE;
	}

	if (!view->line[view->lines - 1].data)
		add_line_nodata(view, LINE_STAT_NONE);

	io_done(&io);
	return TRUE;
}

static const char *status_diff_index_argv[] = { GIT_DIFF_STAGED_FILES("-z") };
static const char *status_diff_files_argv[] = { GIT_DIFF_UNSTAGED_FILES("-z") };

static const char *status_list_other_argv[] = {
	"git", "ls-files", "-z", "--others", "--exclude-standard", opt_prefix, NULL, NULL,
};

static const char *status_list_no_head_argv[] = {
	"git", "ls-files", "-z", "--cached", "--exclude-standard", NULL
};

static const char *update_index_argv[] = {
	"git", "update-index", "-q", "--unmerged", "--refresh", NULL
};

/* Restore the previous line number to stay in the context or select a
 * line with something that can be updated. */
static void
status_restore(struct view *view)
{
	if (!check_position(&view->prev_pos))
		return;

	if (view->prev_pos.lineno >= view->lines)
		view->prev_pos.lineno = view->lines - 1;
	while (view->prev_pos.lineno < view->lines && !view->line[view->prev_pos.lineno].data)
		view->prev_pos.lineno++;
	while (view->prev_pos.lineno > 0 && !view->line[view->prev_pos.lineno].data)
		view->prev_pos.lineno--;

	/* If the above fails, always skip the "On branch" line. */
	if (view->prev_pos.lineno < view->lines)
		view->pos.lineno = view->prev_pos.lineno;
	else
		view->pos.lineno = 1;

	if (view->prev_pos.offset > view->pos.lineno)
		view->pos.offset = view->pos.lineno;
	else if (view->prev_pos.offset < view->lines)
		view->pos.offset = view->prev_pos.offset;

	clear_position(&view->prev_pos);
}

static void
status_update_onbranch(void)
{
	static const char *paths[][2] = {
		{ "rebase-apply/rebasing",	"Rebasing" },
		{ "rebase-apply/applying",	"Applying mailbox" },
		{ "rebase-apply/",		"Rebasing mailbox" },
		{ "rebase-merge/interactive",	"Interactive rebase" },
		{ "rebase-merge/",		"Rebase merge" },
		{ "MERGE_HEAD",			"Merging" },
		{ "BISECT_LOG",			"Bisecting" },
		{ "HEAD",			"On branch" },
	};
	char buf[SIZEOF_STR];
	struct stat stat;
	int i;

	if (is_initial_commit()) {
		string_copy(status_onbranch, "Initial commit");
		return;
	}

	for (i = 0; i < ARRAY_SIZE(paths); i++) {
		char *head = opt_head;

		if (!string_format(buf, "%s/%s", opt_git_dir, paths[i][0]) ||
		    lstat(buf, &stat) < 0)
			continue;

		if (!*opt_head) {
			struct io io;

			if (io_open(&io, "%s/rebase-merge/head-name", opt_git_dir) &&
			    io_read_buf(&io, buf, sizeof(buf))) {
				head = buf;
				if (!prefixcmp(head, "refs/heads/"))
					head += STRING_SIZE("refs/heads/");
			}
		}

		if (!string_format(status_onbranch, "%s %s", paths[i][1], head))
			string_copy(status_onbranch, opt_head);
		return;
	}

	string_copy(status_onbranch, "Not currently on any branch");
}

/* First parse staged info using git-diff-index(1), then parse unstaged
 * info using git-diff-files(1), and finally untracked files using
 * git-ls-files(1). */
static bool
status_open(struct view *view, enum open_flags flags)
{
	const char **staged_argv = is_initial_commit() ?
		status_list_no_head_argv : status_diff_index_argv;
	char staged_status = staged_argv == status_list_no_head_argv ? 'A' : 0;

	if (opt_is_inside_work_tree == FALSE) {
		report("The status view requires a working tree");
		return FALSE;
	}

	reset_view(view);

	add_line_nodata(view, LINE_STAT_HEAD);
	status_update_onbranch();

	io_run_bg(update_index_argv);

	status_list_other_argv[ARRAY_SIZE(status_list_other_argv) - 2] =
		opt_untracked_dirs_content ? NULL : "--directory";

	if (!status_run(view, staged_argv, staged_status, LINE_STAT_STAGED) ||
	    !status_run(view, status_diff_files_argv, 0, LINE_STAT_UNSTAGED) ||
	    !status_run(view, status_list_other_argv, '?', LINE_STAT_UNTRACKED)) {
		report("Failed to load status data");
		return FALSE;
	}

	/* Restore the exact position or use the specialized restore
	 * mode? */
	status_restore(view);
	return TRUE;
}

static bool
status_draw(struct view *view, struct line *line, unsigned int lineno)
{
	struct status *status = line->data;
	enum line_type type;
	const char *text;

	if (!status) {
		switch (line->type) {
		case LINE_STAT_STAGED:
			type = LINE_STAT_SECTION;
			text = "Changes to be committed:";
			break;

		case LINE_STAT_UNSTAGED:
			type = LINE_STAT_SECTION;
			text = "Changed but not updated:";
			break;

		case LINE_STAT_UNTRACKED:
			type = LINE_STAT_SECTION;
			text = "Untracked files:";
			break;

		case LINE_STAT_NONE:
			type = LINE_DEFAULT;
			text = "  (no files)";
			break;

		case LINE_STAT_HEAD:
			type = LINE_STAT_HEAD;
			text = status_onbranch;
			break;

		default:
			return FALSE;
		}
	} else {
		static char buf[] = { '?', ' ', ' ', ' ', 0 };

		buf[0] = status->status;
		if (draw_text(view, line->type, buf))
			return TRUE;
		type = LINE_DEFAULT;
		text = status->new.name;
	}

	draw_text(view, type, text);
	return TRUE;
}

static enum request
status_enter(struct view *view, struct line *line)
{
	struct status *status = line->data;
	enum open_flags flags = view_is_displayed(view) ? OPEN_SPLIT : OPEN_DEFAULT;

	if (line->type == LINE_STAT_NONE ||
	    (!status && line[1].type == LINE_STAT_NONE)) {
		report("No file to diff");
		return REQ_NONE;
	}

	switch (line->type) {
	case LINE_STAT_STAGED:
	case LINE_STAT_UNSTAGED:
		break;

	case LINE_STAT_UNTRACKED:
		if (!status) {
			report("No file to show");
			return REQ_NONE;
		}

	    	if (!suffixcmp(status->new.name, -1, "/")) {
			report("Cannot display a directory");
			return REQ_NONE;
		}
		break;

	case LINE_STAT_HEAD:
		return REQ_NONE;

	default:
		die("line type %d not handled in switch", line->type);
	}

	if (status) {
		stage_status = *status;
	} else {
		memset(&stage_status, 0, sizeof(stage_status));
	}

	stage_line_type = line->type;

	open_view(view, REQ_VIEW_STAGE, flags);
	return REQ_NONE;
}

static bool
status_exists(struct view *view, struct status *status, enum line_type type)
{
	unsigned long lineno;

	for (lineno = 0; lineno < view->lines; lineno++) {
		struct line *line = &view->line[lineno];
		struct status *pos = line->data;

		if (line->type != type)
			continue;
		if (!pos && (!status || !status->status) && line[1].data) {
			select_view_line(view, lineno);
			return TRUE;
		}
		if (pos && !strcmp(status->new.name, pos->new.name)) {
			select_view_line(view, lineno);
			return TRUE;
		}
	}

	return FALSE;
}


static bool
status_update_prepare(struct io *io, enum line_type type)
{
	const char *staged_argv[] = {
		"git", "update-index", "-z", "--index-info", NULL
	};
	const char *others_argv[] = {
		"git", "update-index", "-z", "--add", "--remove", "--stdin", NULL
	};

	switch (type) {
	case LINE_STAT_STAGED:
		return io_run(io, IO_WR, opt_cdup, opt_env, staged_argv);

	case LINE_STAT_UNSTAGED:
	case LINE_STAT_UNTRACKED:
		return io_run(io, IO_WR, opt_cdup, opt_env, others_argv);

	default:
		die("line type %d not handled in switch", type);
		return FALSE;
	}
}

static bool
status_update_write(struct io *io, struct status *status, enum line_type type)
{
	switch (type) {
	case LINE_STAT_STAGED:
		return io_printf(io, "%06o %s\t%s%c", status->old.mode,
				 status->old.rev, status->old.name, 0);

	case LINE_STAT_UNSTAGED:
	case LINE_STAT_UNTRACKED:
		return io_printf(io, "%s%c", status->new.name, 0);

	default:
		die("line type %d not handled in switch", type);
		return FALSE;
	}
}

static bool
status_update_file(struct status *status, enum line_type type)
{
	struct io io;
	bool result;

	if (!status_update_prepare(&io, type))
		return FALSE;

	result = status_update_write(&io, status, type);
	return io_done(&io) && result;
}

static bool
status_update_files(struct view *view, struct line *line)
{
	char buf[sizeof(view->ref)];
	struct io io;
	bool result = TRUE;
	struct line *pos;
	int files = 0;
	int file, done;
	int cursor_y = -1, cursor_x = -1;

	if (!status_update_prepare(&io, line->type))
		return FALSE;

	for (pos = line; view_has_line(view, pos) && pos->data; pos++)
		files++;

	string_copy(buf, view->ref);
	getsyx(cursor_y, cursor_x);
	for (file = 0, done = 5; result && file < files; line++, file++) {
		int almost_done = file * 100 / files;

		if (almost_done > done) {
			done = almost_done;
			string_format(view->ref, "updating file %u of %u (%d%% done)",
				      file, files, done);
			update_view_title(view);
			setsyx(cursor_y, cursor_x);
			doupdate();
		}
		result = status_update_write(&io, line->data, line->type);
	}
	string_copy(view->ref, buf);

	return io_done(&io) && result;
}

static bool
status_update(struct view *view)
{
	struct line *line = &view->line[view->pos.lineno];

	assert(view->lines);

	if (!line->data) {
		if (status_has_none(view, line)) {
			report("Nothing to update");
			return FALSE;
		}

		if (!status_update_files(view, line + 1)) {
			report("Failed to update file status");
			return FALSE;
		}

	} else if (!status_update_file(line->data, line->type)) {
		report("Failed to update file status");
		return FALSE;
	}

	return TRUE;
}

static bool
status_revert(struct status *status, enum line_type type, bool has_none)
{
	if (!status || type != LINE_STAT_UNSTAGED) {
		if (type == LINE_STAT_STAGED) {
			report("Cannot revert changes to staged files");
		} else if (type == LINE_STAT_UNTRACKED) {
			report("Cannot revert changes to untracked files");
		} else if (has_none) {
			report("Nothing to revert");
		} else {
			report("Cannot revert changes to multiple files");
		}

	} else if (prompt_yesno("Are you sure you want to revert changes?")) {
		char mode[10] = "100644";
		const char *reset_argv[] = {
			"git", "update-index", "--cacheinfo", mode,
				status->old.rev, status->old.name, NULL
		};
		const char *checkout_argv[] = {
			"git", "checkout", "--", status->old.name, NULL
		};

		if (status->status == 'U') {
			string_format(mode, "%5o", status->old.mode);

			if (status->old.mode == 0 && status->new.mode == 0) {
				reset_argv[2] = "--force-remove";
				reset_argv[3] = status->old.name;
				reset_argv[4] = NULL;
			}

			if (!io_run_fg(reset_argv, opt_cdup))
				return FALSE;
			if (status->old.mode == 0 && status->new.mode == 0)
				return TRUE;
		}

		return io_run_fg(checkout_argv, opt_cdup);
	}

	return FALSE;
}

static enum request
status_request(struct view *view, enum request request, struct line *line)
{
	struct status *status = line->data;

	switch (request) {
	case REQ_STATUS_UPDATE:
		if (!status_update(view))
			return REQ_NONE;
		break;

	case REQ_STATUS_REVERT:
		if (!status_revert(status, line->type, status_has_none(view, line)))
			return REQ_NONE;
		break;

	case REQ_STATUS_MERGE:
		if (!status || status->status != 'U') {
			report("Merging only possible for files with unmerged status ('U').");
			return REQ_NONE;
		}
		open_mergetool(status->new.name);
		break;

	case REQ_EDIT:
		if (!status)
			return request;
		if (status->status == 'D') {
			report("File has been deleted.");
			return REQ_NONE;
		}

		open_editor(status->new.name, 0);
		break;

	case REQ_VIEW_BLAME:
		if (line->type == LINE_STAT_UNTRACKED || !status) {
			report("Nothing to blame here");
			return REQ_NONE;
		}
		if (status)
			opt_ref[0] = 0;
		return request;

	case REQ_ENTER:
		/* After returning the status view has been split to
		 * show the stage view. No further reloading is
		 * necessary. */
		return status_enter(view, line);

	case REQ_REFRESH:
		/* Load the current branch information and then the view. */
		load_refs(TRUE);
		break;

	default:
		return request;
	}

	refresh_view(view);

	return REQ_NONE;
}

static bool
status_stage_info_(char *buf, size_t bufsize,
		   enum line_type type, struct status *status)
{
	const char *file = status ? status->new.name : "";
	const char *info;

	switch (type) {
	case LINE_STAT_STAGED:
		if (status && status->status)
			info = "Staged changes to %s";
		else
			info = "Staged changes";
		break;

	case LINE_STAT_UNSTAGED:
		if (status && status->status)
			info = "Unstaged changes to %s";
		else
			info = "Unstaged changes";
		break;

	case LINE_STAT_UNTRACKED:
		info = "Untracked file %s";
		break;

	case LINE_STAT_HEAD:
	default:
		info = "";
	}

	return string_nformat(buf, bufsize, NULL, info, file);
}
#define status_stage_info(buf, type, status) \
	status_stage_info_(buf, sizeof(buf), type, status)

static void
status_select(struct view *view, struct line *line)
{
	struct status *status = line->data;
	char file[SIZEOF_STR] = "all files";
	const char *text;
	const char *key;

	if (status && !string_format(file, "'%s'", status->new.name))
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

	case LINE_STAT_HEAD:
	case LINE_STAT_NONE:
		text = "Nothing to update";
		break;

	default:
		die("line type %d not handled in switch", line->type);
	}

	if (status && status->status == 'U') {
		text = "Press %s to resolve conflict in %s";
		key = get_view_key(view, REQ_STATUS_MERGE);

	} else {
		key = get_view_key(view, REQ_STATUS_UPDATE);
	}

	string_format(view->ref, text, key, file);
	status_stage_info(ref_status, line->type, status);
	if (status)
		string_copy(opt_file, status->new.name);
}

static bool
status_grep(struct view *view, struct line *line)
{
	struct status *status = line->data;

	if (status) {
		const char buf[2] = { status->status, 0 };
		const char *text[] = { status->new.name, buf, NULL };

		return grep_text(view, text);
	}

	return FALSE;
}

static struct view_ops status_ops = {
	"file",
	{ "status" },
	VIEW_CUSTOM_STATUS | VIEW_SEND_CHILD_ENTER | VIEW_STATUS_LIKE | VIEW_REFRESH,
	0,
	status_open,
	NULL,
	status_draw,
	status_request,
	status_grep,
	status_select,
};


struct stage_state {
	struct diff_state diff;
	size_t chunks;
	int *chunk;
};

static bool
stage_diff_write(struct io *io, struct line *line, struct line *end)
{
	while (line < end) {
		if (!io_write(io, line->data, strlen(line->data)) ||
		    !io_write(io, "\n", 1))
			return FALSE;
		line++;
		if (line->type == LINE_DIFF_CHUNK ||
		    line->type == LINE_DIFF_HEADER)
			break;
	}

	return TRUE;
}

static bool
stage_apply_chunk(struct view *view, struct line *chunk, struct line *line, bool revert)
{
	const char *apply_argv[SIZEOF_ARG] = {
		"git", "apply", "--whitespace=nowarn", NULL
	};
	struct line *diff_hdr;
	struct io io;
	int argc = 3;

	diff_hdr = find_prev_line_by_type(view, chunk, LINE_DIFF_HEADER);
	if (!diff_hdr)
		return FALSE;

	if (!revert)
		apply_argv[argc++] = "--cached";
	if (line != NULL)
		apply_argv[argc++] = "--unidiff-zero";
	if (revert || stage_line_type == LINE_STAT_STAGED)
		apply_argv[argc++] = "-R";
	apply_argv[argc++] = "-";
	apply_argv[argc++] = NULL;
	if (!io_run(&io, IO_WR, opt_cdup, opt_env, apply_argv))
		return FALSE;

	if (line != NULL) {
		unsigned long lineno = 0;
		struct line *context = chunk + 1;
		const char *markers[] = {
			line->type == LINE_DIFF_DEL ? ""   : ",0",
			line->type == LINE_DIFF_DEL ? ",0" : "",
		};

		parse_chunk_lineno(&lineno, chunk->data, line->type == LINE_DIFF_DEL ? '+' : '-');

		while (context < line) {
			if (context->type == LINE_DIFF_CHUNK || context->type == LINE_DIFF_HEADER) {
				break;
			} else if (context->type != LINE_DIFF_DEL && context->type != LINE_DIFF_ADD) {
				lineno++;
			}
			context++;
		}

		if (!stage_diff_write(&io, diff_hdr, chunk) ||
		    !io_printf(&io, "@@ -%lu%s +%lu%s @@\n",
			       lineno, markers[0], lineno, markers[1]) ||
		    !stage_diff_write(&io, line, line + 1)) {
			chunk = NULL;
		}
	} else {
		if (!stage_diff_write(&io, diff_hdr, chunk) ||
		    !stage_diff_write(&io, chunk, view->line + view->lines))
			chunk = NULL;
	}

	io_done(&io);

	return chunk ? TRUE : FALSE;
}

static bool
stage_update(struct view *view, struct line *line, bool single)
{
	struct line *chunk = NULL;

	if (!is_initial_commit() && stage_line_type != LINE_STAT_UNTRACKED)
		chunk = find_prev_line_by_type(view, line, LINE_DIFF_CHUNK);

	if (chunk) {
		if (!stage_apply_chunk(view, chunk, single ? line : NULL, FALSE)) {
			report("Failed to apply chunk");
			return FALSE;
		}

	} else if (!stage_status.status) {
		view = view->parent;

		for (line = view->line; view_has_line(view, line); line++)
			if (line->type == stage_line_type)
				break;

		if (!status_update_files(view, line + 1)) {
			report("Failed to update files");
			return FALSE;
		}

	} else if (!status_update_file(&stage_status, stage_line_type)) {
		report("Failed to update file");
		return FALSE;
	}

	return TRUE;
}

static bool
stage_revert(struct view *view, struct line *line)
{
	struct line *chunk = NULL;

	if (!is_initial_commit() && stage_line_type == LINE_STAT_UNSTAGED)
		chunk = find_prev_line_by_type(view, line, LINE_DIFF_CHUNK);

	if (chunk) {
		if (!prompt_yesno("Are you sure you want to revert changes?"))
			return FALSE;

		if (!stage_apply_chunk(view, chunk, NULL, TRUE)) {
			report("Failed to revert chunk");
			return FALSE;
		}
		return TRUE;

	} else {
		return status_revert(stage_status.status ? &stage_status : NULL,
				     stage_line_type, FALSE);
	}
}


static void
stage_next(struct view *view, struct line *line)
{
	struct stage_state *state = view->private;
	int i;

	if (!state->chunks) {
		for (line = view->line; view_has_line(view, line); line++) {
			if (line->type != LINE_DIFF_CHUNK)
				continue;

			if (!realloc_ints(&state->chunk, state->chunks, 1)) {
				report("Allocation failure");
				return;
			}

			state->chunk[state->chunks++] = line - view->line;
		}
	}

	for (i = 0; i < state->chunks; i++) {
		if (state->chunk[i] > view->pos.lineno) {
			do_scroll_view(view, state->chunk[i] - view->pos.lineno);
			report("Chunk %d of %zd", i + 1, state->chunks);
			return;
		}
	}

	report("No next chunk found");
}

static struct line *
stage_insert_chunk(struct view *view, struct chunk_header *header,
		   struct line *from, struct line *to, struct line *last_unchanged_line)
{
	char buf[SIZEOF_STR];
	char *chunk_line;
	unsigned long from_lineno = last_unchanged_line - view->line;
	unsigned long to_lineno = to - view->line;
	unsigned long after_lineno = to_lineno;

	if (!string_format(buf, "@@ -%lu,%lu +%lu,%lu @@",
			header->old.position, header->old.lines,
			header->new.position, header->new.lines))
		return NULL;

	chunk_line = strdup(buf);
	if (!chunk_line)
		return NULL;

	free(from->data);
	from->data = chunk_line;

	if (!to)
		return from;

	if (!add_line_at(view, after_lineno++, buf, LINE_DIFF_CHUNK, strlen(buf) + 1, FALSE))
		return NULL;

	while (from_lineno < to_lineno) {
		struct line *line = &view->line[from_lineno++];

		if (!add_line_at(view, after_lineno++, line->data, line->type, strlen(line->data) + 1, FALSE))
			return FALSE;
	}

	return view->line + after_lineno;
}

static void
stage_split_chunk(struct view *view, struct line *chunk_start)
{
	struct chunk_header header;
	struct line *last_changed_line = NULL, *last_unchanged_line = NULL, *pos;
	int chunks = 0;

	if (!chunk_start || !parse_chunk_header(&header, chunk_start->data)) {
		report("Failed to parse chunk header");
		return;
	}

	header.old.lines = header.new.lines = 0;

	for (pos = chunk_start + 1; view_has_line(view, pos); pos++) {
		const char *chunk_line = pos->data;

		if (*chunk_line == '@' || *chunk_line == '\\')
			break;

		if (*chunk_line == ' ') {
			header.old.lines++;
			header.new.lines++;
			if (last_unchanged_line < last_changed_line)
				last_unchanged_line = pos;
			continue;
		}

		if (last_changed_line && last_changed_line < last_unchanged_line) {
			unsigned long chunk_start_lineno = pos - view->line;
			unsigned long diff = pos - last_unchanged_line;

			pos = stage_insert_chunk(view, &header, chunk_start, pos, last_unchanged_line);

			header.old.position += header.old.lines - diff;
			header.new.position += header.new.lines - diff;
			header.old.lines = header.new.lines = diff;

			chunk_start = view->line + chunk_start_lineno;
			last_changed_line = last_unchanged_line = NULL;
			chunks++;
		}

		if (*chunk_line == '-') {
			header.old.lines++;
			last_changed_line = pos;
		} else if (*chunk_line == '+') {
			header.new.lines++;
			last_changed_line = pos;
		}
	}

	if (chunks) {
		stage_insert_chunk(view, &header, chunk_start, NULL, NULL);
		redraw_view(view);
		report("Split the chunk in %d", chunks + 1);
	} else {
		report("The chunk cannot be split");
	}
}

static enum request
stage_request(struct view *view, enum request request, struct line *line)
{
	switch (request) {
	case REQ_STATUS_UPDATE:
		if (!stage_update(view, line, FALSE))
			return REQ_NONE;
		break;

	case REQ_STATUS_REVERT:
		if (!stage_revert(view, line))
			return REQ_NONE;
		break;

	case REQ_STAGE_UPDATE_LINE:
		if (stage_line_type == LINE_STAT_UNTRACKED ||
		    stage_status.status == 'A') {
			report("Staging single lines is not supported for new files");
			return REQ_NONE;
		}
		if (line->type != LINE_DIFF_DEL && line->type != LINE_DIFF_ADD) {
			report("Please select a change to stage");
			return REQ_NONE;
		}
		if (!stage_update(view, line, TRUE))
			return REQ_NONE;
		break;

	case REQ_STAGE_NEXT:
		if (stage_line_type == LINE_STAT_UNTRACKED) {
			report("File is untracked; press %s to add",
			       get_view_key(view, REQ_STATUS_UPDATE));
			return REQ_NONE;
		}
		stage_next(view, line);
		return REQ_NONE;

	case REQ_STAGE_SPLIT_CHUNK:
		if (stage_line_type == LINE_STAT_UNTRACKED ||
		    !(line = find_prev_line_by_type(view, line, LINE_DIFF_CHUNK))) {
			report("No chunks to split in sight");
			return REQ_NONE;
		}
		stage_split_chunk(view, line);
		return REQ_NONE;

	case REQ_EDIT:
		if (!stage_status.new.name[0])
			return diff_common_edit(view, request, line);

		if (stage_status.status == 'D') {
			report("File has been deleted.");
			return REQ_NONE;
		}

		if (stage_line_type == LINE_STAT_UNTRACKED) {
			open_editor(stage_status.new.name, (line - view->line) + 1);
		} else {
			open_editor(stage_status.new.name, diff_get_lineno(view, line));
		}
		break;

	case REQ_REFRESH:
		/* Reload everything(including current branch information) ... */
		load_refs(TRUE);
		break;

	case REQ_VIEW_BLAME:
		if (stage_line_type == LINE_STAT_UNTRACKED) {
			report("Nothing to blame here");
			return REQ_NONE;
		}

		if (stage_status.new.name[0]) {
			string_copy(opt_file, stage_status.new.name);
		} else {
			const char *file = diff_get_pathname(view, line);

			if (file)
				string_copy(opt_file, file);
		}

		opt_ref[0] = 0;
		opt_goto_line = diff_get_lineno(view, line);
		if (opt_goto_line > 0)
			opt_goto_line--;
		return request;

	case REQ_ENTER:
		return diff_common_enter(view, request, line);

	case REQ_DIFF_CONTEXT_UP:
	case REQ_DIFF_CONTEXT_DOWN:
		if (!update_diff_context(request))
			return REQ_NONE;
		break;

	default:
		return request;
	}

	refresh_view(view->parent);

	/* Check whether the staged entry still exists, and close the
	 * stage view if it doesn't. */
	if (!status_exists(view->parent, &stage_status, stage_line_type)) {
		status_restore(view->parent);
		return REQ_VIEW_CLOSE;
	}

	refresh_view(view);

	return REQ_NONE;
}

static bool
stage_open(struct view *view, enum open_flags flags)
{
	static const char *no_head_diff_argv[] = {
		GIT_DIFF_STAGED_INITIAL(encoding_arg, opt_diff_context_arg, opt_ignore_space_arg,
			stage_status.new.name)
	};
	static const char *index_show_argv[] = {
		GIT_DIFF_STAGED(encoding_arg, opt_diff_context_arg, opt_ignore_space_arg,
			stage_status.old.name, stage_status.new.name)
	};
	static const char *files_show_argv[] = {
		GIT_DIFF_UNSTAGED(encoding_arg, opt_diff_context_arg, opt_ignore_space_arg,
			stage_status.old.name, stage_status.new.name)
	};
	/* Diffs for unmerged entries are empty when passing the new
	 * path, so leave out the new path. */
	static const char *files_unmerged_argv[] = {
		"git", "diff-files", encoding_arg, "--root", "--patch-with-stat",
			opt_diff_context_arg, opt_ignore_space_arg, "--",
			stage_status.old.name, NULL
	};
	static const char *file_argv[] = { opt_cdup, stage_status.new.name, NULL };
	const char **argv = NULL;

	if (!stage_line_type) {
		report("No stage content, press %s to open the status view and choose file",
			get_view_key(view, REQ_VIEW_STATUS));
		return FALSE;
	}

	view->encoding = NULL;

	switch (stage_line_type) {
	case LINE_STAT_STAGED:
		if (is_initial_commit()) {
			argv = no_head_diff_argv;
		} else {
			argv = index_show_argv;
		}
		break;

	case LINE_STAT_UNSTAGED:
		if (stage_status.status != 'U')
			argv = files_show_argv;
		else
			argv = files_unmerged_argv;
		break;

	case LINE_STAT_UNTRACKED:
		argv = file_argv;
		view->encoding = get_path_encoding(stage_status.old.name, default_encoding);
		break;

	case LINE_STAT_HEAD:
	default:
		die("line type %d not handled in switch", stage_line_type);
	}

	if (!status_stage_info(view->ref, stage_line_type, &stage_status)
		|| !argv_copy(&view->argv, argv)) {
		report("Failed to open staged view");
		return FALSE;
	}

	view->vid[0] = 0;
	view->dir = opt_cdup;
	return begin_update(view, NULL, NULL, flags);
}

static bool
stage_read(struct view *view, char *data)
{
	struct stage_state *state = view->private;

	if (stage_line_type == LINE_STAT_UNTRACKED)
		return pager_common_read(view, data, LINE_DEFAULT);

	if (data && diff_common_read(view, data, &state->diff))
		return TRUE;

	return pager_read(view, data);
}

static struct view_ops stage_ops = {
	"line",
	{ "stage" },
	VIEW_DIFF_LIKE | VIEW_REFRESH,
	sizeof(struct stage_state),
	stage_open,
	stage_read,
	diff_common_draw,
	stage_request,
	pager_grep,
	pager_select,
};


/*
 * Revision graph
 */

static const enum line_type graph_colors[] = {
	LINE_PALETTE_0,
	LINE_PALETTE_1,
	LINE_PALETTE_2,
	LINE_PALETTE_3,
	LINE_PALETTE_4,
	LINE_PALETTE_5,
	LINE_PALETTE_6,
};

static enum line_type get_graph_color(struct graph_symbol *symbol)
{
	if (symbol->commit)
		return LINE_GRAPH_COMMIT;
	assert(symbol->color < ARRAY_SIZE(graph_colors));
	return graph_colors[symbol->color];
}

static bool
draw_graph_utf8(struct view *view, struct graph_symbol *symbol, enum line_type color, bool first)
{
	const char *chars = graph_symbol_to_utf8(symbol);

	return draw_text(view, color, chars + !!first);
}

static bool
draw_graph_ascii(struct view *view, struct graph_symbol *symbol, enum line_type color, bool first)
{
	const char *chars = graph_symbol_to_ascii(symbol);

	return draw_text(view, color, chars + !!first);
}

static bool
draw_graph_chtype(struct view *view, struct graph_symbol *symbol, enum line_type color, bool first)
{
	const chtype *chars = graph_symbol_to_chtype(symbol);

	return draw_graphic(view, color, chars + !!first, 2 - !!first, FALSE);
}

typedef bool (*draw_graph_fn)(struct view *, struct graph_symbol *, enum line_type, bool);

static bool draw_graph(struct view *view, struct graph_canvas *canvas)
{
	static const draw_graph_fn fns[] = {
		draw_graph_ascii,
		draw_graph_chtype,
		draw_graph_utf8
	};
	draw_graph_fn fn = fns[opt_line_graphics];
	int i;

	for (i = 0; i < canvas->size; i++) {
		struct graph_symbol *symbol = &canvas->symbols[i];
		enum line_type color = get_graph_color(symbol);

		if (fn(view, symbol, color, i == 0))
			return TRUE;
	}

	return draw_text(view, LINE_MAIN_REVGRAPH, " ");
}

/*
 * Main view backend
 */

DEFINE_ALLOCATOR(realloc_reflogs, char *, 32)

struct commit {
	char id[SIZEOF_REV];		/* SHA1 ID. */
	const struct ident *author;	/* Author of the commit. */
	struct time time;		/* Date from the author ident. */
	struct graph_canvas graph;	/* Ancestry chain graphics. */
	char title[1];			/* First line of the commit message. */
};

struct main_state {
	struct graph graph;
	struct commit current;
	char **reflog;
	size_t reflogs;
	int reflog_width;
	char reflogmsg[SIZEOF_STR / 2];
	bool in_header;
	bool added_changes_commits;
	bool with_graph;
};

static void
main_register_commit(struct view *view, struct commit *commit, const char *ids, bool is_boundary)
{
	struct main_state *state = view->private;

	string_copy_rev(commit->id, ids);
	if (state->with_graph)
		graph_add_commit(&state->graph, &commit->graph, commit->id, ids, is_boundary);
}

static struct commit *
main_add_commit(struct view *view, enum line_type type, struct commit *template,
		const char *title, bool custom)
{
	struct main_state *state = view->private;
	size_t titlelen = strlen(title);
	struct commit *commit;
	char buf[SIZEOF_STR / 2];

	/* FIXME: More graceful handling of titles; append "..." to
	 * shortened titles, etc. */
	string_expand(buf, sizeof(buf), title, 1);
	title = buf;
	titlelen = strlen(title);

	if (!add_line_alloc(view, &commit, type, titlelen, custom))
		return NULL;

	*commit = *template;
	strncpy(commit->title, title, titlelen);
	state->graph.canvas = &commit->graph;
	memset(template, 0, sizeof(*template));
	state->reflogmsg[0] = 0;
	return commit;
}

static inline void
main_flush_commit(struct view *view, struct commit *commit)
{
	if (*commit->id)
		main_add_commit(view, LINE_MAIN_COMMIT, commit, "", FALSE);
}

static bool
main_has_changes(const char *argv[])
{
	struct io io;

	if (!io_run(&io, IO_BG, NULL, opt_env, argv, -1))
		return FALSE;
	io_done(&io);
	return io.status == 1;
}

static void
main_add_changes_commit(struct view *view, enum line_type type, const char *parent, const char *title)
{
	char ids[SIZEOF_STR] = NULL_ID " ";
	struct main_state *state = view->private;
	struct commit commit = {};
	struct timeval now;
	struct timezone tz;

	if (!parent)
		return;

	string_copy_rev(ids + STRING_SIZE(NULL_ID " "), parent);

	if (!gettimeofday(&now, &tz)) {
		commit.time.tz = tz.tz_minuteswest * 60;
		commit.time.sec = now.tv_sec - commit.time.tz;
	}

	commit.author = &unknown_ident;
	main_register_commit(view, &commit, ids, FALSE);
	if (main_add_commit(view, type, &commit, title, TRUE) && state->with_graph)
		graph_render_parents(&state->graph);
}

static void
main_add_changes_commits(struct view *view, struct main_state *state, const char *parent)
{
	const char *staged_argv[] = { GIT_DIFF_STAGED_FILES("--quiet") };
	const char *unstaged_argv[] = { GIT_DIFF_UNSTAGED_FILES("--quiet") };
	const char *staged_parent = NULL_ID;
	const char *unstaged_parent = parent;

	if (!is_head_commit(parent))
		return;

	state->added_changes_commits = TRUE;

	io_run_bg(update_index_argv);

	if (!main_has_changes(unstaged_argv)) {
		unstaged_parent = NULL;
		staged_parent = parent;
	}

	if (!main_has_changes(staged_argv)) {
		staged_parent = NULL;
	}

	main_add_changes_commit(view, LINE_STAT_STAGED, staged_parent, "Staged changes");
	main_add_changes_commit(view, LINE_STAT_UNSTAGED, unstaged_parent, "Unstaged changes");
}

static bool
main_open(struct view *view, enum open_flags flags)
{
	static const char *main_argv[] = {
		GIT_MAIN_LOG(encoding_arg, "%(cmdlineargs)", "%(revargs)", "%(fileargs)")
	};
	struct main_state *state = view->private;

	state->with_graph = opt_rev_graph &&
			    opt_commit_order != COMMIT_ORDER_REVERSE;

	if (flags & OPEN_PAGER_MODE) {
		state->added_changes_commits = TRUE;
		state->with_graph = FALSE;
	}

	return begin_update(view, NULL, main_argv, flags);
}

static void
main_done(struct view *view)
{
	struct main_state *state = view->private;
	int i;

	for (i = 0; i < view->lines; i++) {
		struct commit *commit = view->line[i].data;

		free(commit->graph.symbols);
	}

	for (i = 0; i < state->reflogs; i++)
		free(state->reflog[i]);
	free(state->reflog);
}

#define MAIN_NO_COMMIT_REFS 1
#define main_check_commit_refs(line)	!((line)->user_flags & MAIN_NO_COMMIT_REFS)
#define main_mark_no_commit_refs(line)	((line)->user_flags |= MAIN_NO_COMMIT_REFS)

static inline struct ref_list *
main_get_commit_refs(struct line *line, struct commit *commit)
{
	struct ref_list *refs = NULL;

	if (main_check_commit_refs(line) && !(refs = get_ref_list(commit->id)))
		main_mark_no_commit_refs(line);

	return refs;
}

static bool
main_draw(struct view *view, struct line *line, unsigned int lineno)
{
	struct main_state *state = view->private;
	struct commit *commit = line->data;
	struct ref_list *refs = NULL;

	if (!commit->author)
		return FALSE;

	if (draw_lineno(view, lineno))
		return TRUE;

	if (opt_show_id) {
		if (state->reflogs) {
			const char *id = state->reflog[line->lineno - 1];

			if (draw_id_custom(view, LINE_ID, id, state->reflog_width))
				return TRUE;
		} else if (draw_id(view, commit->id)) {
			return TRUE;
		}
	}

	if (draw_date(view, &commit->time))
		return TRUE;

	if (draw_author(view, commit->author))
		return TRUE;

	if (state->with_graph && draw_graph(view, &commit->graph))
		return TRUE;

	if ((refs = main_get_commit_refs(line, commit)) && draw_refs(view, refs))
		return TRUE;

	if (commit->title)
		draw_commit_title(view, commit->title, 0);
	return TRUE;
}

static bool
main_add_reflog(struct view *view, struct main_state *state, char *reflog)
{
	char *end = strchr(reflog, ' ');
	int id_width;

	if (!end)
		return FALSE;
	*end = 0;

	if (!realloc_reflogs(&state->reflog, state->reflogs, 1)
	    || !(reflog = strdup(reflog)))
		return FALSE;

	state->reflog[state->reflogs++] = reflog;
	id_width = strlen(reflog);
	if (state->reflog_width < id_width) {
		state->reflog_width = id_width;
		if (opt_show_id)
			view->force_redraw = TRUE;
	}

	return TRUE;
}

/* Reads git log --pretty=raw output and parses it into the commit struct. */
static bool
main_read(struct view *view, char *line)
{
	struct main_state *state = view->private;
	struct graph *graph = &state->graph;
	enum line_type type;
	struct commit *commit = &state->current;

	if (!line) {
		main_flush_commit(view, commit);

		if (!view->lines && !view->prev)
			die("No revisions match the given arguments.");
		if (view->lines > 0) {
			struct commit *last = view->line[view->lines - 1].data;

			view->line[view->lines - 1].dirty = 1;
			if (!last->author) {
				view->lines--;
				free(last);
			}
		}

		if (state->with_graph)
			done_graph(graph);
		return TRUE;
	}

	type = get_line_type(line);
	if (type == LINE_COMMIT) {
		bool is_boundary;

		state->in_header = TRUE;
		line += STRING_SIZE("commit ");
		is_boundary = *line == '-';
		while (*line && !isalnum(*line))
			line++;

		if (!state->added_changes_commits && opt_show_changes && opt_is_inside_work_tree)
			main_add_changes_commits(view, state, line);
		else
			main_flush_commit(view, commit);

		main_register_commit(view, &state->current, line, is_boundary);
		return TRUE;
	}

	if (!*commit->id)
		return TRUE;

	/* Empty line separates the commit header from the log itself. */
	if (*line == '\0')
		state->in_header = FALSE;

	switch (type) {
	case LINE_PP_REFLOG:
		if (!main_add_reflog(view, state, line + STRING_SIZE("Reflog: ")))
			return FALSE;
		break;

	case LINE_PP_REFLOGMSG:
		line += STRING_SIZE("Reflog message: ");
		string_ncopy(state->reflogmsg, line, strlen(line));
		break;

	case LINE_PARENT:
		if (state->with_graph && !graph->has_parents)
			graph_add_parent(graph, line + STRING_SIZE("parent "));
		break;

	case LINE_AUTHOR:
		parse_author_line(line + STRING_SIZE("author "),
				  &commit->author, &commit->time);
		if (state->with_graph)
			graph_render_parents(graph);
		break;

	default:
		/* Fill in the commit title if it has not already been set. */
		if (*commit->title)
			break;

		/* Skip lines in the commit header. */
		if (state->in_header)
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
		if (*state->reflogmsg)
			line = state->reflogmsg;
		main_add_commit(view, LINE_MAIN_COMMIT, commit, line, FALSE);
	}

	return TRUE;
}

static enum request
main_request(struct view *view, enum request request, struct line *line)
{
	enum open_flags flags = (view_is_displayed(view) && request != REQ_VIEW_DIFF)
				? OPEN_SPLIT : OPEN_DEFAULT;

	switch (request) {
	case REQ_NEXT:
	case REQ_PREVIOUS:
		if (view_is_displayed(view) && display[0] != view)
			return request;
		/* Do not pass navigation requests to the branch view
		 * when the main view is maximized. (GH #38) */
		return request == REQ_NEXT ? REQ_MOVE_DOWN : REQ_MOVE_UP;

	case REQ_VIEW_DIFF:
	case REQ_ENTER:
		if (view_is_displayed(view) && display[0] != view)
			maximize_view(view, TRUE);

		if (line->type == LINE_STAT_UNSTAGED
		    || line->type == LINE_STAT_STAGED) {
			struct view *diff = VIEW(REQ_VIEW_DIFF);
			const char *diff_staged_argv[] = {
				GIT_DIFF_STAGED(encoding_arg,
					opt_diff_context_arg,
					opt_ignore_space_arg, NULL, NULL)
			};
			const char *diff_unstaged_argv[] = {
				GIT_DIFF_UNSTAGED(encoding_arg,
					opt_diff_context_arg,
					opt_ignore_space_arg, NULL, NULL)
			};
			const char **diff_argv = line->type == LINE_STAT_STAGED
				? diff_staged_argv : diff_unstaged_argv;

			open_argv(view, diff, diff_argv, NULL, flags);
			break;
		}

		open_view(view, REQ_VIEW_DIFF, flags);
		break;

	case REQ_REFRESH:
		load_refs(TRUE);
		refresh_view(view);
		break;

	case REQ_JUMP_COMMIT:
	{
		int lineno;

		for (lineno = 0; lineno < view->lines; lineno++) {
			struct commit *commit = view->line[lineno].data;

			if (!strncasecmp(commit->id, opt_search, strlen(opt_search))) {
				select_view_line(view, lineno);
				report_clear();
				return REQ_NONE;
			}
		}

		report("Unable to find commit '%s'", opt_search);
		break;
	}
	default:
		return request;
	}

	return REQ_NONE;
}

static bool
grep_refs(struct line *line, struct commit *commit, regex_t *regex)
{
	struct ref_list *list;
	regmatch_t pmatch;
	size_t i;

	if (!opt_show_refs || !(list = main_get_commit_refs(line, commit)))
		return FALSE;

	for (i = 0; i < list->size; i++) {
		if (!regexec(regex, list->refs[i]->name, 1, &pmatch, 0))
			return TRUE;
	}

	return FALSE;
}

static bool
main_grep(struct view *view, struct line *line)
{
	struct commit *commit = line->data;
	const char *text[] = {
		commit->id,
		commit->title,
		mkauthor(commit->author, opt_author_width, opt_author),
		mkdate(&commit->time, opt_date),
		NULL
	};

	return grep_text(view, text) || grep_refs(line, commit, view->regex);
}

static struct ref *
main_get_commit_branch(struct line *line, struct commit *commit)
{
	struct ref_list *list = main_get_commit_refs(line, commit);
	struct ref *branch = NULL;
	size_t i;

	for (i = 0; list && i < list->size; i++) {
		struct ref *ref = list->refs[i];

		switch (get_line_type_from_ref(ref)) {
		case LINE_MAIN_HEAD:
		case LINE_MAIN_REF:
			/* Always prefer local branches. */
			return ref;

		default:
			branch = ref;
		}
	}

	return branch;
}

static void
main_select(struct view *view, struct line *line)
{
	struct commit *commit = line->data;

	if (line->type == LINE_STAT_STAGED || line->type == LINE_STAT_UNSTAGED) {
		string_ncopy(view->ref, commit->title, strlen(commit->title));
	} else {
		struct ref *branch = main_get_commit_branch(line, commit);

		if (branch)
			string_copy_rev(ref_branch, branch->name);
		string_copy_rev(view->ref, commit->id);
	}
	string_copy_rev(ref_commit, commit->id);
}

static struct view_ops main_ops = {
	"commit",
	{ "main" },
	VIEW_SEND_CHILD_ENTER | VIEW_FILE_FILTER | VIEW_LOG_LIKE | VIEW_REFRESH,
	sizeof(struct main_state),
	main_open,
	main_read,
	main_draw,
	main_request,
	main_grep,
	main_select,
	main_done,
};

static bool
stash_open(struct view *view, enum open_flags flags)
{
	static const char *stash_argv[] = { "git", "stash", "list",
		encoding_arg, "--no-color", "--pretty=raw", NULL };
	struct main_state *state = view->private;

	state->added_changes_commits = TRUE;
	state->with_graph = FALSE;
	return begin_update(view, NULL, stash_argv, flags | OPEN_RELOAD);
}

static void
stash_select(struct view *view, struct line *line)
{
	main_select(view, line);
	string_format(ref_stash, "stash@{%d}", line->lineno - 1);
	string_copy(view->ref, ref_stash);
}

static struct view_ops stash_ops = {
	"stash",
	{ "stash" },
	VIEW_SEND_CHILD_ENTER | VIEW_REFRESH,
	sizeof(struct main_state),
	stash_open,
	main_read,
	main_draw,
	main_request,
	main_grep,
	stash_select,
};

/*
 * Status management
 */

/* Whether or not the curses interface has been initialized. */
static bool cursed = FALSE;

/* Terminal hacks and workarounds. */
static bool use_scroll_redrawwin;
static bool use_scroll_status_wclear;

/* The status window is used for polling keystrokes. */
static WINDOW *status_win;

/* Reading from the prompt? */
static bool input_mode = FALSE;

static bool status_empty = FALSE;

/* Update status and title window. */
static void
report(const char *msg, ...)
{
	struct view *view = display[current_view];

	if (input_mode)
		return;

	if (!view) {
		char buf[SIZEOF_STR];
		int retval;

		FORMAT_BUFFER(buf, sizeof(buf), msg, retval, TRUE);
		die("%s", buf);
	}

	if (!status_empty || *msg) {
		va_list args;

		va_start(args, msg);

		wmove(status_win, 0, 0);
		if (view->has_scrolled && use_scroll_status_wclear)
			wclear(status_win);
		if (*msg) {
			vwprintw(status_win, msg, args);
			status_empty = FALSE;
		} else {
			status_empty = TRUE;
		}
		wclrtoeol(status_win);
		wnoutrefresh(status_win);

		va_end(args);
	}

	update_view_title(view);
}

static void
done_display(void)
{
	endwin();
}

static void
init_display(void)
{
	const char *term;
	int x, y;

	die_callback = done_display;

	/* Initialize the curses library */
	if (isatty(STDIN_FILENO)) {
		cursed = !!initscr();
		opt_tty = stdin;
	} else {
		/* Leave stdin and stdout alone when acting as a pager. */
		opt_tty = fopen("/dev/tty", "r+");
		if (!opt_tty)
			die("Failed to open /dev/tty");
		cursed = !!newterm(NULL, opt_tty, opt_tty);
	}

	if (!cursed)
		die("Failed to initialize curses");

	nonl();		/* Disable conversion and detect newlines from input. */
	cbreak();       /* Take input chars one at a time, no wait for \n */
	noecho();       /* Don't echo input */
	leaveok(stdscr, FALSE);

	if (has_colors())
		init_colors();

	getmaxyx(stdscr, y, x);
	status_win = newwin(1, x, y - 1, 0);
	if (!status_win)
		die("Failed to create status window");

	/* Enable keyboard mapping */
	keypad(status_win, TRUE);
	wbkgdset(status_win, get_line_attr(LINE_STATUS));
#ifdef NCURSES_MOUSE_VERSION
	/* Enable mouse */
	if (opt_mouse){
		mousemask(ALL_MOUSE_EVENTS, NULL);
		mouseinterval(0);
	}
#endif

#if defined(NCURSES_VERSION_PATCH) && (NCURSES_VERSION_PATCH >= 20080119)
	set_tabsize(opt_tab_size);
#else
	TABSIZE = opt_tab_size;
#endif

	term = getenv("XTERM_VERSION") ? NULL : getenv("COLORTERM");
	if (term && !strcmp(term, "gnome-terminal")) {
		/* In the gnome-terminal-emulator, the message from
		 * scrolling up one line when impossible followed by
		 * scrolling down one line causes corruption of the
		 * status line. This is fixed by calling wclear. */
		use_scroll_status_wclear = TRUE;
		use_scroll_redrawwin = FALSE;

	} else if (term && !strcmp(term, "xrvt-xpm")) {
		/* No problems with full optimizations in xrvt-(unicode)
		 * and aterm. */
		use_scroll_status_wclear = use_scroll_redrawwin = FALSE;

	} else {
		/* When scrolling in (u)xterm the last line in the
		 * scrolling direction will update slowly. */
		use_scroll_redrawwin = TRUE;
		use_scroll_status_wclear = FALSE;
	}
}

static int
get_input(int prompt_position)
{
	struct view *view;
	int i, key, cursor_y, cursor_x;

	if (prompt_position)
		input_mode = TRUE;

	while (TRUE) {
		bool loading = FALSE;

		foreach_view (view, i) {
			update_view(view);
			if (view_is_displayed(view) && view->has_scrolled &&
			    use_scroll_redrawwin)
				redrawwin(view->win);
			view->has_scrolled = FALSE;
			if (view->pipe)
				loading = TRUE;
		}

		/* Update the cursor position. */
		if (prompt_position) {
			getbegyx(status_win, cursor_y, cursor_x);
			cursor_x = prompt_position;
		} else {
			view = display[current_view];
			getbegyx(view->win, cursor_y, cursor_x);
			cursor_x = view->width - 1;
			cursor_y += view->pos.lineno - view->pos.offset;
		}
		setsyx(cursor_y, cursor_x);

		/* Refresh, accept single keystroke of input */
		doupdate();
		nodelay(status_win, loading);
		key = wgetch(status_win);

		/* wgetch() with nodelay() enabled returns ERR when
		 * there's no input. */
		if (key == ERR) {

		} else if (key == KEY_RESIZE) {
			int height, width;

			getmaxyx(stdscr, height, width);

			wresize(status_win, 1, width);
			mvwin(status_win, height - 1, 0);
			wnoutrefresh(status_win);
			resize_display();
			redraw_display(TRUE);

		} else {
			input_mode = FALSE;
			if (key == erasechar())
				key = KEY_BACKSPACE;
			return key;
		}
	}
}

static char *
prompt_input(const char *prompt, input_handler handler, void *data)
{
	enum input_status status = INPUT_OK;
	static char buf[SIZEOF_STR];
	size_t pos = 0;

	buf[pos] = 0;

	while (status == INPUT_OK || status == INPUT_SKIP) {
		int key;

		mvwprintw(status_win, 0, 0, "%s%.*s", prompt, pos, buf);
		wclrtoeol(status_win);

		key = get_input(pos + 1);
		switch (key) {
		case KEY_RETURN:
		case KEY_ENTER:
		case '\n':
			status = pos ? INPUT_STOP : INPUT_CANCEL;
			break;

		case KEY_BACKSPACE:
			if (pos > 0)
				buf[--pos] = 0;
			else
				status = INPUT_CANCEL;
			break;

		case KEY_ESC:
			status = INPUT_CANCEL;
			break;

		default:
			if (pos >= sizeof(buf)) {
				report("Input string too long");
				return NULL;
			}

			status = handler(data, buf, key);
			if (status == INPUT_OK)
				buf[pos++] = (char) key;
		}
	}

	/* Clear the status window */
	status_empty = FALSE;
	report_clear();

	if (status == INPUT_CANCEL)
		return NULL;

	buf[pos++] = 0;

	return buf;
}

static enum input_status
prompt_yesno_handler(void *data, char *buf, int c)
{
	if (c == 'y' || c == 'Y')
		return INPUT_STOP;
	if (c == 'n' || c == 'N')
		return INPUT_CANCEL;
	return INPUT_SKIP;
}

static bool
prompt_yesno(const char *prompt)
{
	char prompt2[SIZEOF_STR];

	if (!string_format(prompt2, "%s [Yy/Nn]", prompt))
		return FALSE;

	return !!prompt_input(prompt2, prompt_yesno_handler, NULL);
}

static enum input_status
read_prompt_handler(void *data, char *buf, int c)
{
	return isprint(c) ? INPUT_OK : INPUT_SKIP;
}

static char *
read_prompt(const char *prompt)
{
	return prompt_input(prompt, read_prompt_handler, NULL);
}

static bool prompt_menu(const char *prompt, const struct menu_item *items, int *selected)
{
	enum input_status status = INPUT_OK;
	int size = 0;

	while (items[size].text)
		size++;

	assert(size > 0);

	while (status == INPUT_OK) {
		const struct menu_item *item = &items[*selected];
		int key;
		int i;

		mvwprintw(status_win, 0, 0, "%s (%d of %d) ",
			  prompt, *selected + 1, size);
		if (item->hotkey)
			wprintw(status_win, "[%c] ", (char) item->hotkey);
		wprintw(status_win, "%s", item->text);
		wclrtoeol(status_win);

		key = get_input(COLS - 1);
		switch (key) {
		case KEY_RETURN:
		case KEY_ENTER:
		case '\n':
			status = INPUT_STOP;
			break;

		case KEY_LEFT:
		case KEY_UP:
			*selected = *selected - 1;
			if (*selected < 0)
				*selected = size - 1;
			break;

		case KEY_RIGHT:
		case KEY_DOWN:
			*selected = (*selected + 1) % size;
			break;

		case KEY_ESC:
			status = INPUT_CANCEL;
			break;

		default:
			for (i = 0; items[i].text; i++)
				if (items[i].hotkey == key) {
					*selected = i;
					status = INPUT_STOP;
					break;
				}
		}
	}

	/* Clear the status window */
	status_empty = FALSE;
	report_clear();

	return status != INPUT_CANCEL;
}

/*
 * Repository properties
 */


static void
set_remote_branch(const char *name, const char *value, size_t valuelen)
{
	if (!strcmp(name, ".remote")) {
		string_ncopy(opt_remote, value, valuelen);

	} else if (*opt_remote && !strcmp(name, ".merge")) {
		size_t from = strlen(opt_remote);

		if (!prefixcmp(value, "refs/heads/"))
			value += STRING_SIZE("refs/heads/");

		if (!string_format_from(opt_remote, &from, "/%s", value))
			opt_remote[0] = 0;
	}
}

static void
set_repo_config_option(char *name, char *value, enum status_code (*cmd)(int, const char **))
{
	const char *argv[SIZEOF_ARG] = { name, "=" };
	int argc = 1 + (cmd == option_set_command);
	enum status_code error;

	if (!argv_from_string(argv, &argc, value))
		error = ERROR_TOO_MANY_OPTION_ARGUMENTS;
	else
		error = cmd(argc, argv);

	if (error != SUCCESS)
		warn("Option 'tig.%s': %s", name, get_status_message(error));
}

static void
set_work_tree(const char *value)
{
	char cwd[SIZEOF_STR];

	if (!getcwd(cwd, sizeof(cwd)))
		die("Failed to get cwd path: %s", strerror(errno));
	if (chdir(cwd) < 0)
		die("Failed to chdir(%s): %s", cwd, strerror(errno));
	if (chdir(opt_git_dir) < 0)
		die("Failed to chdir(%s): %s", opt_git_dir, strerror(errno));
	if (!getcwd(opt_git_dir, sizeof(opt_git_dir)))
		die("Failed to get git path: %s", strerror(errno));
	if (chdir(value) < 0)
		die("Failed to chdir(%s): %s", value, strerror(errno));
	if (!getcwd(cwd, sizeof(cwd)))
		die("Failed to get cwd path: %s", strerror(errno));
	if (setenv("GIT_WORK_TREE", cwd, TRUE))
		die("Failed to set GIT_WORK_TREE to '%s'", cwd);
	if (setenv("GIT_DIR", opt_git_dir, TRUE))
		die("Failed to set GIT_DIR to '%s'", opt_git_dir);
	opt_is_inside_work_tree = TRUE;
}

static void
parse_git_color_option(enum line_type type, char *value)
{
	struct line_info *info = &line_info[type];
	const char *argv[SIZEOF_ARG];
	int argc = 0;
	bool first_color = TRUE;
	int i;

	if (!argv_from_string(argv, &argc, value))
		return;

	info->fg = COLOR_DEFAULT;
	info->bg = COLOR_DEFAULT;
	info->attr = 0;

	for (i = 0; i < argc; i++) {
		int attr = 0;

		if (set_attribute(&attr, argv[i])) {
			info->attr |= attr;

		} else if (set_color(&attr, argv[i])) {
			if (first_color)
				info->fg = attr;
			else
				info->bg = attr;
			first_color = FALSE;
		}
	}
}

static void
set_git_color_option(const char *name, char *value)
{
	static const struct enum_map_entry color_option_map[] = {
		ENUM_MAP_ENTRY("branch.current", LINE_MAIN_HEAD),
		ENUM_MAP_ENTRY("branch.local", LINE_MAIN_REF),
		ENUM_MAP_ENTRY("branch.plain", LINE_MAIN_REF),
		ENUM_MAP_ENTRY("branch.remote", LINE_MAIN_REMOTE),

		ENUM_MAP_ENTRY("diff.meta", LINE_DIFF_HEADER),
		ENUM_MAP_ENTRY("diff.meta", LINE_DIFF_INDEX),
		ENUM_MAP_ENTRY("diff.meta", LINE_DIFF_OLDMODE),
		ENUM_MAP_ENTRY("diff.meta", LINE_DIFF_NEWMODE),
		ENUM_MAP_ENTRY("diff.frag", LINE_DIFF_CHUNK),
		ENUM_MAP_ENTRY("diff.old", LINE_DIFF_DEL),
		ENUM_MAP_ENTRY("diff.new", LINE_DIFF_ADD),

		//ENUM_MAP_ENTRY("diff.commit", LINE_DIFF_ADD),

		ENUM_MAP_ENTRY("status.branch", LINE_STAT_HEAD),
		//ENUM_MAP_ENTRY("status.nobranch", LINE_STAT_HEAD),
		ENUM_MAP_ENTRY("status.added", LINE_STAT_STAGED),
		ENUM_MAP_ENTRY("status.updated", LINE_STAT_STAGED),
		ENUM_MAP_ENTRY("status.changed", LINE_STAT_UNSTAGED),
		ENUM_MAP_ENTRY("status.untracked", LINE_STAT_UNTRACKED),
	};
	int type = LINE_NONE;

	if (opt_read_git_colors && map_enum(&type, color_option_map, name)) {
		parse_git_color_option(type, value);
	}
}

static void
set_encoding(struct encoding **encoding_ref, const char *arg, bool priority)
{
	if (parse_encoding(encoding_ref, arg, priority) == SUCCESS)
		encoding_arg[0] = 0;
}

static int
read_repo_config_option(char *name, size_t namelen, char *value, size_t valuelen, void *data)
{
	if (!strcmp(name, "i18n.commitencoding"))
		set_encoding(&default_encoding, value, FALSE);

	else if (!strcmp(name, "gui.encoding"))
		set_encoding(&default_encoding, value, TRUE);

	else if (!strcmp(name, "core.editor"))
		string_ncopy(opt_editor, value, valuelen);

	else if (!strcmp(name, "core.worktree"))
		set_work_tree(value);

	else if (!strcmp(name, "core.abbrev"))
		parse_id(&opt_id_cols, value);

	else if (!prefixcmp(name, "tig.color."))
		set_repo_config_option(name + 10, value, option_color_command);

	else if (!prefixcmp(name, "tig.bind."))
		set_repo_config_option(name + 9, value, option_bind_command);

	else if (!prefixcmp(name, "tig."))
		set_repo_config_option(name + 4, value, option_set_command);

	else if (!prefixcmp(name, "color."))
		set_git_color_option(name + STRING_SIZE("color."), value);

	else if (*opt_head && !prefixcmp(name, "branch.") &&
		 !strncmp(name + 7, opt_head, strlen(opt_head)))
		set_remote_branch(name + 7 + strlen(opt_head), value, valuelen);

	return OK;
}

static int
load_git_config(void)
{
	const char *config_list_argv[] = { "git", "config", "--list", NULL };

	return io_run_load(config_list_argv, "=", read_repo_config_option, NULL);
}

#define REPO_INFO_GIT_DIR	"--git-dir"
#define REPO_INFO_WORK_TREE	"--is-inside-work-tree"
#define REPO_INFO_SHOW_CDUP	"--show-cdup"
#define REPO_INFO_SHOW_PREFIX	"--show-prefix"
#define REPO_INFO_SYMBOLIC_HEAD	"--symbolic-full-name"
#define REPO_INFO_RESOLVED_HEAD	"HEAD"

struct repo_info_state {
	const char **argv;
	char head_id[SIZEOF_REV];
};

static int
read_repo_info(char *name, size_t namelen, char *value, size_t valuelen, void *data)
{
	struct repo_info_state *state = data;
	const char *arg = *state->argv ? *state->argv++ : "";

	if (!strcmp(arg, REPO_INFO_GIT_DIR)) {
		string_ncopy(opt_git_dir, name, namelen);

	} else if (!strcmp(arg, REPO_INFO_WORK_TREE)) {
		/* This can be 3 different values depending on the
		 * version of git being used. If git-rev-parse does not
		 * understand --is-inside-work-tree it will simply echo
		 * the option else either "true" or "false" is printed.
		 * Default to true for the unknown case. */
		opt_is_inside_work_tree = strcmp(name, "false") ? TRUE : FALSE;

	} else if (!strcmp(arg, REPO_INFO_SHOW_CDUP)) {
		string_ncopy(opt_cdup, name, namelen);

	} else if (!strcmp(arg, REPO_INFO_SHOW_PREFIX)) {
		string_ncopy(opt_prefix, name, namelen);

	} else if (!strcmp(arg, REPO_INFO_RESOLVED_HEAD)) {
		string_ncopy(state->head_id, name, namelen);

	} else if (!strcmp(arg, REPO_INFO_SYMBOLIC_HEAD)) {
	    	if (!prefixcmp(name, "refs/heads/")) {
			char *offset = name + STRING_SIZE("refs/heads/");

			string_ncopy(opt_head, offset, strlen(offset) + 1);
			add_ref(state->head_id, name, opt_remote, opt_head);
		}
		state->argv++;
	}

	return OK;
}

static int
load_repo_info(void)
{
	const char *rev_parse_argv[] = {
		"git", "rev-parse", REPO_INFO_GIT_DIR, REPO_INFO_WORK_TREE,
			REPO_INFO_SHOW_CDUP, REPO_INFO_SHOW_PREFIX, \
			REPO_INFO_RESOLVED_HEAD, REPO_INFO_SYMBOLIC_HEAD, "HEAD",
			NULL
	};
	struct repo_info_state state = { rev_parse_argv + 2 };

	return io_run_load(rev_parse_argv, "=", read_repo_info, &state);
}


/*
 * Main
 */

static const char usage[] =
"tig " TIG_VERSION " (" __DATE__ ")\n"
"\n"
"Usage: tig        [options] [revs] [--] [paths]\n"
"   or: tig log    [options] [revs] [--] [paths]\n"
"   or: tig show   [options] [revs] [--] [paths]\n"
"   or: tig blame  [options] [rev] [--] path\n"
"   or: tig stash\n"
"   or: tig status\n"
"   or: tig <      [git command output]\n"
"\n"
"Options:\n"
"  +<number>       Select line <number> in the first view\n"
"  -v, --version   Show version and exit\n"
"  -h, --help      Show help message and exit";

static void TIG_NORETURN
quit(int sig)
{
	if (sig)
		signal(sig, SIG_DFL);

	/* XXX: Restore tty modes and let the OS cleanup the rest! */
	if (cursed)
		endwin();
	exit(0);
}

static int
read_filter_args(char *name, size_t namelen, char *value, size_t valuelen, void *data)
{
	const char ***filter_args = data;

	return argv_append(filter_args, name) ? OK : ERR;
}

static void
filter_rev_parse(const char ***args, const char *arg1, const char *arg2, const char *argv[])
{
	const char *rev_parse_argv[SIZEOF_ARG] = { "git", "rev-parse", arg1, arg2 };
	const char **all_argv = NULL;

	if (!argv_append_array(&all_argv, rev_parse_argv) ||
	    !argv_append_array(&all_argv, argv) ||
	    io_run_load(all_argv, "\n", read_filter_args, args) == ERR)
		die("Failed to split arguments");
	argv_free(all_argv);
	free(all_argv);
}

static bool
is_rev_flag(const char *flag)
{
	static const char *rev_flags[] = { GIT_REV_FLAGS };
	int i;

	for (i = 0; i < ARRAY_SIZE(rev_flags); i++)
		if (!strcmp(flag, rev_flags[i]))
			return TRUE;

	return FALSE;
}

static void
filter_options(const char *argv[], bool blame)
{
	const char **flags = NULL;
	int next, flags_pos;

	for (next = flags_pos = 0; argv[next]; next++) {
		const char *flag = argv[next];
		int value = -1;

		if (map_enum(&value, commit_order_arg_map, flag)) {
			opt_commit_order = value;
			update_commit_order_arg();
			continue;
		}

		if (map_enum(&value, ignore_space_arg_map, flag)) {
			opt_ignore_space = value;
			update_ignore_space_arg();
			continue;
		}

		if (!prefixcmp(flag, "-U")
		    && parse_int(&value, flag + 2, 0, 999999) == SUCCESS) {
			opt_diff_context = value;
			update_diff_context_arg(opt_diff_context);
			continue;
		}

		argv[flags_pos++] = flag;
	}

	argv[flags_pos] = NULL;

	filter_rev_parse(&opt_file_argv, "--no-revs", "--no-flags", argv);
	filter_rev_parse(&flags, "--flags", "--no-revs", argv);

	if (flags) {
		for (next = flags_pos = 0; flags && flags[next]; next++) {
			const char *flag = flags[next];

			if (is_rev_flag(flag))
				argv_append(&opt_rev_argv, flag);
			else
				flags[flags_pos++] = flag;
		}

		flags[flags_pos] = NULL;

		if (blame)
			opt_blame_argv = flags;
		else
			opt_cmdline_argv = flags;
	}

	filter_rev_parse(&opt_rev_argv, "--symbolic", "--revs-only", argv);
}

static enum request
parse_options(int argc, const char *argv[], bool pager_mode)
{
	enum request request;
	const char *subcommand;
	bool seen_dashdash = FALSE;
	const char **filter_argv = NULL;
	int i;

	request = pager_mode ? REQ_VIEW_PAGER : REQ_VIEW_MAIN;

	if (argc <= 1)
		return request;

	subcommand = argv[1];
	if (!strcmp(subcommand, "status")) {
		request = REQ_VIEW_STATUS;

	} else if (!strcmp(subcommand, "blame")) {
		request = REQ_VIEW_BLAME;

	} else if (!strcmp(subcommand, "show")) {
		request = REQ_VIEW_DIFF;

	} else if (!strcmp(subcommand, "log")) {
		request = REQ_VIEW_LOG;

	} else if (!strcmp(subcommand, "stash")) {
		request = REQ_VIEW_STASH;

	} else {
		subcommand = NULL;
	}

	for (i = 1 + !!subcommand; i < argc; i++) {
		const char *opt = argv[i];

		// stop parsing our options after -- and let rev-parse handle the rest
		if (!seen_dashdash) {
			if (!strcmp(opt, "--")) {
				seen_dashdash = TRUE;
				continue;

			} else if (!strcmp(opt, "-v") || !strcmp(opt, "--version")) {
				printf("tig version %s\n", TIG_VERSION);
				quit(0);

			} else if (!strcmp(opt, "-h") || !strcmp(opt, "--help")) {
				printf("%s\n", usage);
				quit(0);

			} else if (strlen(opt) >= 2 && *opt == '+' && string_isnumber(opt + 1)) {
				opt_lineno = atoi(opt + 1);
				continue;

			}
		}

		if (!argv_append(&filter_argv, opt))
			die("command too long");
	}

	if (filter_argv)
		filter_options(filter_argv, request == REQ_VIEW_BLAME);

	/* Finish validating and setting up blame options */
	if (request == REQ_VIEW_BLAME) {
		if (!opt_file_argv || opt_file_argv[1] || (opt_rev_argv && opt_rev_argv[1]))
			die("invalid number of options to blame\n\n%s", usage);

		if (opt_rev_argv) {
			string_ncopy(opt_ref, opt_rev_argv[0], strlen(opt_rev_argv[0]));
		}

		string_ncopy(opt_file, opt_file_argv[0], strlen(opt_file_argv[0]));
	}

	return request;
}

static enum request
open_pager_mode(enum request request)
{
	enum open_flags flags = OPEN_DEFAULT;

	if (request == REQ_VIEW_PAGER) {
		/* Detect if the user requested the main view. */
		if (argv_contains(opt_rev_argv, "--stdin")) {
			request = REQ_VIEW_MAIN;
			flags |= OPEN_FORWARD_STDIN;
		} else if (argv_contains(opt_cmdline_argv, "--pretty=raw")) {
			request = REQ_VIEW_MAIN;
			flags |= OPEN_STDIN;
		} else {
			flags |= OPEN_STDIN;
		}

	} else if (request == REQ_VIEW_DIFF) {
		if (argv_contains(opt_rev_argv, "--stdin"))
			flags |= OPEN_FORWARD_STDIN;
	}

	/* Open the requested view even if the pager mode is enabled so
	 * the warning message below is displayed correctly. */
	open_view(NULL, request, flags);

	if (!open_in_pager_mode(flags)) {
		close(STDIN_FILENO);
		report("Ignoring stdin.");
	}

	return REQ_NONE;
}

static enum request
run_prompt_command(struct view *view, char *cmd)
{
	enum request request;

	if (cmd && string_isnumber(cmd)) {
		int lineno = view->pos.lineno + 1;

		if (parse_int(&lineno, cmd, 1, view->lines + 1) == SUCCESS) {
			select_view_line(view, lineno - 1);
			report_clear();
		} else {
			report("Unable to parse '%s' as a line number", cmd);
		}
	} else if (cmd && iscommit(cmd)) {
		string_ncopy(opt_search, cmd, strlen(cmd));

		request = view_request(view, REQ_JUMP_COMMIT);
		if (request == REQ_JUMP_COMMIT) {
			report("Jumping to commits is not supported by the '%s' view", view->name);
		}

	} else if (cmd && strlen(cmd) == 1) {
		request = get_keybinding(&view->ops->keymap, cmd[0]);
		return request;

	} else if (cmd && cmd[0] == '!') {
		struct view *next = VIEW(REQ_VIEW_PAGER);
		const char *argv[SIZEOF_ARG];
		int argc = 0;

		cmd++;
		/* When running random commands, initially show the
		 * command in the title. However, it maybe later be
		 * overwritten if a commit line is selected. */
		string_ncopy(next->ref, cmd, strlen(cmd));

		if (!argv_from_string(argv, &argc, cmd)) {
			report("Too many arguments");
		} else if (!format_argv(view, &next->argv, argv, FALSE, TRUE)) {
			report("Argument formatting failed");
		} else {
			next->dir = NULL;
			open_view(view, REQ_VIEW_PAGER, OPEN_PREPARED);
		}

	} else if (cmd) {
		request = get_request(cmd);
		if (request != REQ_UNKNOWN)
			return request;

		char *args = strchr(cmd, ' ');
		if (args) {
			*args++ = 0;
			if (set_option(cmd, args) == SUCCESS) {
				request = !view->unrefreshable ? REQ_REFRESH : REQ_SCREEN_REDRAW;
				if (!strcmp(cmd, "color"))
					init_colors();
			}
		}
		return request;
	}
	return REQ_NONE;
}

#ifdef NCURSES_MOUSE_VERSION
static struct view *
find_clicked_view(MEVENT *event)
{
	struct view *view;
	int i;

	foreach_displayed_view (view, i) {
		int beg_y = 0, beg_x = 0;

		getbegyx(view->win, beg_y, beg_x);

		if (beg_y <= event->y && event->y < beg_y + view->height
		    && beg_x <= event->x && event->x < beg_x + view->width) {
			if (i != current_view) {
				current_view = i;
			}
			return view;
		}
	}

	return NULL;
}

static enum request
handle_mouse_event(void)
{
	MEVENT event;
	struct view *view;

	if (getmouse(&event) != OK)
		return REQ_NONE;

	view = find_clicked_view(&event);
	if (!view)
		return REQ_NONE;

	if (event.bstate & BUTTON2_PRESSED)
		return REQ_SCROLL_WHEEL_DOWN;

	if (event.bstate & BUTTON4_PRESSED)
		return REQ_SCROLL_WHEEL_UP;

	if (event.bstate & BUTTON1_PRESSED) {
		if (event.y == view->pos.lineno - view->pos.offset) {
			/* Click is on the same line, perform an "ENTER" */
			return REQ_ENTER;

		} else {
			int y = getbegy(view->win);
			unsigned long lineno = (event.y - y) + view->pos.offset;

			select_view_line(view, lineno);
			update_view_title(view);
			report_clear();
		}
	}

	return REQ_NONE;
}
#endif

int
main(int argc, const char *argv[])
{
	const char *codeset = ENCODING_UTF8;
	bool pager_mode = !isatty(STDIN_FILENO);
	enum request request = parse_options(argc, argv, pager_mode);
	struct view *view;
	int i;

	signal(SIGINT, quit);
	signal(SIGQUIT, quit);
	signal(SIGPIPE, SIG_IGN);

	if (setlocale(LC_ALL, "")) {
		codeset = nl_langinfo(CODESET);
	}

	foreach_view(view, i) {
		add_keymap(&view->ops->keymap);
	}

	if (load_repo_info() == ERR)
		die("Failed to load repo info.");

	if (load_options() == ERR)
		die("Failed to load user config.");

	if (load_git_config() == ERR)
		die("Failed to load repo config.");

	/* Require a git repository unless when running in pager mode. */
	if (!opt_git_dir[0] && request != REQ_VIEW_PAGER)
		die("Not a git repository");

	if (codeset && strcmp(codeset, ENCODING_UTF8)) {
		char translit[SIZEOF_STR];

		if (string_format(translit, "%s%s", codeset, ICONV_TRANSLIT))
			opt_iconv_out = iconv_open(translit, ENCODING_UTF8);
		else
			opt_iconv_out = iconv_open(codeset, ENCODING_UTF8);
		if (opt_iconv_out == ICONV_NONE)
			die("Failed to initialize character set conversion");
	}

	if (load_refs(FALSE) == ERR)
		die("Failed to load refs.");

	init_display();

	if (pager_mode)
		request = open_pager_mode(request);

	while (view_driver(display[current_view], request)) {
		int key = get_input(0);

#ifdef NCURSES_MOUSE_VERSION
		if (key == KEY_MOUSE) {
			request = handle_mouse_event();
			continue;
		}
#endif

		if (key == KEY_ESC)
			key  = get_input(0) + 0x80;

		view = display[current_view];
		request = get_keybinding(&view->ops->keymap, key);

		/* Some low-level request handling. This keeps access to
		 * status_win restricted. */
		switch (request) {
		case REQ_NONE:
			report("Unknown key, press %s for help",
			       get_view_key(view, REQ_VIEW_HELP));
			break;
		case REQ_PROMPT:
		{
			char *cmd = read_prompt(":");
			request = run_prompt_command(view, cmd);
			break;
		}
		case REQ_SEARCH:
		case REQ_SEARCH_BACK:
		{
			const char *prompt = request == REQ_SEARCH ? "/" : "?";
			char *search = read_prompt(prompt);

			if (search)
				string_ncopy(opt_search, search, strlen(search));
			else if (*opt_search)
				request = request == REQ_SEARCH ?
					REQ_FIND_NEXT :
					REQ_FIND_PREV;
			else
				request = REQ_NONE;
			break;
		}
		default:
			break;
		}
	}

	quit(0);

	return 0;
}

/* vim: set ts=8 sw=8 noexpandtab: */
