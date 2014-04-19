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
#include "tig/display.h"
#include "tig/draw.h"
#include "tig/log.h"
#include "tig/diff.h"
#include "tig/pager.h"

struct log_state {
	/* Used for tracking when we need to recalculate the previous
	 * commit, for example when the user scrolls up or uses the page
	 * up/down in the log view. */
	int last_lineno;
	size_t graph_indent;
	enum line_type last_type;
	bool commit_title_read;
	bool after_commit_header;
	bool reading_diff_stat;
};

static inline void
log_copy_rev(struct view *view, struct line *line)
{
	const char *text = line->data;
	size_t offset = get_graph_indent(text);

	string_copy_rev_from_commit_line(view->ref, text + offset);
}

static void
log_select(struct view *view, struct line *line)
{
	struct log_state *state = view->private;
	int last_lineno = state->last_lineno;

	if (!last_lineno || abs(last_lineno - line->lineno) > 1
	    || (state->last_type == LINE_COMMIT && last_lineno > line->lineno)) {
		struct line *commit_line = find_prev_line_by_type(view, line, LINE_COMMIT);

		if (commit_line)
			log_copy_rev(view, commit_line);
	}

	if (line->type == LINE_COMMIT && !view_has_flags(view, VIEW_NO_REF))
		log_copy_rev(view, line);
	string_copy_rev(view->env->commit, view->ref);
	state->last_lineno = line->lineno;
	state->last_type = line->type;
}

static bool
log_open(struct view *view, enum open_flags flags)
{
	const char *log_argv[] = {
		"git", "log", encoding_arg, commit_order_arg(), "--cc",
			"--stat", "%(cmdlineargs)", "%(revargs)", "--no-color",
			"--", "%(fileargs)", NULL
	};

	if (!pager_column_init(view))
		return FALSE;
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

static bool
log_read(struct view *view, char *data)
{
	struct line *line = NULL;
	enum line_type type;
	struct log_state *state = view->private;
	size_t len;
	char *commit;

	if (!data)
		return TRUE;

	commit = strstr(data, "commit ");
	if (commit && get_graph_indent(data) == commit - data)
		state->graph_indent = commit - data;

	type = get_line_type(data + state->graph_indent);
	len = strlen(data + state->graph_indent);

	if (type == LINE_COMMIT)
		state->commit_title_read = TRUE;
	else if (state->commit_title_read && len < 1) {
		state->commit_title_read = FALSE;
		state->after_commit_header = TRUE;
	} else if (state->after_commit_header && len < 1) {
		state->after_commit_header = FALSE;
		state->reading_diff_stat = TRUE;
	} else if (state->reading_diff_stat) {
		line = diff_common_add_diff_stat(view, data, state->graph_indent);
		if (line) {
			if (state->graph_indent)
				line->graph_indent = 1;
			return TRUE;
		}
		state->reading_diff_stat = FALSE;
	}

	if (!pager_common_read(view, data, type, &line))
		return FALSE;
	if (line && state->graph_indent)
		line->graph_indent = 1;
	return TRUE;
}

static struct view_ops log_ops = {
	"line",
	argv_env.head,
	VIEW_ADD_PAGER_REFS | VIEW_OPEN_DIFF | VIEW_SEND_CHILD_ENTER | VIEW_LOG_LIKE | VIEW_REFRESH,
	sizeof(struct log_state),
	log_open,
	log_read,
	view_column_draw,
	log_request,
	view_column_grep,
	log_select,
	NULL,
	pager_get_column_data,
};

DEFINE_VIEW(log);

/* vim: set ts=8 sw=8 noexpandtab: */
