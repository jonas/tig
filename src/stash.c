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

#include "tig/display.h"
#include "tig/draw.h"
#include "tig/main.h"
#include "tig/diff.h"

static bool
stash_open(struct view *view, enum open_flags flags)
{
	static const char *stash_argv[] = { "git", "stash", "list",
		encoding_arg, "--no-color", "--pretty=raw", NULL };
	struct main_state *state = view->private;

	state->with_graph = false;
	watch_register(&view->watch, WATCH_STASH);
	return begin_update(view, NULL, stash_argv, flags | OPEN_RELOAD);
}

static void
stash_select(struct view *view, struct line *line)
{
	main_select(view, line);
	string_format(view->env->stash, "stash@{%d}", line->lineno - 1);
	string_copy(view->ref, view->env->stash);
}

enum request
stash_request(struct view *view, enum request request, struct line *line)
{
	enum open_flags flags = (view_is_displayed(view) && request != REQ_VIEW_DIFF)
				? OPEN_SPLIT : OPEN_DEFAULT;
	struct view *diff = &diff_view;

	switch (request) {
	case REQ_VIEW_DIFF:
	case REQ_ENTER:
		if (view_is_displayed(view) && display[0] != view)
			maximize_view(view, true);

		if (!view_is_displayed(diff) ||
		    strcmp(view->env->stash, diff->ref)) {
			const char *diff_argv[] = {
				"git", "stash", "show", encoding_arg, "--pretty=fuller",
					"--root", "--patch-with-stat", use_mailmap_arg(),
					show_notes_arg(), diff_context_arg(),
					ignore_space_arg(), "%(diffargs)",
					"--no-color", "%(stash)", NULL
			};

			if (!argv_format(diff_view.env, &diff_view.argv, diff_argv, false, false))
				report("Failed to format argument");
			else
				open_view(view, &diff_view, flags | OPEN_PREPARED);
		}
		return REQ_NONE;

	default:
		return main_request(view, request, line);
	}
}

static struct view_ops stash_ops = {
	"stash",
	"",
	VIEW_SEND_CHILD_ENTER | VIEW_REFRESH,
	sizeof(struct main_state),
	stash_open,
	main_read,
	view_column_draw,
	stash_request,
	view_column_grep,
	stash_select,
	main_done,
	view_column_bit(AUTHOR) | view_column_bit(COMMIT_TITLE) |
		view_column_bit(DATE) | view_column_bit(ID) |
		view_column_bit(LINE_NUMBER),
	main_get_column_data,
};

DEFINE_VIEW(stash);

/* vim: set ts=8 sw=8 noexpandtab: */
