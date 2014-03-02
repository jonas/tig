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

#include "tig/refs.h"
#include "tig/options.h"
#include "tig/parse.h"
#include "tig/repo.h"
#include "tig/display.h"
#include "tig/draw.h"
#include "tig/grep.h"

struct grep_line {
	const char *file;
	unsigned long lineno;
	char text[1];
};

struct grep_state {
	const char *last_file;
	int lineno_digits;
	int filename_width;
};

#define grep_view_lineno(grep)	((grep)->lineno > 0 ? (grep)->lineno - 1 : 0)

static struct grep_line *
grep_get_line(struct line *line)
{
	static struct grep_line grep_line;

	if (line->type == LINE_DEFAULT)
		return line->data;

	grep_line.file = line->type == LINE_DELIMITER ? "" : get_path(line->data);
	return &grep_line;
}

static bool
grep_draw(struct view *view, struct line *line, unsigned int lineno)
{
	struct grep_state *state = view->private;
	struct grep_line *grep = grep_get_line(line);

	if (*grep->file && !grep->lineno) {
		//draw_lineno_custom(view, 13, TRUE, 42);
		draw_filename_custom(view, grep->file, TRUE, state->filename_width);
		return TRUE;
	}


	if (grep->lineno && draw_lineno_custom(view, grep->lineno, TRUE, 1))
		return TRUE;

	draw_text(view, LINE_DEFAULT, grep->text);
	return TRUE;
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
}

static const char *grep_args[] = {
	"git", "grep", "--no-color", "-n", "-z", NULL
};

static const char **grep_argv;

static bool
grep_prompt(void)
{
	const char *argv[SIZEOF_ARG];
	int argc = 0;
	char *grep = read_prompt("grep: ");

	if (!grep || !argv_from_string_no_quotes(argv, &argc, grep))
		return FALSE;
	if (grep_argv)
		argv_free(grep_argv);
	return argv_append_array(&grep_argv, argv);
}

void
open_grep_view(struct view *prev)
{
	struct view *view = VIEW(REQ_VIEW_GREP);
	bool in_grep_view = prev == view;

	if (is_initial_view(view) || (view->lines && !in_grep_view)) {
		open_view(prev, REQ_VIEW_GREP, OPEN_DEFAULT);
	} else {
		if (grep_prompt())
			open_view(prev, REQ_VIEW_GREP, OPEN_RELOAD);
	}
}

static bool
grep_open(struct view *view, enum open_flags flags)
{
	struct grep_state *state = view->private;
	const char **argv = NULL;

	if (is_initial_view(view)) {
		grep_argv = opt_cmdline_argv;
		opt_cmdline_argv = NULL;
	}

	if (!argv_append_array(&argv, grep_args) ||
	    !argv_append_array(&argv, grep_argv))
		return FALSE;

	state->filename_width = opt_show_filename_width;
	return begin_update(view, NULL, argv, flags);
}

static enum request
grep_request(struct view *view, enum request request, struct line *line)
{
	struct grep_state *state = view->private;
	struct grep_line *grep = grep_get_line(line);
	enum request file_view_req = REQ_VIEW_BLOB;
	struct view *file_view = VIEW(file_view_req);

	switch (request) {
	case REQ_REFRESH:
		refresh_view(view);
		return REQ_NONE;

	case REQ_ENTER:
		if (!*grep->file)
			return REQ_NONE;
		if (file_view->parent == view && file_view->prev == view &&
		    state->last_file == grep->file && view_is_displayed(file_view)) {
			if (grep_view_lineno(grep))
				select_view_line(file_view, grep_view_lineno(grep));

		} else {
			const char *file_argv[] = { repo.cdup, grep->file, NULL };

			clear_position(&file_view->pos);
			view->env->lineno = grep_view_lineno(grep);
			view->env->blob[0] = 0;
			open_argv(view, file_view, file_argv, repo.cdup, OPEN_SPLIT | OPEN_RELOAD);
		}
		state->last_file = grep->file;
		return REQ_NONE;

	case REQ_EDIT:
		if (!*grep->file)
			return request;
		open_editor(grep->file, grep->lineno);
		return REQ_NONE;

	case REQ_VIEW_BLAME:
		view->env->ref[0] = 0;
		view->env->lineno = grep_view_lineno(grep);
		return request;

	default:
		return request;
	}
}

static inline void *
io_memchr(struct io *io, char *data, int c)
{
	if (data < io->buf || io->bufpos <= data)
		return NULL;
	return memchr(data, c, io->bufpos - data - 1);
}

static bool
grep_read(struct view *view, char *line)
{
	struct grep_state *state = view->private;
	struct grep_line *grep;
	char *lineno, *text;
	const char *file;
	size_t textlen;
	unsigned long lineno_digits;

	if (!line) {
		view->digits = state->lineno_digits;
		state->last_file = NULL;
		return TRUE;
	}

	if (!strcmp(line, "--"))
		return add_line_nodata(view, LINE_DELIMITER) != NULL;

	lineno = io_memchr(&view->io, line, 0);
	text = io_memchr(&view->io, lineno + 1, 0);

	if (!lineno || !text)
		return FALSE;

	lineno += 1;
	text += 1;
	textlen = strlen(text);

	file = get_path(line);
	if (!file ||
	    (file != state->last_file && !add_line_text(view, file, LINE_FILENAME)))
		return FALSE;

	if (!add_line_alloc(view, &grep, LINE_DEFAULT, textlen, FALSE))
		return FALSE;

	grep->file = file;
	grep->lineno = atoi(lineno);
	strncpy(grep->text, text, textlen);
	grep->text[textlen] = 0;

	lineno_digits = count_digits(grep->lineno);
	if (lineno_digits > state->lineno_digits) {
		view->digits = state->lineno_digits = lineno_digits;
		view->force_redraw = TRUE;
	}

	if (strlen(grep->file) > state->filename_width)
		state->filename_width = strlen(grep->file);

	state->last_file = file;

	return TRUE;
}

static bool
grep_grep(struct view *view, struct line *line)
{
	struct grep_line *grep = grep_get_line(line);
	const char *text[] = {
		grep->text,
		grep->file,
		NULL
	};

	return grep_text(view, text);
}

struct view_ops grep_ops = {
	"line",
	{ "grep" },
	"grep",
	VIEW_ALWAYS_LINENO | VIEW_CUSTOM_DIGITS | VIEW_REFRESH,
	sizeof(struct grep_state),
	grep_open,
	grep_read,
	grep_draw,
	grep_request,
	grep_grep,
	grep_select,
};

/* vim: set ts=8 sw=8 noexpandtab: */
