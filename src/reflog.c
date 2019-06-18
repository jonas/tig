/* Copyright (c) 2006-2019 Jonas Fonseca <jonas.fonseca@gmail.com>
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

#include "tig/display.h"
#include "tig/draw.h"
#include "tig/git.h"
#include "tig/main.h"

static enum status_code
reflog_open(struct view *view, enum open_flags flags)
{
	struct main_state *state = view->private;
	const char *reflog_argv[] = {
		"git", "reflog", "show", encoding_arg, "%(cmdlineargs)",
			"%(revargs)", "--no-color", "--pretty=raw", NULL
	};

	if (is_initial_view(view) && opt_file_args)
		die("No revisions match the given arguments.");

	state->with_graph = false;
	watch_register(&view->watch, WATCH_HEAD | WATCH_REFS);
	return begin_update(view, NULL, reflog_argv, flags);
}

static enum request
reflog_request(struct view *view, enum request request, struct line *line)
{
	struct commit *commit = line->data;

	switch (request) {
	case REQ_ENTER:
	{
		const char *main_argv[] = {
			GIT_MAIN_LOG(encoding_arg, commit_order_arg(),
				"%(mainargs)", "", commit->id, "",
				show_notes_arg(), log_custom_pretty_arg())
		};

		if (!argv_format(main_view.env, &main_view.argv, main_argv, false, false))
			report("Failed to format argument");
		else
			open_view(view, &main_view, OPEN_SPLIT | OPEN_PREPARED);
		return REQ_NONE;
	}

	default:
		return main_request(view, request, line);
	}
}

static struct view_ops reflog_ops = {
	"reference",
	argv_env.head,
	VIEW_LOG_LIKE | VIEW_REFRESH,
	sizeof(struct main_state),
	reflog_open,
	main_read,
	view_column_draw,
	reflog_request,
	view_column_grep,
	main_select,
	main_done,
	view_column_bit(AUTHOR) | view_column_bit(COMMIT_TITLE) |
		view_column_bit(DATE) | view_column_bit(ID) |
		view_column_bit(LINE_NUMBER),
	main_get_column_data,
};

DEFINE_VIEW(reflog);

/* vim: set ts=8 sw=8 noexpandtab: */
