/* Copyright (c) 2006-2014 Jonas Fonseca <jonas.fonseca@gmail.com>
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

#include "tig/tig.h"
#include "tig/argv.h"
#include "tig/io.h"
#include "tig/repo.h"
#include "tig/options.h"
#include "tig/view.h"
#include "tig/draw.h"
#include "tig/display.h"

struct view *display[2];
unsigned int current_view;

static WINDOW *display_win[2];
static WINDOW *display_title[2];
static WINDOW *display_sep;

FILE *opt_tty;

bool
open_external_viewer(const char *argv[], const char *dir, bool confirm, const char *notice)
{
	bool ok;

	def_prog_mode();           /* save current tty modes */
	endwin();                  /* restore original tty modes */
	ok = io_run_fg(argv, dir);
	if (confirm || !ok) {
		if (!ok && *notice)
			fprintf(stderr, "%s", notice);
		fprintf(stderr, "Press Enter to continue");
		getc(opt_tty);
	}
	reset_prog_mode();
	redraw_display(TRUE);
	return ok;
}

#define EDITOR_LINENO_MSG \
	"*** Your editor reported an error while opening the file.\n" \
	"*** This is probably because it doesn't support the line\n" \
	"*** number argument added automatically. The line number\n" \
	"*** has been disabled for now. You can permanently disable\n" \
	"*** it by adding the following line to ~/.tigrc\n" \
	"***	set editor-line-number = no\n"

void
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

	if (lineno && opt_editor_line_number && string_format(lineno_cmd, "+%u", lineno))
		editor_argv[argc++] = lineno_cmd;
	editor_argv[argc] = file;
	if (!open_external_viewer(editor_argv, repo.cdup, FALSE, EDITOR_LINENO_MSG))
		opt_editor_line_number = FALSE;
}


static void
apply_horizontal_split(struct view *base, struct view *view)
{
	view->width   = base->width;
	view->height  = apply_step(opt_split_view_height, base->height);
	view->height  = MAX(view->height, MIN_VIEW_HEIGHT);
	view->height  = MIN(view->height, base->height - MIN_VIEW_HEIGHT);
	base->height -= view->height;
}

static void
apply_vertical_split(struct view *base, struct view *view)
{
	view->height = base->height;
	view->width  = apply_step(VSPLIT_SCALE, base->width);
	view->width  = MAX(view->width, MIN_VIEW_WIDTH);
	view->width  = MIN(view->width, base->width - MIN_VIEW_WIDTH);
	base->width -= view->width;
}

static bool
vertical_split_is_enabled(void)
{
	if (opt_vertical_split == VERTICAL_SPLIT_AUTO) {
		int height, width;

		getmaxyx(stdscr, height, width);
		return width > 160 || width * VSPLIT_SCALE > (height - 1) * 2;
	}

	return opt_vertical_split == VERTICAL_SPLIT_VERTICAL;
}

static void
redraw_display_separator(bool clear)
{
	if (displayed_views() > 1 && vertical_split_is_enabled()) {
		chtype separator = opt_line_graphics ? ACS_VLINE : '|';

		if (clear)
			wclear(display_sep);
		wbkgd(display_sep, separator + get_line_attr(NULL, LINE_TITLE_BLUR));
		wnoutrefresh(display_sep);
	}
}

void
resize_display(void)
{
	int x, y, i;
	struct view *base = display[0];
	struct view *view = display[1] ? display[1] : display[0];
	bool vsplit;

	/* Setup window dimensions */

	getmaxyx(stdscr, base->height, base->width);

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

	string_format(opt_env_columns, "COLUMNS=%d", base->width);
	string_format(opt_env_lines, "LINES=%d", base->height);

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
		view->title = display_title[i];

		if (vsplit)
			x += view->width + 1;
		else
			y += view->height + 1;
	}

	redraw_display_separator(FALSE);
}

void
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
 * Status management
 */

/* Whether or not the curses interface has been initialized. */
static bool cursed = FALSE;

