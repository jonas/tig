/* Copyright (c) 2006-2015 Jonas Fonseca <jonas.fonseca@gmail.com>
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

#include "tig/tig.h"
#include "tig/types.h"
#include "tig/util.h"
#include "tig/parse.h"
#include "tig/io.h"
#include "tig/argv.h"
#include "tig/refdb.h"
#include "tig/watch.h"
#include "tig/graph.h"
#include "tig/git.h"
#include "tig/request.h"
#include "tig/line.h"
#include "tig/keys.h"
#include "tig/view.h"
#include "tig/search.h"
#include "tig/repo.h"
#include "tig/options.h"
#include "tig/draw.h"
#include "tig/display.h"
#include "tig/prompt.h"

/* Views. */
#include "tig/blame.h"
#include "tig/blob.h"
#include "tig/diff.h"
#include "tig/grep.h"
#include "tig/help.h"
#include "tig/log.h"
#include "tig/reflog.h"
#include "tig/main.h"
#include "tig/pager.h"
#include "tig/refs.h"
#include "tig/stage.h"
#include "tig/stash.h"
#include "tig/status.h"
#include "tig/tree.h"

#ifdef HAVE_READLINE
#include <readline/readline.h>
#endif /* HAVE_READLINE */

static bool
forward_request_to_child(struct view *child, enum request request)
{
	return displayed_views() == 2 && view_is_displayed(child) &&
		!strcmp(child->vid, child->ops->id);
}

static enum request
view_request(struct view *view, enum request request)
{
	if (!view || !view->lines)
		return request;

	if (request == REQ_ENTER && !opt_focus_child && opt_send_child_enter &&
	    view_has_flags(view, VIEW_SEND_CHILD_ENTER)) {
		struct view *child = display[1];

		if (forward_request_to_child(child, request)) {
			view_request(child, request);
			return REQ_NONE;
		}
	}

	if (request == REQ_REFRESH && !view_can_refresh(view)) {
		report("This view can not be refreshed");
		return REQ_NONE;
	}

	return view->ops->request(view, request, &view->line[view->pos.lineno]);
}

/*
 * Option management
 */

#define TOGGLE_MENU_INFO(_) \
	_('.', "line numbers",			"line-number"), \
	_('D', "dates",				"date"), \
	_('A', "author",			"author"), \
	_('~', "graphics",			"line-graphics"), \
	_('g', "revision graph",		"commit-title-graph"), \
	_('#', "file names",			"file-name"), \
	_('*', "file sizes",			"file-size"), \
	_('W', "space changes",			"ignore-space"), \
	_('l', "commit order",			"commit-order"), \
	_('F', "reference display",		"commit-title-refs"), \
	_('C', "local change display",		"show-changes"), \
	_('X', "commit ID display",		"id"), \
	_('%', "file filtering",		"file-filter"), \
	_('$', "commit title overflow display",	"commit-title-overflow"), \
	_('d', "untracked directory info",	"status-show-untracked-dirs"), \
	_('|', "view split",			"vertical-split"), \

static void
toggle_option(struct view *view)
{
	const struct menu_item menu[] = {
#define DEFINE_TOGGLE_MENU(key, help, name) { key, help, name }
		TOGGLE_MENU_INFO(DEFINE_TOGGLE_MENU)
		{ 0 }
	};
	const char *toggle_argv[] = { "toggle", NULL, NULL };
	int i = 0;

	if (!prompt_menu("Toggle option", menu, &i))
		return;

	toggle_argv[1] = menu[i].data;
	run_prompt_command(view, toggle_argv);
}


/*
 * View opening
 */

static enum request
open_run_request(struct view *view, enum request request)
{
	struct run_request *req = get_run_request(request);

	if (!req) {
		report("Unknown run request");
		return REQ_NONE;
	}

	return exec_run_request(view, req);
}

/*
 * User request switch noodle
 */

