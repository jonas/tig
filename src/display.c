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

#include "tig/tig.h"
#include "tig/argv.h"
#include "tig/io.h"
#include "tig/repo.h"
#include "tig/options.h"
#include "tig/view.h"
#include "tig/draw.h"
#include "tig/display.h"
#include "tig/watch.h"

struct view *display[2];
unsigned int current_view;

static WINDOW *display_win[2];
static WINDOW *display_title[2];
static WINDOW *display_sep;

static FILE *opt_tty;

static struct io script_io = { -1 };

static bool
is_script_executing(void)
{
	return script_io.pipe != -1;
}

enum status_code
open_script(const char *path)
{
	if (is_script_executing())
		return error("Scripts cannot be run from scripts");

	return io_open(&script_io, "%s", path)
		? SUCCESS : error("Failed to open %s", path);
}

bool
open_external_viewer(const char *argv[], const char *dir, bool silent, bool confirm, bool refresh, const char *notice)
{
	bool ok;

	if (silent || is_script_executing()) {
		ok = io_run_bg(argv, dir);

	} else {
		endwin();                  /* restore original tty modes */
		ok = io_run_fg(argv, dir);
		if (confirm || !ok) {
			if (!ok && *notice)
				fprintf(stderr, "%s", notice);
			fprintf(stderr, "Press Enter to continue");
			getc(opt_tty);
		}
	}

	if (watch_update(WATCH_EVENT_AFTER_COMMAND) && refresh) {
		struct view *view;
		int i;

		foreach_displayed_view (view, i) {
			if (watch_dirty(&view->watch))
				refresh_view(view);
		}
	}
	redraw_display(true);
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
	if (!open_external_viewer(editor_argv, repo.cdup, false, false, true, EDITOR_LINENO_MSG))
		opt_editor_line_number = false;
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

int
apply_vertical_split(int base_width)
{
	int width  = apply_step(opt_split_view_width, base_width);

	width = MAX(width, MIN_VIEW_WIDTH);
	width = MIN(width, base_width - MIN_VIEW_WIDTH);

	return width;
}

bool
vertical_split_is_enabled(enum vertical_split vsplit, int height, int width)
{
	if (vsplit == VERTICAL_SPLIT_AUTO)
		return width > 160 || width * VSPLIT_SCALE > (height - 1) * 2;
	return vsplit == VERTICAL_SPLIT_VERTICAL;
}

static void
redraw_display_separator(bool clear)
{
	if (display_sep) {
		chtype separator = opt_line_graphics ? ACS_VLINE : '|';

		if (clear)
			wclear(display_sep);
		wbkgd(display_sep, separator + get_line_attr(NULL, LINE_TITLE_BLUR));
		wnoutrefresh(display_sep);
	}
}

static void create_or_move_display_separator(int height, int x)
{
	if (!display_sep) {
		display_sep = newwin(height, 1, 0, x);
		if (!display_sep)
			die("Failed to create separator window");

	} else {
		wresize(display_sep, height, 1);
		mvwin(display_sep, 0, x);
	}
}

static void remove_display_separator(void)
{
	if (display_sep) {
		delwin(display_sep);
		display_sep = NULL;
	}
}

void
resize_display(void)
{
	int x, y, i;
	int height, width;
	struct view *base = display[0];
	struct view *view = display[1] ? display[1] : display[0];
	bool vsplit;

	/* Setup window dimensions */

	getmaxyx(stdscr, height, width);

	/* Make room for the status window. */
	base->height = height - 1;
	base->width = width;

	vsplit = vertical_split_is_enabled(opt_vertical_split, height, width);

	if (view != base) {
		if (vsplit) {
			view->height = base->height;
			view->width = apply_vertical_split(base->width);
			base->width -= view->width;

			/* Make room for the separator bar. */
			view->width -= 1;

			create_or_move_display_separator(base->height, base->width);
			redraw_display_separator(false);
		} else {
			remove_display_separator();
			apply_horizontal_split(base, view);
		}

		/* Make room for the title bar. */
		view->height -= 1;

	} else {
		remove_display_separator();
	}

	/* Make room for the title bar. */
	base->height -= 1;

	x = y = 0;

	foreach_displayed_view (view, i) {
		if (!display_win[i]) {
			display_win[i] = newwin(view->height, view->width, y, x);
			if (!display_win[i])
				die("Failed to create %s view", view->name);

			scrollok(display_win[i], false);

			display_title[i] = newwin(1, view->width, y + view->height, x);
			if (!display_title[i])
				die("Failed to create title window");

		} else {
			wresize(display_win[i], view->height, view->width);
			mvwin(display_win[i], y, x);
			wresize(display_title[i], 1, view->width);
			mvwin(display_title[i], y + view->height, x);
		}

		view->win = display_win[i];
		view->title = display_title[i];

		if (vsplit)
			x += view->width + 1;
		else
			y += view->height + 1;
	}

	redraw_display_separator(false);
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

static bool
save_window_line(FILE *file, WINDOW *win, int y, char *buf, size_t bufsize)
{
	int read = mvwinnstr(win, y, 0, buf, bufsize);

	return read == ERR ? false : fprintf(file, "%s\n", buf) == read + 1;
}

static bool
save_window_vline(FILE *file, WINDOW *left, WINDOW *right, int y, char *buf, size_t bufsize)
{
	int read1 = mvwinnstr(left, y, 0, buf, bufsize);
	int read2 = read1 == ERR ? ERR : mvwinnstr(right, y, 0, buf + read1 + 1, bufsize - read1 - 1);

	if (read2 == ERR)
		return false;
	buf[read1] = '|';

	return fprintf(file, "%s\n", buf) == read1 + 1 + read2 + 1;
}

bool
save_display(const char *path)
{
	int i, width;
	size_t linelen;
	char *line;
	FILE *file = fopen(path, "w");
	bool ok = true;
	struct view *view = display[0];

	if (!file)
		return false;

	getmaxyx(stdscr, i, width);
	linelen = width * 4;
	line = malloc(linelen + 1);
	if (!line) {
		fclose(file);
		return false;
	}

	if (view->width < width && display[1]) {
		struct view *left = display[0],
			    *right = display[1];

		for (i = 0; ok && i < left->height; i++)
			ok = save_window_vline(file, left->win, right->win, i, line, linelen);
		if (ok)
			ok = save_window_vline(file, left->title, right->title, 0, line, linelen);
	} else {
		int j;

		foreach_displayed_view (view, j) {
			for (i = 0; ok && i < view->height; i++)
				ok = save_window_line(file, view->win, i, line, linelen);
			if (ok)
				ok = save_window_line(file, view->title, 0, line, linelen);
		}
	}

	free(line);
	fclose(file);
	return ok;
}

/*
 * Status management
 */

/* Whether or not the curses interface has been initialized. */
static bool cursed = false;

/* Terminal hacks and workarounds. */
static bool use_scroll_redrawwin;
static bool use_scroll_status_wclear;

/* The status window is used for polling keystrokes. */
WINDOW *status_win;

/* Reading from the prompt? */
static bool input_mode = false;

static bool status_empty = false;

/* Update status and title window. */
static bool
update_status_window(struct view *view, const char *msg, va_list args)
{
	if (input_mode)
		return false;

	if (!status_empty || *msg) {
		wmove(status_win, 0, 0);
		if (view && view->has_scrolled && use_scroll_status_wclear)
			wclear(status_win);
		if (*msg) {
			vwprintw(status_win, msg, args);
			status_empty = false;
		} else {
			status_empty = true;
		}
		wclrtoeol(status_win);
		return true;
	}

	return false;
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

		FORMAT_BUFFER(buf, sizeof(buf), msg, retval, true);
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
	if (cursed)
		endwin();
	cursed = false;
}

void
init_display(void)
{
	bool no_display = !!getenv("TIG_NO_DISPLAY");
	const char *term;
	int x, y;

	die_callback = done_display;
	/* XXX: Restore tty modes and let the OS cleanup the rest! */
	if (atexit(done_display))
		die("Failed to register done_display");

	/* Initialize the curses library */
	if (!no_display && isatty(STDIN_FILENO)) {
		cursed = !!initscr();
		opt_tty = stdin;
	} else {
		/* Leave stdin and stdout alone when acting as a pager. */
		FILE *out_tty;

		opt_tty = fopen("/dev/tty", "r+");
		out_tty = no_display ? fopen("/dev/null", "w+") : opt_tty;
		if (!opt_tty || !out_tty)
			die("Failed to open /dev/tty");
		cursed = !!newterm(NULL, out_tty, opt_tty);
	}

	if (!cursed)
		die("Failed to initialize curses");

	nonl();		/* Disable conversion and detect newlines from input. */
	cbreak();       /* Take input chars one at a time, no wait for \n */
	noecho();       /* Don't echo input */
	leaveok(stdscr, false);

	init_colors();

	getmaxyx(stdscr, y, x);
	status_win = newwin(1, x, y - 1, 0);
	if (!status_win)
		die("Failed to create status window");

	/* Enable keyboard mapping */
	keypad(status_win, true);
	wbkgdset(status_win, get_line_attr(NULL, LINE_STATUS));
	enable_mouse(opt_mouse);

#if defined(NCURSES_VERSION_PATCH) && (NCURSES_VERSION_PATCH >= 20080119)
	set_tabsize(opt_tab_size);
#else
	TABSIZE = opt_tab_size;
#endif

	term = getenv("XTERM_VERSION") ? NULL : getenv("COLORTERM");
	if (term && !strcmp(term, "gnome-terminal")) {
		/* In the gnome-terminal-emulator, the warning message
		 * shown when scrolling up one line while the cursor is
		 * on the first line followed by scrolling down one line
		 * corrupts the status line. This is fixed by calling
		 * wclear. */
		use_scroll_status_wclear = true;
		use_scroll_redrawwin = false;

	} else if (term && !strcmp(term, "xrvt-xpm")) {
		/* No problems with full optimizations in xrvt-(unicode)
		 * and aterm. */
		use_scroll_status_wclear = use_scroll_redrawwin = false;

	} else {
		/* When scrolling in (u)xterm the last line in the
		 * scrolling direction will update slowly. */
		use_scroll_redrawwin = true;
		use_scroll_status_wclear = false;
	}
}

static bool
read_script(struct key *key)
{
	static struct buffer input_buffer;
	static const char *line = "";
	enum status_code code;

	if (!line || !*line) {
		if (input_buffer.data && *input_buffer.data == ':') {
			line = "<Enter>";
			memset(&input_buffer, 0, sizeof(input_buffer));

		} else if (!io_get(&script_io, &input_buffer, '\n', true)) {
			io_done(&script_io);
			return false;
		} else {
			line = input_buffer.data;
		}
	}

	code = get_key_value(&line, key);
	if (code != SUCCESS)
		die("Error reading script: %s", get_status_message(code));
	return true;
}

int
get_input_char(void)
{
	if (is_script_executing()) {
		static struct key key;
		static int bytes_pos;

		if (!key.modifiers.multibytes || bytes_pos >= strlen(key.data.bytes)) {
			if (!read_script(&key))
				return 0;
			bytes_pos = 0;
		}

		if (!key.modifiers.multibytes) {
			if (key.data.value < 128)
				return key.data.value;
			die("Only ASCII control characters can be used in prompts: %d", key.data.value);
		}

		return key.data.bytes[bytes_pos++];
	}

	return getc(opt_tty);
}

int
get_input(int prompt_position, struct key *key)
{
	struct view *view;
	int i, key_value, cursor_y, cursor_x;

	if (prompt_position > 0)
		input_mode = true;

	memset(key, 0, sizeof(*key));

	while (true) {
		int delay = -1;

		if (opt_refresh_mode == REFRESH_MODE_PERIODIC) {
			delay = watch_periodic(opt_refresh_interval);
			foreach_displayed_view (view, i) {
				if (view_can_refresh(view) &&
				    watch_dirty(&view->watch))
					refresh_view(view);
			}
		}

		foreach_view (view, i) {
			update_view(view);
			if (view_is_displayed(view) && view->has_scrolled &&
			    use_scroll_redrawwin)
				redrawwin(view->win);
			view->has_scrolled = false;
			if (view->pipe)
				delay = 0;
		}

		/* Update the cursor position. */
		if (prompt_position) {
			getbegyx(status_win, cursor_y, cursor_x);
			cursor_x = prompt_position;
		} else {
			view = display[current_view];
			getbegyx(view->win, cursor_y, cursor_x);
			cursor_x += view->width - 1;
			cursor_y += view->pos.lineno - view->pos.offset;
		}
		set_cursor_pos(cursor_y, cursor_x);

		if (is_script_executing()) {
			/* Wait for the current command to complete. */
			if (delay == 0 || !read_script(key))
				continue;
			return key->modifiers.multibytes ? OK : key->data.value;

		} else {
			/* Refresh, accept single keystroke of input */
			doupdate();
			wtimeout(status_win, delay);
			key_value = wgetch(status_win);
		}

		/* wgetch() with nodelay() enabled returns ERR when
		 * there's no input. */
		if (key_value == ERR) {

		} else if (key_value == KEY_RESIZE) {
			int height, width;

			getmaxyx(stdscr, height, width);

			wresize(status_win, 1, width);
			mvwin(status_win, height - 1, 0);
			wnoutrefresh(status_win);
			resize_display();
			redraw_display(true);

		} else {
			int pos, key_length;

			input_mode = false;
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
			if (KEY_CTL('a') <= key_value && key_value <= KEY_CTL('y') &&
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

void
enable_mouse(bool enable)
{
#ifdef NCURSES_MOUSE_VERSION
	static bool enabled = false;

	if (enable != enabled) {
		mmask_t mask = enable ? ALL_MOUSE_EVENTS : 0;

		if (mousemask(mask, NULL))
			mouseinterval(0);
		enabled = enable;
	}
#endif
}

/* vim: set ts=8 sw=8 noexpandtab: */
