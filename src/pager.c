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

#include "tig/tig.h"
#include "tig/pager.h"
#include "tig/options.h"
#include "tig/request.h"
#include "tig/line.h"
#include "tig/keys.h"
#include "tig/display.h"
#include "tig/view.h"
#include "tig/draw.h"
#include "tig/diff.h"

/*
 * Pager backend
 */

bool
pager_get_column_data(struct view *view, const struct line *line, struct view_column_data *column_data)
{
	column_data->text = box_text(line);
	column_data->box = line->data;
	return true;
}

static void
add_pager_refs(struct view *view, const char *commit_id)
{
	char buf[SIZEOF_STR];
	const struct ref *list;
	size_t bufpos = 0;
	const char *sep = "Refs: ";

	list = get_ref_list(commit_id);
	if (!list) {
		if (view_has_flags(view, VIEW_ADD_DESCRIBE_REF) && refs_contain_tag())
			add_line_text(view, sep, LINE_PP_REFS);
		return;
	}

	for (; list; list = list->next) {
		const struct ref *ref = list;
		const struct ref_format *fmt = get_ref_format(opt_reference_format, ref);

		if (!string_format_from(buf, &bufpos, "%s%s%s%s", sep,
					fmt->start, ref->name, fmt->end))
			return;
		sep = ", ";
	}

	if (bufpos == 0)
		return;

	add_line_text(view, buf, LINE_PP_REFS);
}

static struct line *
pager_wrap_line(struct view *view, const char *data, enum line_type type)
{
	size_t first_line = 0;
	bool has_first_line = false;
	size_t datalen = strlen(data);
	size_t lineno = 0;

	while (datalen > 0 || !has_first_line) {
		int width;
		int trimmed;
		bool wrapped = !!first_line;
		size_t linelen = utf8_length(&data, datalen, 0, &width, view->width, &trimmed, wrapped, opt_tab_size);
		struct line *line;

		line = add_line_text_at_(view, view->lines, data, linelen, type, 1, wrapped);
		if (!line)
			break;
		if (!has_first_line) {
			first_line = view->lines - 1;
			has_first_line = true;
		}

		if (!wrapped)
			lineno = line->lineno;

		line->wrapped = wrapped;
		line->lineno = lineno;

		datalen -= linelen;
		data += linelen;
	}

	return has_first_line ? &view->line[first_line] : NULL;
}

bool
pager_common_read(struct view *view, const char *data, enum line_type type, struct line **line_ptr)
{
	struct line *line;

	if (!data)
		return true;

	if (opt_wrap_lines) {
		line = pager_wrap_line(view, data, type);
	} else {
		line = add_line_text(view, data, type);
	}

	if (!line)
		return false;

	if (line_ptr)
		*line_ptr = line;

	if (line->type == LINE_COMMIT && view_has_flags(view, VIEW_ADD_PAGER_REFS)) {
		data += STRING_SIZE("commit ");
		while (*data && !isalnum(*data))
			data++;
		add_pager_refs(view, data);
	}

	return true;
}

bool
pager_read(struct view *view, struct buffer *buf, bool force_stop)
{
	if (!buf) {
		if (!diff_done_highlight(view->private)) {
			report("Failed run the diff-highlight program: %s", opt_diff_highlight);
			return false;
		}

		return true;
	}

	return diff_common_read(view, buf->data, view->private);
}

enum request
pager_request(struct view *view, enum request request, struct line *line)
{
	enum open_flags flags = view_is_displayed(view) ? OPEN_SPLIT : OPEN_DEFAULT;
	int split = 0;

	if (request != REQ_ENTER)
		return request;

	if (line->type == LINE_COMMIT && view_has_flags(view, VIEW_OPEN_DIFF)) {
		open_diff_view(view, flags);
		split = 1;
	}

	/* Always scroll the view even if it was split. That way
	 * you can use Enter to scroll through the log view and
	 * split open each commit diff. */
	if (view == display[current_view])
		scroll_view(view, REQ_SCROLL_LINE_DOWN);

	/* FIXME: A minor workaround. Scrolling the view will call report_clear()
	 * but if we are scrolling a non-current view this won't properly
	 * update the view title. */
	if (split)
		update_view_title(view);

	return REQ_NONE;
}

void
pager_select(struct view *view, struct line *line)
{
	const char *text = box_text(line);

	string_ncopy(view->env->text, text, strlen(text));

	if (line->type == LINE_COMMIT) {
		string_copy_rev_from_commit_line(view->env->commit, text);
		if (!view_has_flags(view, VIEW_NO_REF))
			string_copy_rev(view->ref, view->env->commit);
	}
}

static enum status_code
pager_open(struct view *view, enum open_flags flags)
{
	enum status_code code;

	if (!open_from_stdin(flags) && !view->lines && !(flags & OPEN_PREPARED))
		return error("No pager content, press %s to run command from prompt",
			     get_view_key(view, REQ_PROMPT));

	code = begin_update(view, NULL, NULL, flags);
	if (code != SUCCESS)
		return code;

	return diff_init_highlight(view, view->private);
}

static struct view_ops pager_ops = {
	"line",
	"",
	VIEW_OPEN_DIFF | VIEW_NO_REF | VIEW_NO_GIT_DIR,
	sizeof(struct diff_state),
	pager_open,
	pager_read,
	view_column_draw,
	pager_request,
	view_column_grep,
	pager_select,
	NULL,
	view_column_bit(LINE_NUMBER) | view_column_bit(TEXT),
	pager_get_column_data,
};

DEFINE_VIEW(pager);

/* vim: set ts=8 sw=8 noexpandtab: */