static bool
view_driver(struct view *view, enum request request)
{
	int i;

	if (request == REQ_NONE)
		return true;

	if (request >= REQ_RUN_REQUESTS) {
		request = open_run_request(view, request);

		// exit quickly rather than going through view_request and back
		if (request == REQ_QUIT)
			return false;
	}

	request = view_request(view, request);
	if (request == REQ_NONE)
		return true;

	switch (request) {
	case REQ_MOVE_UP:
	case REQ_MOVE_DOWN:
	case REQ_MOVE_PAGE_UP:
	case REQ_MOVE_PAGE_DOWN:
	case REQ_MOVE_HALF_PAGE_UP:
	case REQ_MOVE_HALF_PAGE_DOWN:
	case REQ_MOVE_FIRST_LINE:
	case REQ_MOVE_LAST_LINE:
	case REQ_MOVE_WHEEL_DOWN:
	case REQ_MOVE_WHEEL_UP:
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

	case REQ_VIEW_GREP:
		open_grep_view(view);
		break;

	case REQ_VIEW_MAIN:
		open_main_view(view, OPEN_DEFAULT);
		break;
	case REQ_VIEW_DIFF:
		if (view && string_rev_is_null(view->env->commit))
			open_stage_view(view, NULL, 0, OPEN_DEFAULT);
		else
			open_diff_view(view, OPEN_DEFAULT);
		break;
	case REQ_VIEW_LOG:
		open_log_view(view, OPEN_DEFAULT);
		break;
	case REQ_VIEW_REFLOG:
		open_reflog_view(view, OPEN_DEFAULT);
		break;
	case REQ_VIEW_TREE:
		open_tree_view(view, OPEN_DEFAULT);
		break;
	case REQ_VIEW_HELP:
		open_help_view(view, OPEN_DEFAULT);
		break;
	case REQ_VIEW_REFS:
		open_refs_view(view, OPEN_DEFAULT);
		break;
	case REQ_VIEW_BLAME:
		open_blame_view(view, OPEN_DEFAULT);
		break;
	case REQ_VIEW_BLOB:
		open_blob_view(view, OPEN_DEFAULT);
		break;
	case REQ_VIEW_STATUS:
		open_status_view(view, false, OPEN_DEFAULT);
		break;
	case REQ_VIEW_STAGE:
		open_stage_view(view, NULL, 0, OPEN_DEFAULT);
		break;
	case REQ_VIEW_PAGER:
		open_pager_view(view, OPEN_DEFAULT);
		break;
	case REQ_VIEW_STASH:
		open_stash_view(view, OPEN_DEFAULT);
		break;

	case REQ_NEXT:
	case REQ_PREVIOUS:
		if (view->parent && view == display[1]) {
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
		int next_view = nviews ? (current_view + 1) % nviews : current_view;

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
		report("Moving to parent is not supported by the %s view", view->name);
		break;

	case REQ_BACK:
		report("Going back is not supported by the %s view", view->name);
		break;

	case REQ_MAXIMIZE:
		if (displayed_views() == 2)
			maximize_view(view, true);
		break;

	case REQ_OPTIONS:
		toggle_option(view);
		break;

	case REQ_SEARCH:
	case REQ_SEARCH_BACK:
		search_view(view, request);
		break;

	case REQ_FIND_NEXT:
	case REQ_FIND_PREV:
		find_next(view, request);
		break;

	case REQ_MOVE_NEXT_MERGE:
	case REQ_MOVE_PREV_MERGE:
		report("Moving between merge commits is not supported by the %s view", view->name);
		break;

	case REQ_STOP_LOADING:
		foreach_view(view, i) {
			if (view->pipe)
				report("Stopped loading the %s view", view->name),
			end_update(view, true);
			if (view_is_displayed(view))
				update_view_title(view);
		}
		break;

	case REQ_SHOW_VERSION:
		report("tig-%s", TIG_VERSION);
		return true;

	case REQ_SCREEN_REDRAW:
		redraw_display(true);
		break;

	case REQ_EDIT:
		report("Nothing to edit");
		break;

	case REQ_ENTER:
		report("Nothing to enter");
		break;

	case REQ_VIEW_CLOSE_NO_QUIT:
	case REQ_VIEW_CLOSE:
		/* XXX: Mark closed views by letting view->prev point to the
		 * view itself. Parents to closed view should never be
		 * followed. */
		if (view->prev && view->prev != view) {
			maximize_view(view->prev, true);
			view->prev = view;
			view->parent = NULL;
			break;
		}
		if (request == REQ_VIEW_CLOSE_NO_QUIT) {
			report("Can't close last remaining view");
			break;
		}
		/* Fall-through */
	case REQ_QUIT:
		return false;

	default:
		report("Unknown key, press %s for help",
		       get_view_key(view, REQ_VIEW_HELP));
		return true;
	}

	return true;
}

/*
 * Main
 */

static const char usage_string[] =
"tig " TIG_VERSION " \n"
"\n"
"Usage: tig        [options] [revs] [--] [paths]\n"
"   or: tig log    [options] [revs] [--] [paths]\n"
"   or: tig show   [options] [revs] [--] [paths]\n"
"   or: tig reflog [options] [revs]\n"
"   or: tig blame  [options] [rev] [--] path\n"
"   or: tig grep   [options] [pattern]\n"
"   or: tig refs   [options]\n"
"   or: tig stash  [options]\n"
"   or: tig status\n"
"   or: tig <      [git command output]\n"
"\n"
"Options:\n"
"  +<number>       Select line <number> in the first view\n"
"  -v, --version   Show version and exit\n"
"  -h, --help      Show help message and exit\n"
"  -C<path>        Start in <path>";

void
usage(const char *message)
{
	die("%s\n\n%s", message, usage_string);
}

static enum status_code
read_filter_args(char *name, size_t namelen, char *value, size_t valuelen, void *data)
{
	const char ***filter_args = data;

	return argv_append(filter_args, name) ? SUCCESS : ERROR_OUT_OF_MEMORY;
}

static bool
filter_rev_parse(const char ***args, const char *arg1, const char *arg2, const char *argv[])
{
	const char *rev_parse_argv[SIZEOF_ARG] = { "git", "rev-parse", arg1, arg2 };
	const char **all_argv = NULL;
	struct io io;

	if (!argv_append_array(&all_argv, rev_parse_argv) ||
	    !argv_append_array(&all_argv, argv) ||
	    io_run_load(&io, all_argv, "\n", read_filter_args, args) != SUCCESS)
		die("Failed to split arguments");
	argv_free(all_argv);
	free(all_argv);

	return !io.status;
}

static void
filter_options(const char *argv[], enum request request)
{
	const char **flags = NULL;
	int next, flags_pos;

	update_options_from_argv(argv);

	if (request == REQ_VIEW_GREP || request == REQ_VIEW_REFS) {
		opt_cmdline_args = argv;
		return;
	}

	/* Add known revision arguments in opt_rev_args and use
	 * git-rev-parse to filter out the remaining options.
	 */
	for (next = flags_pos = 0; argv[next]; next++) {
		const char *arg = argv[next];

		if (!strcmp(arg, "--"))
			while (argv[next])
				argv[flags_pos++] = argv[next++];
		else if (argv_parse_rev_flag(arg, NULL))
			argv_append(&opt_rev_args, arg);
		else
			argv[flags_pos++] = arg;
	}

	argv[flags_pos] = NULL;

	if (!filter_rev_parse(&opt_file_args, "--no-revs", "--no-flags", argv) &&
	    request != REQ_VIEW_BLAME)
		die("No revisions match the given arguments.");
	filter_rev_parse(&flags, "--flags", "--no-revs", argv);

	if (flags) {
		for (next = flags_pos = 0; flags && flags[next]; next++) {
			const char *flag = flags[next];

			if (argv_parse_rev_flag(flag, NULL))
				argv_append(&opt_rev_args, flag);
			else
				flags[flags_pos++] = flag;
		}

		flags[flags_pos] = NULL;

		opt_cmdline_args = flags;
	}

	filter_rev_parse(&opt_rev_args, "--symbolic", "--revs-only", argv);
}

static enum request
parse_options(int argc, const char *argv[], bool pager_mode)
{
	enum request request;
	const char *subcommand;
	bool seen_dashdash = false;
	const char **filter_argv = NULL;
	int i;

	request = pager_mode ? REQ_VIEW_PAGER : REQ_VIEW_MAIN;

	/* Options that must come before any subcommand. */
	for (i = 1; i < argc; i++) {
		const char *opt = argv[i];
		if (!strncmp(opt, "-C", 2)) {
			if (chdir(opt + 2))
				die("Failed to change directory to %s", opt + 2);
			continue;
		} else {
			break;
		}
	}

	if (i >= argc)
		return request;

	subcommand = argv[i++];
	if (!strcmp(subcommand, "status")) {
		request = REQ_VIEW_STATUS;

	} else if (!strcmp(subcommand, "blame")) {
		request = REQ_VIEW_BLAME;

	} else if (!strcmp(subcommand, "grep")) {
		request = REQ_VIEW_GREP;

	} else if (!strcmp(subcommand, "show")) {
		request = REQ_VIEW_DIFF;

	} else if (!strcmp(subcommand, "log")) {
		request = REQ_VIEW_LOG;

	} else if (!strcmp(subcommand, "reflog")) {
		request = REQ_VIEW_REFLOG;

	} else if (!strcmp(subcommand, "stash")) {
		request = REQ_VIEW_STASH;

	} else if (!strcmp(subcommand, "refs")) {
		request = REQ_VIEW_REFS;

	} else {
		subcommand = NULL;
		i--; /* revisit option in loop below */
	}

	for (; i < argc; i++) {
		const char *opt = argv[i];

		// stop parsing our options after -- and let rev-parse handle the rest
		if (!seen_dashdash) {
			if (!strcmp(opt, "--")) {
				seen_dashdash = true;

			} else if (!strcmp(opt, "-v") || !strcmp(opt, "--version")) {
				printf("tig version %s\n", TIG_VERSION);
#ifdef NCURSES_VERSION
				printf("%s version %s.%d\n",
#ifdef NCURSES_WIDECHAR
				       "ncursesw",
#else
				       "ncurses",
#endif
				       NCURSES_VERSION, NCURSES_VERSION_PATCH);
#endif
#ifdef HAVE_READLINE
				printf("readline version %s\n", rl_library_version);
#endif
				exit(EXIT_SUCCESS);

			} else if (!strcmp(opt, "-h") || !strcmp(opt, "--help")) {
				printf("%s\n", usage_string);
				exit(EXIT_SUCCESS);

			} else if (strlen(opt) >= 2 && *opt == '+' && string_isnumber(opt + 1)) {
				int lineno = atoi(opt + 1);

				argv_env.goto_lineno = lineno > 0 ? lineno - 1 : 0;
				continue;

			}
		}

		if (!argv_append(&filter_argv, opt))
			die("command too long");
	}

	if (filter_argv)
		filter_options(filter_argv, request);

	return request;
}

static enum request
open_pager_mode(enum request request)
{
	if (request == REQ_VIEW_PAGER) {
		/* Detect if the user requested the main view. */
		if (argv_contains(opt_rev_args, "--stdin")) {
			open_main_view(NULL, OPEN_FORWARD_STDIN);
		} else if (argv_contains(opt_cmdline_args, "--pretty=raw")) {
			open_main_view(NULL, OPEN_STDIN);
		} else {
			open_pager_view(NULL, OPEN_STDIN);
		}

	} else if (request == REQ_VIEW_DIFF) {
		if (argv_contains(opt_rev_args, "--stdin"))
			open_diff_view(NULL, OPEN_FORWARD_STDIN);
		else
			open_diff_view(NULL, OPEN_STDIN);

	} else {
		close(STDIN_FILENO);
		report("Ignoring stdin.");
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

#ifdef BUTTON5_PRESSED
	if (event.bstate & (BUTTON2_PRESSED | BUTTON5_PRESSED))
#else
	if (event.bstate & BUTTON2_PRESSED)
#endif
		return opt_mouse_wheel_cursor ? REQ_MOVE_WHEEL_DOWN : REQ_SCROLL_WHEEL_DOWN;

	if (event.bstate & BUTTON4_PRESSED)
		return opt_mouse_wheel_cursor ? REQ_MOVE_WHEEL_UP : REQ_SCROLL_WHEEL_UP;

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

/*
 * Error handling.
 *
 * Inspired by code from src/util.c in ELinks
 * (f86be659718c0cd0a67f88b42f07044c23d0d028).
 */

#ifdef DEBUG
void
sigsegv_handler(int sig)
{
	if (die_callback)
		die_callback();

	fputs("Tig crashed!\n\n"
	      "Please report this issue along with all info printed below to\n\n"
	      "  https://github.com/jonas/tig/issues/new\n\n", stderr);

	fputs("Tig version: ", stderr);
	fputs(TIG_VERSION, stderr);
	fputs("\n\n", stderr);

#ifdef HAVE_EXECINFO_H
	{
		/* glibc way of doing this */
		void *stack[20];
		size_t size = backtrace(stack, 20);

		backtrace_symbols_fd(stack, size, STDERR_FILENO);
	}
#endif

	/* The fastest way OUT! */
	abort();
}
#endif

void
sighup_handler(int sig)
{
	if (die_callback)
		die_callback();

	exit(EXIT_SUCCESS);
}

struct key_combo {
	enum request request;
	struct keymap *keymap;
	size_t bufpos;
	size_t keys;
	struct key key[16];
};

static enum input_status
key_combo_handler(struct input *input, struct key *key)
{
	struct key_combo *combo = input->data;
	int matches = 0;

#ifdef NCURSES_MOUSE_VERSION
	if (key_to_value(key) == KEY_MOUSE) {
		combo->request = handle_mouse_event();
		return INPUT_STOP;
	}
#endif

	if (combo->keys && key_to_value(key) == KEY_ESC)
		return INPUT_CANCEL;

	string_format_from(input->buf, &combo->bufpos, "%s%s",
			   combo->bufpos ? " " : "Keys: ", get_key_name(key, 1, false));
	combo->key[combo->keys++] = *key;
	combo->request = get_keybinding(combo->keymap, combo->key, combo->keys, &matches);

	if (combo->request == REQ_UNKNOWN)
		return matches > 0 ? INPUT_OK : INPUT_STOP;
	return INPUT_STOP;
}

enum request
read_key_combo(struct keymap *keymap)
{
	struct key_combo combo = { REQ_NONE, keymap, 0 };
	char *value = read_prompt_incremental("", false, false, key_combo_handler, &combo);

	return value ? combo.request : REQ_NONE;
}

static inline void
die_if_failed(enum status_code code, const char *msg)
{
	if (code != SUCCESS)
		die("%s: %s", msg, get_status_message(code));
}

void
hangup_children(void)
{
	if (signal(SIGHUP, SIG_IGN) == SIG_ERR)
		return;
	killpg(getpid(), SIGHUP);
}

static inline enum status_code
handle_git_prefix(void)
{
	const char *prefix = getenv("GIT_PREFIX");
	char cwd[4096];

	if (!prefix || !*prefix)
		return SUCCESS;

	/*
	 * GIT_PREFIX is set when tig is invoked as a git alias.
	 * Tig expects to run from the subdirectory so clear the prefix
	 * and set GIT_WORK_TREE accordinglyt.
	 */
	if (!getcwd(cwd, sizeof(cwd)))
		return error("Failed to read CWD");
	if (setenv("GIT_WORK_TREE", cwd, 1))
		return error("Failed to set GIT_WORK_TREE");
	if (chdir(prefix))
		return error("Failed to change directory to %s", prefix);
	if (setenv("GIT_PREFIX", "", 1))
		return error("Failed to clear GIT_PREFIX");

	return SUCCESS;
}

int
main(int argc, const char *argv[])
{
	const char *codeset = ENCODING_UTF8;
	bool pager_mode = !isatty(STDIN_FILENO);
	enum request request = parse_options(argc, argv, pager_mode);
	struct view *view;

	if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
		die("Failed to setup signal handler");

	if (signal(SIGHUP, sighup_handler) == SIG_ERR)
		die("Failed to setup signal handler");

#ifdef DEBUG
	if (signal(SIGSEGV, sigsegv_handler) == SIG_ERR)
		die("Failed to setup signal handler");
#endif

	if (setlocale(LC_ALL, "")) {
		codeset = nl_langinfo(CODESET);
	}

	die_if_failed(handle_git_prefix(), "Failed to handle GIT_PREFIX");
	die_if_failed(load_repo_info(), "Failed to load repo info.");
	die_if_failed(load_options(), "Failed to load user config.");
	die_if_failed(load_git_config(), "Failed to load repo config.");

	init_tty();

	if (opt_pgrp)
		atexit(hangup_children);

	prompt_init();

	/* Require a git repository unless when running in pager mode. */
	if (!repo.git_dir[0] && request != REQ_VIEW_PAGER)
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

	die_if_failed(load_refs(false), "Failed to load refs.");

	init_display();

	if (pager_mode)
		request = open_pager_mode(request);

	if (getenv("TIG_SCRIPT")) {
		const char *script_command[] = { "script", getenv("TIG_SCRIPT"), NULL };

		run_prompt_command(NULL, script_command);
	}

	while (view_driver(display[current_view], request)) {
		view = display[current_view];
		request = read_key_combo(view->keymap);

		/* Some low-level request handling. This keeps access to
		 * status_win restricted. */
		switch (request) {
		case REQ_UNKNOWN:
			report("Unknown key, press %s for help",
			       get_view_key(view, REQ_VIEW_HELP));
			request = REQ_NONE;
			break;
		case REQ_PROMPT:
			request = open_prompt(view);
			break;
		default:
			break;
		}
	}

	exit(EXIT_SUCCESS);

	return 0;
}

/* vim: set ts=8 sw=8 noexpandtab: */