/* Terminal hacks and workarounds. */
static bool use_scroll_redrawwin;
static bool use_scroll_status_wclear;

/* The status window is used for polling keystrokes. */
WINDOW *status_win;

/* Reading from the prompt? */
static bool input_mode = FALSE;

static bool status_empty = FALSE;

/* Update status and title window. */
static bool
update_status_window(struct view *view, const char *msg, va_list args)
{
	if (input_mode)
		return FALSE;

	if (!status_empty || *msg) {
		wmove(status_win, 0, 0);
		if (view && view->has_scrolled && use_scroll_status_wclear)
			wclear(status_win);
		if (*msg) {
			vwprintw(status_win, msg, args);
			status_empty = FALSE;
		} else {
			status_empty = TRUE;
		}
		wclrtoeol(status_win);
		return TRUE;
	}

	return FALSE;
}

void
update_status(const char *msg, ...)
{
	va_list args;

	va_start(args, msg);
	update_status_window(display[current_view], msg, args);
	va_end(args);
}

void
report(const char *msg, ...)
{
	struct view *view = display[current_view];
	va_list args;

	if (!view) {
		char buf[SIZEOF_STR];
		int retval;

		FORMAT_BUFFER(buf, sizeof(buf), msg, retval, TRUE);
		die("%s", buf);
	}

	va_start(args, msg);
	if (update_status_window(view, msg, args))
		wnoutrefresh(status_win);
	va_end(args);

	update_view_title(view);
}

static void
done_display(void)
{
	endwin();
}

void
init_display(void)
{
	const char *term;
	int x, y;

	die_callback = done_display;
	/* XXX: Restore tty modes and let the OS cleanup the rest! */
	if (atexit(done_display))
		die("Failed to register done_display");

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
	wbkgdset(status_win, get_line_attr(NULL, LINE_STATUS));
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

int
get_input(int prompt_position, struct key *key, bool modifiers)
{
	struct view *view;
	int i, key_value, cursor_y, cursor_x;

	if (prompt_position)
		input_mode = TRUE;

	memset(key, 0, sizeof(*key));

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
		key_value = wgetch(status_win);

		/* wgetch() with nodelay() enabled returns ERR when
		 * there's no input. */
		if (key_value == ERR) {

		} else if (key_value == KEY_ESC && modifiers) {
			key->modifiers.escape = 1;

		} else if (key_value == KEY_RESIZE) {
			int height, width;

			getmaxyx(stdscr, height, width);

			wresize(status_win, 1, width);
			mvwin(status_win, height - 1, 0);
			wnoutrefresh(status_win);
			resize_display();
			redraw_display(TRUE);

		} else {
			int pos, key_length;

			input_mode = FALSE;
			if (key_value == erasechar())
				key_value = KEY_BACKSPACE;

			/*
			 * Ctrl-<key> values are represented using a 0x1F
			 * bitmask on the key value. To 'unmap' we assume that:
			 *
			 * - Ctrl-Z is handled by Ncurses.
			 * - Ctrl-m is the same as Return/Enter.
			 * - Ctrl-i is the same as Tab.
			 *
			 * For all other key values in the range the Ctrl flag
			 * is set and the key value is updated to the proper
			 * ASCII value.
			 */
			if (KEY_CTL('a') <= key_value && key_value <= KEY_CTL('x') &&
			    key_value != KEY_RETURN && key_value != KEY_TAB) {
				key->modifiers.control = 1;
				key_value = key_value | 0x40;
			}

			if ((key_value >= KEY_MIN && key_value < KEY_MAX) || key_value < 0x1F) {
				key->data.value = key_value;
				return key->data.value;
			}

			key->modifiers.multibytes = 1;
			key->data.bytes[0] = key_value;

			key_length = utf8_char_length(key->data.bytes);
			for (pos = 1; pos < key_length && pos < sizeof(key->data.bytes) - 1; pos++) {
				key->data.bytes[pos] = wgetch(status_win);
			}

			return OK;
		}
	}
}

/* vim: set ts=8 sw=8 noexpandtab: */
