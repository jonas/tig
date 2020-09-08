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
#include "tig/repo.h"
#include "tig/watch.h"
#include "tig/options.h"
#include "tig/view.h"
#include "tig/search.h"
#include "tig/draw.h"
#include "tig/display.h"

/*
 * Navigation
 */

bool
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
		return true;
	}

	return false;
}

/* Scrolling backend */
void
do_scroll_view(struct view *view, int lines)
{
	bool redraw_current_line = false;

	/* The rendering expects the new offset. */
	view->pos.offset += lines;

	assert(0 <= view->pos.offset && view->pos.offset < view->lines);
	assert(lines);

	/* Move current line into the view. */
	if (view->pos.lineno < view->pos.offset) {
		view->pos.lineno = view->pos.offset;
		redraw_current_line = true;
	} else if (view->pos.lineno >= view->pos.offset + view->height) {
		view->pos.lineno = view->pos.offset + view->height - 1;
		redraw_current_line = true;
	}

	assert(view->pos.offset <= view->pos.lineno && view->pos.lineno < view->lines);

	/* Redraw the whole screen if scrolling is pointless. */
	if (view->height < ABS(lines)) {
		redraw_view(view);

	} else {
		int line = lines > 0 ? view->height - lines : 0;
		int end = line + ABS(lines);

		scrollok(view->win, true);
		wscrl(view->win, lines);
		scrollok(view->win, false);

		while (line < end && draw_view_line(view, line))
			line++;

		if (redraw_current_line)
			draw_view_line(view, view->pos.lineno - view->pos.offset);
		wnoutrefresh(view->win);
	}

	view->has_scrolled = true;
	report_clear();
}

