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

#include "tig/refdb.h"
#include "tig/options.h"
#include "tig/parse.h"
#include "tig/repo.h"
#include "tig/display.h"
#include "tig/prompt.h"
#include "tig/draw.h"
#include "tig/blob.h"
#include "tig/grep.h"

struct grep_line {
	const char *file;
	unsigned long lineno;
	char text[1];
};

struct grep_state {
	const char *last_file;
	bool no_file_group;
};

static struct grep_line *
grep_get_line(const struct line *line)
{
	static struct grep_line grep_line;

	if (line->type == LINE_DEFAULT)
		return line->data;

	grep_line.file = line->type == LINE_DELIMITER ? "" : get_path(box_text(line));
	return &grep_line;
}

static bool
grep_get_column_data(struct view *view, const struct line *line, struct view_column_data *column_data)
{
	struct grep_line *grep = grep_get_line(line);

	if (line->type == LINE_DELIMITER) {
		static struct view_column separator_column;

		separator_column.type = VIEW_COLUMN_TEXT;
		column_data->section = &separator_column;
		column_data->text = "--";
		return true;
	}

	if (*grep->file && !*grep->text) {
		static struct view_column file_name_column;

		file_name_column.type = VIEW_COLUMN_FILE_NAME;
		file_name_column.opt.file_name.display = FILENAME_ALWAYS;

		column_data->section = &file_name_column;
	}

	column_data->line_number = &grep->lineno;
	column_data->file_name = grep->file;
	column_data->text = grep->text;
	return true;
}

static void
grep_select(struct view *view, struct line *line)
{
	struct grep_line *grep = grep_get_line(line);

	if (!*grep->file)
		return;
	view->env->ref[0] = 0;
	string_ncopy(view->env->file, grep->file, strlen(grep->file));
	string_ncopy(view->ref, grep->file, strlen(grep->file));
	view->env->lineno = grep->lineno + 1;
}

static const char *grep_args[] = {
	"git", "grep", "--no-color", "-n", "-z", "--full-name", NULL
};

static const char **grep_argv;

static bool
grep_prompt(void)
{
	const char *argv[SIZEOF_ARG];
	int argc = 0;
	char *grep = read_prompt("grep: ");

	report_clear();

	if (!grep || !*grep || !argv_from_string_no_quotes(argv, &argc, grep))
		return false;
	if (grep_argv)
		argv_free(grep_argv);
	return argv_append_array(&grep_argv, argv);
}

void
open_grep_view(struct view *prev)
{
	struct view *view = &grep_view;
	bool in_grep_view = prev == view;

	if ((!prev && is_initial_view(view)) || (view->lines && !in_grep_view)) {
		open_view(prev, view, OPEN_DEFAULT);
	} else {
		if (grep_prompt()) {
			clear_position(&view->pos);
			open_view(prev, view, OPEN_RELOAD);
		}
	}
}

static enum status_code
grep_open(struct view *view, enum open_flags flags)
{
	struct grep_state *state = view->private;
	const char **argv = NULL;

	if (is_initial_view(view)) {
		grep_argv = opt_cmdline_args;
		opt_cmdline_args = NULL;
	}

	if (!argv_append_array(&argv, grep_args) ||
	    !argv_append_array(&argv, grep_argv))
		return ERROR_OUT_OF_MEMORY;

	{
		struct view_column *column = get_view_column(view, VIEW_COLUMN_FILE_NAME);

		state->no_file_group = !column || column->opt.file_name.display != FILENAME_NO;
	}

	return begin_update(view, NULL, argv, flags);
}

static enum request
grep_request(struct view *view, enum request request, struct line *line)
{
	struct grep_state *state = view->private;
	struct grep_line *grep = grep_get_line(line);
	struct view *file_view = &blob_view;

	switch (request) {
	case REQ_REFRESH:
		refresh_view(view);
		return REQ_NONE;

	case REQ_ENTER:
		if (!*grep->file)
			return REQ_NONE;
		if (file_view->parent == view && file_view->prev == view &&
		    state->last_file == grep->file && view_is_displayed(file_view)) {
			if (*grep->text) {
				select_view_line(file_view, grep->lineno);
				update_view_title(file_view);
			}

		} else {
			const char *file_argv[] = { repo.exec_dir, grep->file, NULL };

			clear_position(&file_view->pos);
			view->env->goto_lineno = grep->lineno;
			view->env->blob[0] = 0;
			open_argv(view, file_view, file_argv, repo.exec_dir, OPEN_SPLIT | OPEN_RELOAD);
		}
		state->last_file = grep->file;
		return REQ_NONE;

	case REQ_EDIT:
		if (!*grep->file)
			return request;
		open_editor(grep->file, grep->lineno + 1);
		return REQ_NONE;

	case REQ_VIEW_BLAME:
		view->env->ref[0] = 0;
		view->env->goto_lineno = grep->lineno;
		return request;

	default:
		return request;
	}
}

static bool
grep_read(struct view *view, struct buffer *buf, bool force_stop)
{
	struct grep_state *state = view->private;
	struct grep_line *grep;
	char *lineno, *text;
	struct line *line;
	const char *file;
	size_t textlen;

	if (!buf) {
		state->last_file = NULL;
		if (!view->lines) {
			view->ref[0] = 0;
			report("No matches found");
		}
		return true;
	}

	if (!strcmp(buf->data, "--"))
		return add_line_nodata(view, LINE_DELIMITER) != NULL;

	lineno = io_memchr(buf, buf->data, 0);
	text = io_memchr(buf, lineno, 0);

	/*
	 * No data indicates binary file matches, e.g.:
	 *	> git grep vertical- -- test
	 *	test/graph/20-tig-all-long-test:● │ Add "auto" vertical-split
	 *	Binary file test/graph/20-tig-all-long-test.in matches
	 */
	if (!lineno || !text)
		return true;

	textlen = strlen(text);

	file = get_path(buf->data);
	if (!file)
		return false;

	if (!state->no_file_group && file != state->last_file &&
	    !add_line_text(view, file, LINE_FILE))
		return false;

	line = add_line_alloc(view, &grep, LINE_DEFAULT, textlen, false);
	if (!line)
		return false;

	grep->file = file;
	grep->lineno = atoi(lineno);
	if (grep->lineno > 0)
		grep->lineno -= 1;
	strncpy(grep->text, text, textlen + 1);
	view_column_info_update(view, line);

	state->last_file = file;

	return true;
}

static struct view_ops grep_ops = {
	"line",
	"",
	VIEW_REFRESH | VIEW_GREP_LIKE,
	sizeof(struct grep_state),
	grep_open,
	grep_read,
	view_column_draw,
	grep_request,
	view_column_grep,
	grep_select,
	NULL,
	view_column_bit(FILE_NAME) | view_column_bit(LINE_NUMBER) |
		view_column_bit(TEXT),
	grep_get_column_data,
};

DEFINE_VIEW(grep);

/* vim: set ts=8 sw=8 noexpandtab: */
