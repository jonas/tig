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
#include "tig/parse.h"
#include "tig/repo.h"
#include "tig/display.h"
#include "tig/draw.h"
#include "tig/ui.h"
#include "tig/pager.h"
#include "tig/tree.h"
#include "tig/blob.h"

struct blob_state {
	char commit[SIZEOF_REF];
	const char *file;
};

void
open_blob_view(struct view *prev, enum open_flags flags)
{
	struct view *view = &blob_view;
	bool in_blob_view = prev == view;
	bool has_blob_selection = view->env->blob[0] || view->env->file[0];

	if (!in_blob_view && (view->lines || has_blob_selection)) {
		if (view->env->goto_lineno > 0)
			flags |= OPEN_RELOAD;
		open_view(prev, view, flags);

	} else {
		const char *file = open_file_finder(view->env->commit);

		if (file) {
			clear_position(&view->pos);
			string_ncopy(view->env->file, file, strlen(file));
			view->env->blob[0] = 0;
			open_view(prev, view, OPEN_RELOAD);
		}
	}
}

static enum status_code
blob_open(struct view *view, enum open_flags flags)
{
	struct blob_state *state = view->private;
	static const char *blob_argv[] = {
		"git", "cat-file", "blob", "%(blob)", NULL
	};
	const char **argv = (flags & (OPEN_PREPARED | OPEN_REFRESH)) ? view->argv : blob_argv;

	if (argv != blob_argv) {
		state->file = get_path(view->env->file);
		state->commit[0] = 0;
	}

	if (!state->file && !view->env->blob[0] && view->env->file[0]) {
		const char *commit = view->env->commit[0] && !string_rev_is_null(view->env->commit)
				   ? view->env->commit : "HEAD";
		char blob_spec[SIZEOF_STR];
		const char *rev_parse_argv[] = {
			"git", "rev-parse", blob_spec, NULL
		};

		if (!string_format(blob_spec, "%s:%s", commit, view->env->file) ||
		    !io_run_buf(rev_parse_argv, view->env->blob, sizeof(view->env->blob), NULL, false))
			return error("Failed to resolve blob from file name");

		string_ncopy(state->commit, commit, strlen(commit));
	}

	if (!state->file && !view->env->blob[0])
		return error("No file chosen, press %s to open tree view",
			     get_view_key(view, REQ_VIEW_TREE));

	view->encoding = get_path_encoding(view->env->file, default_encoding);

	if (*view->env->file)
		string_copy(view->ref, view->env->file);
	else
		string_copy_rev(view->ref, view->ops->id);

	return begin_update(view, NULL, argv, flags);
}

static bool
blob_read(struct view *view, struct buffer *buf, bool force_stop)
{
	if (!buf) {
		if (view->env->goto_lineno > 0) {
			select_view_line(view, view->env->goto_lineno);
			view->env->goto_lineno = 0;
		}
		return true;
	}

	return pager_common_read(view, buf->data, LINE_DEFAULT, NULL);
}

static void
blob_select(struct view *view, struct line *line)
{
	struct blob_state *state = view->private;

	if (state->file)
		string_format(view->env->file, "%s", state->file);
	view->env->lineno = view->pos.lineno + 1;
}

static enum request
blob_request(struct view *view, enum request request, struct line *line)
{
	struct blob_state *state = view->private;

	switch (request) {
	case REQ_REFRESH:
		if (!state->file) {
			report("Cannot reload immutable blob");
		} else {
			string_ncopy(view->env->file, state->file, strlen(state->file));
			refresh_view(view);
		}
		return REQ_NONE;

	case REQ_VIEW_BLAME:
		string_ncopy(view->env->ref, state->commit, strlen(state->commit));
		view->env->goto_lineno = line - view->line;
		return request;

	case REQ_EDIT:
		if (state->file)
			open_editor(state->file, (line - view->line) + 1);
		else
			open_blob_editor(view->vid, NULL, (line - view->line) + 1);
		return REQ_NONE;

	default:
		return pager_request(view, request, line);
	}
}

static struct view_ops blob_ops = {
	"line",
	argv_env.blob,
	VIEW_NO_FLAGS | VIEW_REFRESH,
	sizeof(struct blob_state),
	blob_open,
	blob_read,
	view_column_draw,
	blob_request,
	view_column_grep,
	blob_select,
	NULL,
	view_column_bit(LINE_NUMBER) | view_column_bit(TEXT),
	pager_get_column_data,
};

DEFINE_VIEW(blob);

/* vim: set ts=8 sw=8 noexpandtab: */