/* Scroll frontend */
void
scroll_view(struct view *view, enum request request)
{
	int lines = 1;

	assert(view_is_displayed(view));

	if (request == REQ_SCROLL_WHEEL_DOWN || request == REQ_SCROLL_WHEEL_UP)
		lines = opt_mouse_scroll;

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
		if (view->pos.col <= apply_step(opt_horizontal_scroll, view->width))
			view->pos.col = 0;
		else
			view->pos.col -= apply_step(opt_horizontal_scroll, view->width);
		redraw_view_from(view, 0);
		report_clear();
		return;
	case REQ_SCROLL_RIGHT:
		view->pos.col += apply_step(opt_horizontal_scroll, view->width);
		redraw_view(view);
		report_clear();
		return;
	case REQ_SCROLL_PAGE_DOWN:
		lines = view->height;
		/* Fall-through */
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
		/* Fall-through */
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
void
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

	case REQ_MOVE_HALF_PAGE_UP:
		steps = view->height / 2 > view->pos.lineno
		      ? -view->pos.lineno : -(view->height / 2);
		break;

	case REQ_MOVE_HALF_PAGE_DOWN:
		steps = view->pos.lineno + view->height / 2 >= view->lines
		      ? view->lines - view->pos.lineno - 1 : view->height / 2;
		break;

	case REQ_MOVE_WHEEL_DOWN:
		steps = opt_mouse_scroll;
		break;

	case REQ_MOVE_WHEEL_UP:
		steps = -opt_mouse_scroll;
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

void
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

void
goto_id(struct view *view, const char *expr, bool from_start, bool save_search)
{
	struct view_column_data column_data = {0};
	char id[SIZEOF_STR] = "";
	size_t idlen;
	struct line *line = &view->line[view->pos.lineno];

	if (!(view->ops->column_bits & view_column_bit(ID))) {
		report("Jumping to ID is not supported by the %s view", view->name);
		return;
	} else {
		char *rev = argv_format_arg(view->env, expr);
		const char *rev_parse_argv[] = {
			"git", "rev-parse", "--revs-only", rev, NULL
		};
		bool ok = rev && io_run_buf(rev_parse_argv, id, sizeof(id), NULL, true);

		free(rev);
		if (!ok) {
			report("Failed to parse expression '%s'", expr);
			return;
		}
	}

	if (!id[0]) {
		if (view->ops->get_column_data(view, line, &column_data)
		    && column_data.id && string_rev_is_null(column_data.id)) {
			select_view_line(view, view->pos.lineno + 1);
			report_clear();
		} else {
			report("Expression '%s' is not a meaningful revision", expr);
		}
		return;
	}

	line = from_start ? view->line : &view->line[view->pos.lineno];

	for (idlen = strlen(id); view_has_line(view, line); line++) {
		struct view_column_data column_data = {0};

		if (view->ops->get_column_data(view, line, &column_data) &&
		    column_data.id &&
		    !strncasecmp(column_data.id, id, idlen)) {
			if (save_search)
				string_ncopy(view->env->search, id, idlen);
			select_view_line(view, line - view->line);
			report_clear();
			return;
		}
	}

	report("Unable to find commit '%s'", id);
}

/*
 * View history
 */

static bool
view_history_is_empty(struct view_history *history)
{
	return !history->stack;
}

struct view_state *
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

bool
pop_view_history_state(struct view_history *history, struct position *position, void *data)
{
	struct view_state *state = history->stack;

	if (view_history_is_empty(history))
		return false;

	history->position = state->position;
	history->stack = state->prev;

	if (data && history->state_alloc)
		memcpy(data, state->data, history->state_alloc);
	if (position)
		*position = state->position;

	free(state);
	return true;
}

void
reset_view_history(struct view_history *history)
{
	while (pop_view_history_state(history, NULL, NULL))
		;
}

/*
 * Incremental updating
 */

void
reset_view(struct view *view)
{
	int i;

	for (i = 0; i < view->lines; i++)
		free(view->line[i].data);
	free(view->line);

	reset_search(view);
	view->prev_pos = view->pos;
	/* A view without a previous view is the first view */
	if (!view->prev && !view->lines && view->prev_pos.lineno == 0)
		view->prev_pos.lineno = view->env->goto_lineno;
	clear_position(&view->pos);

	if (view->columns)
		view_column_reset(view);

	view->line = NULL;
	view->lines  = 0;
	view->vid[0] = 0;
	view->custom_lines = 0;
	view->update_secs = 0;
}

static bool
restore_view_position(struct view *view)
{
	/* Ensure that the view position is in a valid state. */
	if (!check_position(&view->prev_pos) ||
	    (view->pipe && view->lines <= view->prev_pos.lineno))
		return goto_view_line(view, view->pos.offset, view->pos.lineno);

	/* Changing the view position cancels the restoring. */
	/* FIXME: Changing back to the first line is not detected. */
	if (check_position(&view->pos)) {
		clear_position(&view->prev_pos);
		return false;
	}

	if (goto_view_line(view, view->prev_pos.offset, view->prev_pos.lineno) &&
	    view_is_displayed(view))
		werase(view->win);

	view->pos.col = view->prev_pos.col;
	clear_position(&view->prev_pos);

	return true;
}

void
end_update(struct view *view, bool force)
{
	if (!view->pipe)
		return;
	while (!view->ops->read(view, NULL, force))
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
view_no_refresh(struct view *view, enum open_flags flags)
{
	bool reload = !!(flags & OPEN_ALWAYS_LOAD) || !view->lines;

	return (!reload && !strcmp(view->vid, view->ops->id)) ||
	       ((flags & OPEN_REFRESH) && !view_can_refresh(view));
}

bool
view_exec(struct view *view, enum open_flags flags)
{
	char opt_env_lines[64] = "";
	char opt_env_columns[64] = "";
	char * const opt_env[]	= { opt_env_lines, opt_env_columns, NULL };

	enum io_flags forward_stdin = (flags & OPEN_FORWARD_STDIN) ? IO_RD_FORWARD_STDIN : 0;
	enum io_flags with_stderr = (flags & OPEN_WITH_STDERR) ? IO_RD_WITH_STDERR : 0;
	enum io_flags io_flags = forward_stdin | with_stderr;

	int views = displayed_views();
	bool split = (views == 1 && !!(flags & OPEN_SPLIT)) || views == 2;
	int height, width;

	getmaxyx(stdscr, height, width);
	if (split && vertical_split_is_enabled(opt_vertical_split, height, width)) {
		bool is_base_view = display[0] == view;
		int split_width = apply_vertical_split(width);

		if (is_base_view)
			width -= split_width;
		else
			width = split_width - 1;
	}

	string_format(opt_env_columns, "COLUMNS=%d", MAX(0, width));
	string_format(opt_env_lines, "LINES=%d", height);

	return io_exec(&view->io, IO_RD, view->dir, opt_env, view->argv, io_flags);
}

enum status_code
begin_update(struct view *view, const char *dir, const char **argv, enum open_flags flags)
{
	bool extra = !!(flags & (OPEN_EXTRA));
	bool refresh = flags & (OPEN_REFRESH | OPEN_PREPARED | OPEN_STDIN);

	if (view_no_refresh(view, flags))
		return SUCCESS;

	if (view->pipe) {
		if (extra)
			io_done(view->pipe);
		else
			end_update(view, true);
	}

	view->unrefreshable = open_in_pager_mode(flags);

	if (!refresh && argv) {
		bool file_filter = !view_has_flags(view, VIEW_FILE_FILTER) || opt_file_filter;

		view->dir = dir;
		if (!argv_format(view->env, &view->argv, argv, !view->prev, file_filter))
			return error("Failed to format %s arguments", view->name);
	}

	if (view->argv && view->argv[0] &&
	    !view_exec(view, flags))
		return error("Failed to open %s view", view->name);

	if (open_from_stdin(flags)) {
		if (!io_open(&view->io, "%s", ""))
			die("Failed to open stdin");
	}

	if (!extra)
		setup_update(view, view->ops->id);

	return SUCCESS;
}

bool
update_view(struct view *view)
{
	/* Clear the view and redraw everything since the tree sorting
	 * might have rearranged things. */
	bool redraw = view->lines == 0;
	bool can_read = true;
	struct encoding *encoding = view->encoding ? view->encoding : default_encoding;
	struct buffer line;

	if (!view->pipe)
		return true;

	if (!io_can_read(view->pipe, false)) {
		if (view->lines == 0 && view_is_displayed(view)) {
			time_t secs = time(NULL) - view->start_time;

			if (secs > 1 && secs > view->update_secs) {
				if (view->update_secs == 0)
					redraw_view(view);
				update_view_title(view);
				view->update_secs = secs;
			}
		}
		return true;
	}

	for (; io_get(view->pipe, &line, '\n', can_read); can_read = false) {
		if (encoding && !encoding_convert(encoding, &line)) {
			report("Encoding failure");
			end_update(view, true);
			return false;
		}

		if (!view->ops->read(view, &line, false)) {
			report("Allocation failure");
			end_update(view, true);
			return false;
		}
	}

	if (io_error(view->pipe)) {
		report("Failed to read: %s", io_strerror(view->pipe));
		end_update(view, true);

	} else if (io_eof(view->pipe)) {
		end_update(view, false);
	}

	if (restore_view_position(view))
		redraw = true;

	if (!view_is_displayed(view))
		return true;

	if (redraw || view->force_redraw)
		redraw_view_from(view, 0);
	else
		redraw_view_dirty(view);
	view->force_redraw = false;

	/* Update the title _after_ the redraw so that if the redraw picks up a
	 * commit reference in view->ref it'll be available here. */
	update_view_title(view);
	return true;
}

void
update_view_title(struct view *view)
{
	WINDOW *window = view->title;
	struct line *line = &view->line[view->pos.lineno];
	unsigned int view_lines, lines;
	int update_increment = view_has_flags(view, VIEW_LOG_LIKE | VIEW_GREP_LIKE)
			       ? 100
			       : view_has_flags(view, VIEW_DIFF_LIKE) ? 10 : 1;

	assert(view_is_displayed(view));

	if (view == display[current_view])
		wbkgdset(window, get_view_attr(view, LINE_TITLE_FOCUS));
	else
		wbkgdset(window, get_view_attr(view, LINE_TITLE_BLUR));

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
					   MAX(line->lineno,
					       view->pipe
					       ? update_increment *
						 (size_t) ((view->lines - view->custom_lines) / update_increment)
					       : view->lines - view->custom_lines));
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

/*
 * View opening
 */

void
split_view(struct view *prev, struct view *view)
{
	int height, width;
	bool vsplit;
	int nviews = displayed_views();

	getmaxyx(stdscr, height, width);
	vsplit = vertical_split_is_enabled(opt_vertical_split, height, width);

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

	if (view_has_flags(prev, VIEW_FLEX_WIDTH) && vsplit && nviews == 1)
		load_view(prev, NULL, OPEN_RELOAD);
}

void
maximize_view(struct view *view, bool redraw)
{
	int height, width;
	bool vsplit;
	int nviews = displayed_views();

	getmaxyx(stdscr, height, width);
	vsplit = vertical_split_is_enabled(opt_vertical_split, height, width);

	memset(display, 0, sizeof(display));
	current_view = 0;
	display[current_view] = view;
	resize_display();
	if (redraw) {
		redraw_display(false);
		report_clear();
	}

	if (view_has_flags(view, VIEW_FLEX_WIDTH) && vsplit && nviews > 1)
		load_view(view, NULL, OPEN_RELOAD);
}

void
load_view(struct view *view, struct view *prev, enum open_flags flags)
{
	bool refresh = !view_no_refresh(view, flags);

	/* When prev == view it means this is the first loaded view. */
	if (prev && view != prev) {
		view->prev = prev;
	}

	if (!refresh && view_can_refresh(view) &&
	    watch_update_single(&view->watch, WATCH_EVENT_SWITCH_VIEW)) {
		refresh = watch_dirty(&view->watch);
		if (refresh)
			flags |= OPEN_REFRESH;
	}

	if (refresh) {
		enum status_code code;

		if (view->pipe)
			end_update(view, true);
		if (view->ops->private_size) {
			if (!view->private) {
				view->private = calloc(1, view->ops->private_size);
			} else {
				if (view->ops->done)
					view->ops->done(view);
				memset(view->private, 0, view->ops->private_size);
			}
		}

		code = view->ops->open(view, flags);
		if (code != SUCCESS) {
			report("%s", get_status_message(code));
			return;
		}
	}

	if (prev) {
		bool split = !!(flags & OPEN_SPLIT);

		if (split) {
			split_view(prev, view);
		} else {
			maximize_view(view, false);
		}
	}

	restore_view_position(view);

	if (view->pipe && view->lines == 0) {
		/* Clear the old view and let the incremental updating refill
		 * the screen. */
		werase(view->win);
		/* Do not clear the position if it is the first view. */
		if (view->prev && !(flags & (OPEN_RELOAD | OPEN_REFRESH)))
			clear_position(&view->prev_pos);
		report_clear();
	} else if (view_is_displayed(view)) {
		redraw_view(view);
		report_clear();
	}
}

#define refresh_view(view) load_view(view, NULL, OPEN_REFRESH)
#define reload_view(view) load_view(view, NULL, OPEN_RELOAD)

void
open_view(struct view *prev, struct view *view, enum open_flags flags)
{
	bool reload = !!(flags & (OPEN_RELOAD | OPEN_PREPARED));
	int nviews = displayed_views();

	assert(flags ^ OPEN_REFRESH);

	if (view == prev && nviews == 1 && !reload) {
		report("Already in %s view", view->name);
		return;
	}

	if (!view_has_flags(view, VIEW_NO_GIT_DIR) && !repo.git_dir[0]) {
		report("The %s view is disabled in pager mode", view->name);
		return;
	}

	/* don't use a child view as previous view */
	if (prev && prev->parent && prev == display[1])
		prev = prev->parent;

	if (!view->keymap)
		view->keymap = get_keymap(view->name, strlen(view->name));
	load_view(view, prev ? prev : view, flags);
}

void
open_argv(struct view *prev, struct view *view, const char *argv[], const char *dir, enum open_flags flags)
{
	if (view->pipe)
		end_update(view, true);
	view->dir = dir;

	if (!argv_copy(&view->argv, argv)) {
		report("Failed to open %s view: %s", view->name, io_strerror(&view->io));
	} else {
		open_view(prev, view, flags | OPEN_PREPARED);
	}
}

/*
 * Various utilities.
 */

static struct view *sorting_view;

#define apply_comparator(cmp, o1, o2) \
	(!(o1) || !(o2)) ? !!(o2) - !!(o1) : cmp(o1, o2)

#define number_compare(size1, size2)	(*(size1) - *(size2))

#define mode_is_dir(mode)		((mode) && S_ISDIR(*(mode)))

static int
compare_view_column(enum view_column_type column, bool use_file_mode,
		    const struct line *line1, struct view_column_data *column_data1,
		    const struct line *line2, struct view_column_data *column_data2)
{
	switch (column) {
	case VIEW_COLUMN_AUTHOR:
		return apply_comparator(ident_compare, column_data1->author, column_data2->author);

	case VIEW_COLUMN_DATE:
		return apply_comparator(timecmp, column_data1->date, column_data2->date);

	case VIEW_COLUMN_ID:
		if (column_data1->reflog && column_data2->reflog)
			return apply_comparator(strcmp, column_data1->reflog, column_data2->reflog);
		return apply_comparator(strcmp, column_data1->id, column_data2->id);

	case VIEW_COLUMN_FILE_NAME:
		if (use_file_mode && mode_is_dir(column_data1->mode) != mode_is_dir(column_data2->mode))
			return mode_is_dir(column_data1->mode) ? -1 : 1;
		return apply_comparator(strcmp, column_data1->file_name, column_data2->file_name);

	case VIEW_COLUMN_FILE_SIZE:
		return apply_comparator(number_compare, column_data1->file_size, column_data2->file_size);

	case VIEW_COLUMN_LINE_NUMBER:
		return line1->lineno - line2->lineno;

	case VIEW_COLUMN_MODE:
		return apply_comparator(number_compare, column_data1->mode, column_data2->mode);

	case VIEW_COLUMN_REF:
		return apply_comparator(ref_compare, column_data1->ref, column_data2->ref);

	case VIEW_COLUMN_COMMIT_TITLE:
		return apply_comparator(strcmp, column_data1->commit_title, column_data2->commit_title);

	case VIEW_COLUMN_SECTION:
		return apply_comparator(strcmp, column_data1->section->opt.section.text,
						column_data2->section->opt.section.text);

	case VIEW_COLUMN_STATUS:
		return apply_comparator(number_compare, column_data1->status, column_data2->status);

	case VIEW_COLUMN_TEXT:
		if (column_data1->box && column_data2->box)
			return apply_comparator(strcmp, column_data1->box->text,
							column_data2->box->text);
		return apply_comparator(strcmp, column_data1->text, column_data2->text);
	}

	return 0;
}

static enum view_column_type view_column_order[] = {
	VIEW_COLUMN_FILE_NAME,
	VIEW_COLUMN_STATUS,
	VIEW_COLUMN_MODE,
	VIEW_COLUMN_FILE_SIZE,
	VIEW_COLUMN_DATE,
	VIEW_COLUMN_AUTHOR,
	VIEW_COLUMN_COMMIT_TITLE,
	VIEW_COLUMN_LINE_NUMBER,
	VIEW_COLUMN_SECTION,
	VIEW_COLUMN_TEXT,
	VIEW_COLUMN_REF,
	VIEW_COLUMN_ID,
};

static int
sort_view_compare(const void *l1, const void *l2)
{
	const struct line *line1 = l1;
	const struct line *line2 = l2;
	struct view_column_data column_data1 = {0};
	struct view_column_data column_data2 = {0};
	struct sort_state *sort = &sorting_view->sort;
	enum view_column_type column = get_sort_field(sorting_view);
	int cmp;
	int i;

	if (!sorting_view->ops->get_column_data(sorting_view, line1, &column_data1))
		return -1;
	else if (!sorting_view->ops->get_column_data(sorting_view, line2, &column_data2))
		return 1;

	cmp = compare_view_column(column, true, line1, &column_data1, line2, &column_data2);

	/* Ensure stable sorting by ordering by the other
	 * columns if the selected column values are equal. */
	for (i = 0; !cmp && i < ARRAY_SIZE(view_column_order); i++)
		if (column != view_column_order[i])
			cmp = compare_view_column(view_column_order[i], false,
						  line1, &column_data1,
						  line2, &column_data2);

	return sort->reverse ? -cmp : cmp;
}

void
resort_view(struct view *view, bool renumber)
{
	sorting_view = view;
	qsort(view->line, view->lines, sizeof(*view->line), sort_view_compare);

	if (renumber) {
		size_t i, lineno;

		for (i = 0, lineno = 1; i < view->lines; i++)
			if (view->line[i].lineno)
				view->line[i].lineno = lineno++;
	}
}

void
sort_view(struct view *view, bool change_field)
{
	struct sort_state *state = &view->sort;

	if (change_field) {
		while (true) {
			state->current = state->current->next
				? state->current->next : view->columns;
			if (get_sort_field(view) == VIEW_COLUMN_ID &&
			    !state->current->opt.id.display)
				continue;
			break;
		}
	} else {
		state->reverse = !state->reverse;
	}

	resort_view(view, false);
}

static const char *
view_column_text(struct view *view, struct view_column_data *column_data,
		 struct view_column *column)
{
	const char *text = "";

	switch (column->type) {
	case VIEW_COLUMN_AUTHOR:
		if (column_data->author)
			text = mkauthor(column_data->author, column->opt.author.width, column->opt.author.display);
		break;

	case VIEW_COLUMN_COMMIT_TITLE:
		text = column_data->commit_title;
		break;

	case VIEW_COLUMN_DATE:
		if (column_data->date)
			text = mkdate(column_data->date, column->opt.date.display,
				      column->opt.date.local, column->opt.date.format);
		break;

	case VIEW_COLUMN_REF:
		if (column_data->ref)
			text = column_data->ref->name;
		break;

	case VIEW_COLUMN_FILE_NAME:
		if (column_data->file_name)
			text = column_data->file_name;
		break;

	case VIEW_COLUMN_FILE_SIZE:
		if (column_data->file_size)
			text = mkfilesize(*column_data->file_size, column->opt.file_size.display);
		break;

	case VIEW_COLUMN_ID:
		if (column->opt.id.display)
			text = column_data->reflog ? column_data->reflog : column_data->id;
		break;

	case VIEW_COLUMN_LINE_NUMBER:
		break;

	case VIEW_COLUMN_MODE:
		if (column_data->mode)
			text = mkmode(*column_data->mode);
		break;

	case VIEW_COLUMN_STATUS:
		if (column_data->status)
			text = mkstatus(*column_data->status, column->opt.status.display);
		break;

	case VIEW_COLUMN_SECTION:
		text = column_data->section->opt.section.text;
		break;

	case VIEW_COLUMN_TEXT:
		text = column_data->text;
		break;
	}

	return text ? text : "";
}

static bool
grep_refs(struct view *view, struct view_column *column, const struct ref *ref)
{
	regmatch_t pmatch;

	for (; ref; ref = ref->next) {
		if (!regexec(view->regex, ref->name, 1, &pmatch, 0))
			return true;
	}

	return false;
}

bool
view_column_grep(struct view *view, struct line *line)
{
	struct view_column_data column_data = {0};
	bool ok = view->ops->get_column_data(view, line, &column_data);
	struct view_column *column;

	if (!ok)
		return false;

	for (column = view->columns; column; column = column->next) {
		const char *text[] = {
			view_column_text(view, &column_data, column),
			NULL
		};

		if (grep_text(view, text))
			return true;

		if (column->type == VIEW_COLUMN_COMMIT_TITLE &&
		    column->opt.commit_title.refs &&
		    grep_refs(view, column, column_data.refs))
			return true;
	}

	return false;
}

bool
view_column_info_changed(struct view *view, bool update)
{
	struct view_column *column;
	bool changed = false;

	for (column = view->columns; column; column = column->next) {
		if (memcmp(&column->prev_opt, &column->opt, sizeof(column->opt))) {
			if (!update)
				return true;
			column->prev_opt = column->opt;
			changed = true;
		}
	}

	return changed;
}

void
view_column_reset(struct view *view)
{
	struct view_column *column;

	view_column_info_changed(view, true);
	for (column = view->columns; column; column = column->next)
		column->width = 0;
}

static enum status_code
parse_view_column_config_expr(char **pos, const char **name, const char **value, bool first)
{
	size_t len = strcspn(*pos, ",");
	size_t optlen;

	if (strlen(*pos) > len)
		(*pos)[len] = 0;
	optlen = strcspn(*pos, ":=");

	if (first) {
		*name = "display";

		if (optlen == len) {
			*value = len ? *pos : "yes";
			*pos += len + 1;
			return SUCCESS;
		}

		/* Fake boolean enum value. */
		*value = "yes";
		return SUCCESS;
	}

	*name = *pos;
	if (optlen == len)
		*value = "yes";
	else
		*value = *pos + optlen + 1;
	(*pos)[optlen] = 0;
	*pos += len + 1;

	return SUCCESS;
}

static enum status_code
parse_view_column_option(struct view_column *column,
			 const char *opt_name, const char *opt_value)
{
#define DEFINE_COLUMN_OPTION_INFO(name, type, flags) \
	{ #name, STRING_SIZE(#name), #type, &opt->name, flags },

#define DEFINE_COLUMN_OPTIONS_PARSE(name, id, options) \
	if (column->type == VIEW_COLUMN_##id) { \
		struct name##_options *opt = &column->opt.name; \
		struct option_info info[] = { \
			options(DEFINE_COLUMN_OPTION_INFO) \
		}; \
		struct option_info *option = find_option_info(info, ARRAY_SIZE(info), "", opt_name); \
		if (!option) \
			return error("Unknown option `%s' for column %s", opt_name, \
				     view_column_name(VIEW_COLUMN_##id)); \
		return parse_option(option, #name, opt_value); \
	}

	COLUMN_OPTIONS(DEFINE_COLUMN_OPTIONS_PARSE);

	return error("Unknown view column option: %s", opt_name);
}

static enum status_code
parse_view_column_config_exprs(struct view_column *column, const char *arg)
{
	char buf[SIZEOF_STR] = "";
	char *pos, *end;
	bool first = true;
	enum status_code code = SUCCESS;

	string_ncopy(buf, arg, strlen(arg));

	for (pos = buf, end = pos + strlen(pos); code == SUCCESS && pos <= end; first = false) {
		const char *name = NULL;
		const char *value = NULL;

		code = parse_view_column_config_expr(&pos, &name, &value, first);
		if (code == SUCCESS)
			code = parse_view_column_option(column, name, value);
	}

	return code;
}

static enum status_code
parse_view_column_type(struct view_column *column, const char **arg)
{
	enum view_column_type type;
	size_t typelen = strcspn(*arg, ":,");

	for (type = 0; type < view_column_type_map->size; type++)
		if (enum_equals(view_column_type_map->entries[type], *arg, typelen)) {
			*arg += typelen + !!(*arg)[typelen];
			column->type = type;
			return SUCCESS;
		}

	return error("Failed to parse view column type: %.*s", (int) typelen, *arg);
}

static struct view *
find_view(const char *view_name)
{
	struct view *view;
	int i;

	foreach_view(view, i)
		if (!strncmp(view_name, view->name, strlen(view->name)))
			return view;

	return NULL;
}

enum status_code
parse_view_column_config(const char *view_name, enum view_column_type type,
			 const char *option_name, const char *argv[])
{
	struct view_column *column;
	struct view *view = find_view(view_name);

	if (!view)
		return error("Unknown view: %s", view_name);

	if (!(view->ops->column_bits & (1 << type)))
		return error("The %s view does not support %s column", view->name,
			     view_column_name(type));

	column = get_view_column(view, type);
	if (!column)
		return error("The %s view does not have a %s column configured", view->name,
			     view_column_name(type));

	if (option_name)
		return parse_view_column_option(column, option_name, argv[0]);
	return parse_view_column_config_exprs(column, argv[0]);
}

enum status_code
parse_view_config(struct view_column **column_ref, const char *view_name, const char *argv[])
{
	enum status_code code = SUCCESS;
	size_t size = argv_size(argv);
	struct view_column *columns;
	struct view_column *column;
	struct view *view = find_view(view_name);
	int i;

	if (!view)
		return error("Unknown view: %s", view_name);

	columns = calloc(size, sizeof(*columns));
	if (!columns)
		return ERROR_OUT_OF_MEMORY;

	for (i = 0, column = NULL; code == SUCCESS && i < size; i++) {
		const char *arg = argv[i];

		if (column)
			column->next = &columns[i];
		column = &columns[i];

		code = parse_view_column_type(column, &arg);
		if (code != SUCCESS)
			break;

		if (!(view->ops->column_bits & (1 << column->type)))
			return error("The %s view does not support %s column", view->name,
				     view_column_name(column->type));

		if ((column->type == VIEW_COLUMN_TEXT ||
		     column->type == VIEW_COLUMN_COMMIT_TITLE) &&
		     i + 1 < size)
			return error("The %s column must always be last",
				     view_column_name(column->type));

		code = parse_view_column_config_exprs(column, arg);
		column->prev_opt = column->opt;
	}

	if (code == SUCCESS) {
		free(view->columns);
		view->columns = columns;
		view->sort.current = view->columns;
		*column_ref = columns;
	} else {
		free(columns);
	}

	return code;
}

static enum status_code
format_view_column_options(struct option_info options[], size_t options_size, char buf[], size_t bufsize)
{
	char name[SIZEOF_STR];
	char value[SIZEOF_STR];
	size_t bufpos = 0;
	const char *sep = ":";
	int i;

	buf[0] = 0;

	for (i = 0; i < options_size; i++) {
		struct option_info *option = &options[i];
		const char *assign = "=";

		if (!enum_name_copy(name, sizeof(name), option->name)
		    || !format_option_value(option, value, sizeof(value)))
			return error("No space left in buffer");

		if (!strcmp(name, "display")) {
			name[0] = 0;
			assign = "";

		}

		if (!strcmp(option->type, "bool") && !strcmp(value, "yes")) {
			if (!*name) {
				sep = ":yes,";
				continue;
			}

			/* For non-display boolean options 'yes' is implied. */
#if 0
			value[0] = 0;
			assign = "";
#endif
		}

		if (!strcmp(option->type, "int") && !strcmp(value, "0"))
			continue;

		if (!string_nformat(buf, bufsize, &bufpos, "%s%s%s%s",
				    sep, name, assign, value))
			return error("No space left in buffer");

		sep = ",";
	}

	return SUCCESS;
}

static enum status_code
format_view_column(struct view_column *column, char buf[], size_t bufsize)
{
#define FORMAT_COLUMN_OPTION_INFO(name, type, flags) \
	{ #name, STRING_SIZE(#name), #type, &opt->name, flags },

#define FORMAT_COLUMN_OPTIONS_PARSE(col_name, id, options) \
	if (column->type == VIEW_COLUMN_##id) { \
		struct col_name##_options *opt = &column->opt.col_name; \
		struct option_info info[] = { \
			options(FORMAT_COLUMN_OPTION_INFO) \
		}; \
		\
		return format_view_column_options(info, ARRAY_SIZE(info), buf, bufsize); \
	}

	COLUMN_OPTIONS(FORMAT_COLUMN_OPTIONS_PARSE);

	return error("Unknown view column type: %d", column->type);
}

enum status_code
format_view_config(struct view_column *column, char buf[], size_t bufsize)
{
	const struct enum_map *map = view_column_type_map;
	const char *sep = "";
	size_t bufpos = 0;
	char type[SIZEOF_STR];
	char value[SIZEOF_STR];

	for (; column; column = column->next) {
		enum status_code code = format_view_column(column, value, sizeof(value));

		if (code != SUCCESS)
			return code;

		if (!enum_name_copy(type, sizeof(type), map->entries[column->type].name)
		    || !string_nformat(buf, bufsize, &bufpos, "%s%s%s",
				       sep, type, value))
			return error("No space left in buffer");

		sep = " ";
	}

	return SUCCESS;
}

struct view_column *
get_view_column(struct view *view, enum view_column_type type)
{
	struct view_column *column;

	for (column = view->columns; column; column = column->next)
		if (column->type == type)
			return column;
	return NULL;
}

#define MAXWIDTH(maxwidth)	(width == 0 ? maxwidth < 0 ? -maxwidth * view->width / 100 : maxwidth : 0)

bool
view_column_info_update(struct view *view, struct line *line)
{
	struct view_column_data column_data = {0};
	struct view_column *column;
	bool changed = false;

	if (!view->ops->get_column_data(view, line, &column_data))
		return false;

	for (column = view->columns; column; column = column->next) {
		const char *text = view_column_text(view, &column_data, column);
		int width = 0;
		int maxwidth = 0;

		switch (column->type) {
		case VIEW_COLUMN_AUTHOR:
			width = column->opt.author.width;
			maxwidth = MAXWIDTH(column->opt.author.maxwidth);
			break;

		case VIEW_COLUMN_COMMIT_TITLE:
			break;

		case VIEW_COLUMN_DATE:
			width = column->opt.date.width;
			break;

		case VIEW_COLUMN_FILE_NAME:
			width = column->opt.file_name.width;
			maxwidth = MAXWIDTH(column->opt.file_name.maxwidth);
			break;

		case VIEW_COLUMN_FILE_SIZE:
			width = column->opt.file_size.width;
			break;

		case VIEW_COLUMN_ID:
			width = column->opt.id.width;
			if (!width)
				width = opt_id_width;
			if (!column_data.reflog && !width)
				width = 7;
			break;

		case VIEW_COLUMN_LINE_NUMBER:
			width = column->opt.line_number.width;
			if (!width) {
				if (column_data.line_number)
					width = count_digits(*column_data.line_number);
				else
					width = count_digits(view->lines);
			}
			if (width < 3)
				width = 3;
			break;

		case VIEW_COLUMN_MODE:
			width = column->opt.mode.width;
			break;

		case VIEW_COLUMN_REF:
			width = column->opt.ref.width;
			maxwidth = MAXWIDTH(column->opt.ref.maxwidth);
			break;

		case VIEW_COLUMN_SECTION:
			break;

		case VIEW_COLUMN_STATUS:
			break;

		case VIEW_COLUMN_TEXT:
			break;
		}

		if (*text && !width)
			width = utf8_width(text);

		if ((maxwidth > 0) && (width > maxwidth))
			width = maxwidth;

		if (width > column->width) {
			column->width = width;
			changed = true;
		}
	}

	if (changed)
		view->force_redraw = true;
	return changed;
}

struct line *
find_line_by_type(struct view *view, struct line *line, enum line_type type, int direction)
{
	for (; view_has_line(view, line); line += direction)
		if (line->type == type)
			return line;

	return NULL;
}

/*
 * Line utilities.
 */

DEFINE_ALLOCATOR(realloc_lines, struct line, 256)

static inline char *
box_text_offset(struct box *box, size_t cells)
{
	return (char *) &box->cell[cells];
}

void
box_text_copy(struct box *box, size_t cells, const char *src, size_t srclen)
{
	char *dst = box_text_offset(box, cells);

	box->text = dst;
	strncpy(dst, src, srclen);
}

struct line *
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

		memmove(line + 1, line, (view->lines - 1 - pos) * sizeof(*view->line));
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

struct line *
add_line(struct view *view, const void *data, enum line_type type, size_t data_size, bool custom)
{
	return add_line_at(view, view->lines, data, type, data_size, custom);
}

struct line *
add_line_alloc_(struct view *view, void **ptr, enum line_type type, size_t data_size, bool custom)
{
	struct line *line = add_line(view, NULL, type, data_size, custom);

	if (line)
		*ptr = line->data;
	return line;
}

struct line *
add_line_nodata(struct view *view, enum line_type type)
{
	return add_line(view, NULL, type, 0, false);
}

struct line *
add_line_text_at_(struct view *view, unsigned long pos, const char *text, size_t textlen, enum line_type type, size_t cells, bool custom)
{
	struct box *box;
	struct line *line = add_line_at(view, pos, NULL, type, box_sizeof(NULL, cells, textlen), custom);

	if (!line)
		return NULL;

	box = line->data;
	box->cell[box->cells].length = textlen;
	box->cell[box->cells++].type = type;
	box_text_copy(box, cells, text, textlen);

	if (view->ops->column_bits)
		view_column_info_update(view, line);
	return line;
}

struct line *
add_line_text_at(struct view *view, unsigned long pos, const char *text, enum line_type type, size_t cells)
{
	size_t textlen = strlen(text);

	/* If the filename contains a space, Git adds a tab at the end of
	 * the line, to satisfy GNU patch. Drop it to correct the filename. */
	if ((type == LINE_DIFF_ADD_FILE || type == LINE_DIFF_DEL_FILE) && text[textlen - 1] == '\t')
		textlen--;

	return add_line_text_at_(view, pos, text, textlen, type, cells, false);
}

struct line *
add_line_text(struct view *view, const char *text, enum line_type type)
{
	return add_line_text_at(view, view->lines, text, type, 1);
}

struct line * PRINTF_LIKE(3, 4)
add_line_format(struct view *view, enum line_type type, const char *fmt, ...)
{
	char buf[SIZEOF_STR];
	int retval;

	FORMAT_BUFFER(buf, sizeof(buf), fmt, retval, false);
	return retval >= 0 ? add_line_text(view, buf, type) : NULL;
}

bool
append_line_format(struct view *view, struct line *line, const char *fmt, ...)
{
	struct box *box;
	size_t textlen = box_text_length(line->data);
	int fmtlen, retval;
	va_list args;
	char *text;

	va_start(args, fmt);
	fmtlen = vsnprintf(NULL, 0, fmt, args);
	va_end(args);

	if (fmtlen <= 0)
		return false;

	box = realloc(line->data, box_sizeof(line->data, 0, fmtlen));
	if (!box)
		return false;

	box->text = text = box_text_offset(box, box->cells);
	FORMAT_BUFFER(text + textlen, fmtlen + 1, fmt, retval, false);
	if (retval < 0)
		text[textlen] = 0;

	box->cell[box->cells - 1].length += fmtlen;
	line->data = box;
	line->dirty = true;

	if (view->ops->column_bits)
		view_column_info_update(view, line);

	return true;
}

/*
 * Global view state.
 */

/* Included last to not pollute the rest of the file. */
#include "tig/main.h"
#include "tig/diff.h"
#include "tig/log.h"
#include "tig/reflog.h"
#include "tig/tree.h"
#include "tig/blob.h"
#include "tig/blame.h"
#include "tig/refs.h"
#include "tig/status.h"
#include "tig/stage.h"
#include "tig/stash.h"
#include "tig/grep.h"
#include "tig/pager.h"
#include "tig/help.h"

static struct view *views[] = {
#define VIEW_DATA(id, name) &name##_view
	VIEW_INFO(VIEW_DATA)
};

struct view *
get_view(int i)
{
	return 0 <= i && i < ARRAY_SIZE(views) ? views[i] : NULL;
}

/* vim: set ts=8 sw=8 noexpandtab: */
