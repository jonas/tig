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

#include "tig/tig.h"
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
pager_draw(struct view *view, struct line *line, unsigned int lineno)
{
	if (draw_lineno(view, lineno))
		return TRUE;

	if (line->wrapped && draw_text(view, LINE_DELIMITER, "+"))
		return TRUE;

	draw_text(view, line->type, line->data);
	return TRUE;
}

static bool
add_describe_ref(char *buf, size_t *bufpos, const char *commit_id, const char *sep)
{
	const char *describe_argv[] = { "git", "describe", commit_id, NULL };
	char ref[SIZEOF_STR];

	if (!io_run_buf(describe_argv, ref, sizeof(ref)) || !*ref)
		return TRUE;

	/* This is the only fatal call, since it can "corrupt" the buffer. */
	if (!string_nformat(buf, SIZEOF_STR, bufpos, "%s%s", sep, ref))
		return FALSE;

	return TRUE;
}

static void
add_pager_refs(struct view *view, const char *commit_id)
{
	char buf[SIZEOF_STR];
	struct ref_list *list;
	size_t bufpos = 0, i;
	const char *sep = "Refs: ";
	bool is_tag = FALSE;

	list = get_ref_list(commit_id);
	if (!list) {
		if (view_has_flags(view, VIEW_ADD_DESCRIBE_REF))
			goto try_add_describe_ref;
		return;
	}

	for (i = 0; i < list->size; i++) {
		struct ref *ref = list->refs[i];
		const char *fmt = ref->tag    ? "%s[%s]" :
		                  ref->remote ? "%s<%s>" : "%s%s";

		if (!string_format_from(buf, &bufpos, fmt, sep, ref->name))
			return;
		sep = ", ";
		if (ref->tag)
			is_tag = TRUE;
	}

	if (!is_tag && view_has_flags(view, VIEW_ADD_DESCRIBE_REF)) {
try_add_describe_ref:
		/* Add <tag>-g<commit_id> "fake" reference. */
		if (!add_describe_ref(buf, &bufpos, commit_id, sep))
			return;
	}

	if (bufpos == 0)
		return;

	add_line_text(view, buf, LINE_PP_REFS);
}

static struct line *
pager_wrap_line(struct view *view, const char *data, enum line_type type)
{
	size_t first_line = 0;
	bool has_first_line = FALSE;
	size_t datalen = strlen(data);
	size_t lineno = 0;

	while (datalen > 0 || !has_first_line) {
		bool wrapped = !!first_line;
		size_t linelen = string_expanded_length(data, datalen, opt_tab_size, view->width - !!wrapped);
		struct line *line;
		char *text;

		line = add_line(view, NULL, type, linelen + 1, wrapped);
		if (!line)
			break;
		if (!has_first_line) {
			first_line = view->lines - 1;
			has_first_line = TRUE;
		}

		if (!wrapped)
			lineno = line->lineno;

		line->wrapped = wrapped;
		line->lineno = lineno;
		text = line->data;
		if (linelen)
			strncpy(text, data, linelen);
		text[linelen] = 0;

		datalen -= linelen;
		data += linelen;
	}

	return has_first_line ? &view->line[first_line] : NULL;
}

bool
pager_common_read(struct view *view, const char *data, enum line_type type)
{
	struct line *line;

	if (!data)
		return TRUE;

	if (opt_wrap_lines) {
		line = pager_wrap_line(view, data, type);
	} else {
		line = add_line_text(view, data, type);
	}

	if (!line)
		return FALSE;

	if (line->type == LINE_COMMIT && view_has_flags(view, VIEW_ADD_PAGER_REFS))
		add_pager_refs(view, data + STRING_SIZE("commit "));

	return TRUE;
}

bool
pager_read(struct view *view, char *data)
{
	if (!data)
		return TRUE;

	return pager_common_read(view, data, get_line_type(data));
}

enum request
pager_request(struct view *view, enum request request, struct line *line)
{
	int split = 0;

	if (request != REQ_ENTER)
		return request;

	if (line->type == LINE_COMMIT && view_has_flags(view, VIEW_OPEN_DIFF)) {
		open_diff_view(view, OPEN_SPLIT);
		split = 1;
	}

	/* Always scroll the view even if it was split. That way
	 * you can use Enter to scroll through the log view and
	 * split open each commit diff. */
	scroll_view(view, REQ_SCROLL_LINE_DOWN);

	/* FIXME: A minor workaround. Scrolling the view will call report_clear()
	 * but if we are scrolling a non-current view this won't properly
	 * update the view title. */
	if (split)
		update_view_title(view);

	return REQ_NONE;
}

bool
pager_grep(struct view *view, struct line *line)
{
	const char *text[] = { line->data, NULL };

	return grep_text(view, text);
}

void
pager_select(struct view *view, struct line *line)
{
	if (line->type == LINE_COMMIT) {
		string_copy_rev_from_commit_line(view->env->commit, line->data);
		if (!view_has_flags(view, VIEW_NO_REF))
			string_copy_rev(view->ref, view->env->commit);
	}
}

bool
pager_open(struct view *view, enum open_flags flags)
{
	if (!open_from_stdin(flags) && !view->lines && !(flags & OPEN_PREPARED)) {
		report("No pager content, press %s to run command from prompt",
			get_view_key(view, REQ_PROMPT));
		return FALSE;
	}

	return begin_update(view, NULL, NULL, flags);
}

static struct view_ops pager_ops = {
	"line",
	"",
	VIEW_OPEN_DIFF | VIEW_NO_REF | VIEW_NO_GIT_DIR,
	0,
	pager_open,
	pager_read,
	pager_draw,
	pager_request,
	pager_grep,
	pager_select,
};

DEFINE_VIEW(pager);

/* vim: set ts=8 sw=8 noexpandtab: */
