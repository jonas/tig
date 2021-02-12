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
#include "tig/repo.h"

static enum status_code
stash_open(struct view *view, enum open_flags flags)
{
	static const char *stash_argv[] = { "git", "stash", "list",
		encoding_arg, "--no-color", "--pretty=raw", "%(revargs)", NULL };
	struct main_state *state = view->private;

	if (!(repo.is_inside_work_tree || *repo.worktree))
		return error("The stash view requires a working tree");

	state->with_graph = false;
	watch_register(&view->watch, WATCH_STASH);
	return begin_update(view, NULL, stash_argv, flags | OPEN_RELOAD);
}

static void
stash_select(struct view *view, struct line *line)
{
	struct main_state *state = view->private;

	main_select(view, line);
	assert(state->reflogs >= line->lineno);
	string_ncopy(view->env->stash, state->reflog[line->lineno - 1] + STRING_SIZE("refs/"),
		     strlen(state->reflog[line->lineno - 1]) - STRING_SIZE("refs/"));
	string_copy(view->ref, view->env->stash);
}

static enum request
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
					"--root", "--patch-with-stat",
					show_notes_arg(), diff_context_arg(),
					ignore_space_arg(), DIFF_ARGS,
					"--no-color", "%(stash)", NULL
			};

			if (!argv_format(diff_view.env, &diff_view.argv, diff_argv, false, false))
				report("Failed to format argument");
			else
				open_diff_view(view, flags | OPEN_PREPARED);
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
