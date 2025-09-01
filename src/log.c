/* Copyright (c) 2006-2025 Jonas Fonseca <jonas.fonseca@gmail.com>
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
	struct option_common optcom;
	enum line_type last_type;
	bool commit_title_read;
	bool after_commit_header;
	bool reading_diff_stat;
	bool external_format;
};

static inline void
log_copy_rev(struct view *view, struct line *line)
{
	const char *text = box_text(line);
	size_t offset = get_graph_indent(text);

	string_copy_rev_from_commit_line(view->ref, text + offset);
	view->env->blob[0] = 0;
}

static void
log_select(struct view *view, struct line *line)
{
	struct log_state *state = view->private;
	int last_lineno = state->last_lineno;
	const char *text = box_text(line);

	if (!last_lineno || abs(last_lineno - line->lineno) > 1
	    || (state->last_type == LINE_COMMIT && last_lineno > line->lineno)) {
		struct line *commit_line = find_prev_line_by_type(view, line, LINE_COMMIT);

		if (commit_line)
			log_copy_rev(view, commit_line);
	}

	if (line->type == LINE_COMMIT && !view_has_flags(view, VIEW_NO_REF))
		log_copy_rev(view, line);
	string_copy_rev(view->env->commit, view->ref);
	string_ncopy(view->env->text, text, strlen(text));
	state->last_lineno = line->lineno;
	state->last_type = line->type;
}

static bool
log_check_external_formatter()
{
	/* check if any formatter arugments in "%(logargs)", "%(cmdlineargs)" */
	const char ** opt_list[] = {
		opt_log_options,
		opt_cmdline_args,
	};
	for (int i=0; i<ARRAY_SIZE(opt_list); i++) {
		if (opt_list[i] &&
			(argv_containsn(opt_list[i], "--pretty", STRING_SIZE("--pretty")) ||
				argv_containsn(opt_list[i], "--format", STRING_SIZE("--format"))))
			return true;
	}
	return false;
}

static enum status_code
log_open(struct view *view, enum open_flags flags)
{
	struct log_state *state = view->private;
	bool external_format = log_check_external_formatter();
	const char *log_argv[] = {
		"git", "log", encoding_arg, commit_order_arg(),
			use_mailmap_arg(), "%(logargs)", "%(cmdlineargs)",
			"%(revargs)", "--no-color",
			external_format ? "" : "--pretty=fuller",
			"--", "%(fileargs)", NULL
	};
	enum status_code code;

	read_option_common(view, &state->optcom);
	state->external_format = external_format;
	code = begin_update(view, NULL, log_argv, flags | OPEN_WITH_STDERR);
	if (code != SUCCESS)
		return code;

	watch_register(&view->watch, WATCH_HEAD | WATCH_REFS);

	return SUCCESS;
}

static enum request
log_request(struct view *view, enum request request, struct line *line)
{
	enum open_flags flags = view_is_displayed(view) ? OPEN_SPLIT : OPEN_DEFAULT;

	switch (request) {
	case REQ_REFRESH:
		load_refs(true);
		refresh_view(view);
		return REQ_NONE;

	case REQ_EDIT:
		return diff_common_edit(view, request, line);

	case REQ_ENTER:
		if (!display[1] || strcmp(display[1]->vid, view->ref))
			open_diff_view(view, flags);
		return REQ_NONE;

	default:
		return request;
	}
}

static bool
log_read(struct view *view, struct buffer *buf, bool force_stop)
{
	struct line *line = NULL;
	enum line_type type = LINE_DEFAULT;
	struct log_state *state = view->private;
	size_t len;
	char *commit;
	char *data;
	bool swap_lines = false;

	if (!buf)
		return true;

	data = buf->data;
	commit = strstr(data, "commit ");
	if (commit && get_graph_indent(data) == commit - data)
		state->graph_indent = commit - data;

	len = strlen(data);
	if (len >= state->graph_indent) {
		type = get_line_type(data + state->graph_indent);
		len -= state->graph_indent;
	}

	if (type == LINE_COMMIT)
		state->commit_title_read = true;
	else if (state->commit_title_read && len < 1) {
		state->commit_title_read = false;
		state->after_commit_header = true;
	} else if ((state->after_commit_header && len < 1) || type == LINE_DIFF_START) {
		state->after_commit_header = false;
		state->reading_diff_stat = true;
	} else if (state->reading_diff_stat) {
		line = diff_common_add_diff_stat(view, data, state->graph_indent);
		if (line) {
			if (state->graph_indent)
				line->graph_indent = 1;
			return true;
		}
		state->reading_diff_stat = false;
	}

	if (!state->external_format) {
		switch (type)
		{
		case LINE_PP_AUTHOR:
			if (state->optcom.author_as_committer)
				return true;
			break;
		case LINE_PP_COMMITTER:
			if (!state->optcom.author_as_committer)
				return true;
			swap_lines = state->optcom.use_author_date;
			break;
		case LINE_PP_AUTHORDATE:
		case LINE_PP_DATE:
			if (!state->optcom.use_author_date)
				return true;
			break;
		case LINE_PP_COMMITDATE:
			if (state->optcom.use_author_date)
				return true;
			break;
		default:
			break;
		}
		/* remove 4 spaces after Commit:/Author:, or
		 * convert CommitDate:/AuthorDate: to Date: */
		switch (type)
		{
		case LINE_PP_AUTHOR:
		case LINE_PP_COMMITTER:
			{
				char *p = strchr(data, ':');
				if (p && p[5]==' ')
					memmove(p+1, p+5, strlen(p+5)+1);
				break;
			}
		case LINE_PP_AUTHORDATE:
		case LINE_PP_COMMITDATE:
			{
				char *p = strchr(data, ':');
				if (p && p[1]==' ' && (p - data) >= 10) {
					memcpy(p - 10, "Date:   ", STRING_SIZE("Date:   "));
					memmove(p - 10 + STRING_SIZE("Date:   "), p+2, strlen(p+2)+1);
				}
				break;
			}
		default:
			break;
		}
	}

	if (!pager_common_read(view, data, type, &line))
		return false;
	if (line && state->graph_indent)
		line->graph_indent = 1;
	if (swap_lines && view->lines >= 2) {
		size_t last_idx = view->lines - 1;
		struct line *line1 = &view->line[last_idx];
		struct line *line2 = &view->line[last_idx - 1];
		struct line buf = *line1;
		*line1 = *line2;
		*line2 = buf;
		line1->lineno--;
		line2->lineno++;
	}
	return true;
}

static struct view_ops log_ops = {
	"line",
	argv_env.head,
	VIEW_ADD_PAGER_REFS | VIEW_OPEN_DIFF | VIEW_SEND_CHILD_ENTER | VIEW_LOG_LIKE | VIEW_REFRESH | VIEW_FLEX_WIDTH,
	sizeof(struct log_state),
	log_open,
	log_read,
	view_column_draw,
	log_request,
	view_column_grep,
	log_select,
	NULL,
	view_column_bit(LINE_NUMBER) | view_column_bit(TEXT),
	pager_get_column_data,
};

DEFINE_VIEW(log);

/* vim: set ts=8 sw=8 noexpandtab: */
