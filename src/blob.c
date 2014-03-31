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

#include "tig/refdb.h"
#include "tig/parse.h"
#include "tig/display.h"
#include "tig/log.h"
#include "tig/pager.h"
#include "tig/tree.h"

struct blob_state {
	char commit[SIZEOF_REF];
	const char *file;
};

static bool
blob_open(struct view *view, enum open_flags flags)
{
	struct blob_state *state = view->private;
	static const char *blob_argv[] = {
		"git", "cat-file", "blob", "%(blob)", NULL
	};
	const char **argv = (flags & OPEN_PREPARED) ? view->argv : blob_argv;

	if (argv != blob_argv) {
		state->file = get_path(view->env->file);
		state->commit[0] = 0;
	}

	if (!state->file && !view->env->blob[0] && view->env->file[0]) {
		const char *commit = view->env->commit[0] ? view->env->commit : "HEAD";
		char blob_spec[SIZEOF_STR];
		const char *rev_parse_argv[] = {
			"git", "rev-parse", blob_spec, NULL
		};

		if (!string_format(blob_spec, "%s:%s", commit, view->env->file) ||
		    !io_run_buf(rev_parse_argv, view->env->blob, sizeof(view->env->blob))) {
			report("Failed to resolve blob from file name");
			return FALSE;
		}

		string_ncopy(state->commit, commit, strlen(commit));
	}

	if (!state->file && !view->env->blob[0]) {
		report("No file chosen, press %s to open tree view",
				get_view_key(view, REQ_VIEW_TREE));
		return FALSE;
	}

	view->encoding = get_path_encoding(view->env->file, default_encoding);
	string_copy(view->ref, view->env->file);

	return begin_update(view, NULL, argv, flags);
}

static bool
blob_read(struct view *view, char *line)
{
	if (!line) {
		if (view->env->lineno > 0) {
			select_view_line(view, view->env->lineno);
			view->env->lineno = 0;
		}
		return TRUE;
	}

	return add_line_text(view, line, LINE_DEFAULT) != NULL;
}

static enum request
blob_request(struct view *view, enum request request, struct line *line)
{
	struct blob_state *state = view->private;

	switch (request) {
	case REQ_VIEW_BLAME:
		string_ncopy(view->env->ref, state->commit, strlen(state->commit));
		view->env->lineno = line - view->line;
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
	VIEW_NO_FLAGS,
	sizeof(struct blob_state),
	blob_open,
	blob_read,
	pager_draw,
	blob_request,
	pager_grep,
	pager_select,
};

DEFINE_VIEW(blob);

/* vim: set ts=8 sw=8 noexpandtab: */
