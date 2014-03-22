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
#include "tig/display.h"
#include "tig/log.h"
#include "tig/diff.h"
#include "tig/pager.h"

struct log_state {
	/* Used for tracking when we need to recalculate the previous
	 * commit, for example when the user scrolls up or uses the page
	 * up/down in the log view. */
	int last_lineno;
	enum line_type last_type;
};

static void
log_select(struct view *view, struct line *line)
{
	struct log_state *state = view->private;
	int last_lineno = state->last_lineno;

	if (!last_lineno || abs(last_lineno - line->lineno) > 1
	    || (state->last_type == LINE_COMMIT && last_lineno > line->lineno)) {
		const struct line *commit_line = find_prev_line_by_type(view, line, LINE_COMMIT);

		if (commit_line)
			string_copy_rev_from_commit_line(view->ref, commit_line->data);
	}

	if (line->type == LINE_COMMIT && !view_has_flags(view, VIEW_NO_REF)) {
		string_copy_rev_from_commit_line(view->ref, (char *)line->data);
	}
	string_copy_rev(view->env->commit, view->ref);
	state->last_lineno = line->lineno;
	state->last_type = line->type;
}

static bool
log_open(struct view *view, enum open_flags flags)
{
	static const char *log_argv[] = {
		"git", "log", encoding_arg, "--no-color", "--cc", "--stat", "-n100", "%(head)", "--", NULL
	};

	return begin_update(view, NULL, log_argv, flags);
}

static enum request
log_request(struct view *view, enum request request, struct line *line)
{
	switch (request) {
	case REQ_REFRESH:
		load_refs(TRUE);
		refresh_view(view);
		return REQ_NONE;

	case REQ_ENTER:
		if (!display[1] || strcmp(display[1]->vid, view->ref))
			open_diff_view(view, OPEN_SPLIT);
		return REQ_NONE;

	default:
		return request;
	}
}

static struct view_ops log_ops = {
	"line",
	{ "log" },
	argv_env.head,
	VIEW_ADD_PAGER_REFS | VIEW_OPEN_DIFF | VIEW_SEND_CHILD_ENTER | VIEW_LOG_LIKE | VIEW_REFRESH,
	sizeof(struct log_state),
	log_open,
	pager_read,
	pager_draw,
	log_request,
	pager_grep,
	log_select,
};

DEFINE_VIEW(log);

/* vim: set ts=8 sw=8 noexpandtab: */
