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

#include "refs.h"
#include "display.h"
#include "log.h"
#include "pager.h"
#include "tree.h"

static bool
blob_open(struct view *view, enum open_flags flags)
{
	static const char *blob_argv[] = {
		"git", "cat-file", "blob", "%(blob)", NULL
	};

	if (!view->env->blob[0] && view->env->file[0]) {
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
	}

	if (!view->env->blob[0]) {
		report("No file chosen, press %s to open tree view",
			get_view_key(view, REQ_VIEW_TREE));
		return FALSE;
	}

	view->encoding = get_path_encoding(view->env->file, default_encoding);

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
			string_copy(view->env->ref, view->parent->vid);
		return request;

	case REQ_EDIT:
		open_blob_editor(view->vid, NULL, (line - view->line) + 1);
		return REQ_NONE;
	default:
		return pager_request(view, request, line);
	}
}

struct view_ops blob_ops = {
	"line",
	{ "blob" },
	view_env.blob,
	VIEW_NO_FLAGS,
	0,
	blob_open,
	blob_read,
	pager_draw,
	blob_request,
	pager_grep,
	pager_select,
};

/* vim: set ts=8 sw=8 noexpandtab: */
