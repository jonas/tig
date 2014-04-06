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

#include "tig/io.h"
#include "tig/options.h"
#include "tig/parse.h"
#include "tig/display.h"
#include "tig/view.h"
#include "tig/draw.h"
#include "tig/git.h"
#include "tig/main.h"

/*
 * Branch backend
 */

struct reference {
	const struct ident *author;	/* Author of the last commit. */
	struct time time;		/* Date of the last activity. */
	char title[128];		/* First line of the commit message. */
	const struct ref *ref;		/* Name and commit ID information. */
};

static const struct ref *refs_all;
#define REFS_ALL_NAME	"All references"
#define refs_is_all(reference) ((reference)->ref == refs_all)

static const enum view_column_type refs_columns[] = {
	VIEW_COLUMN_LINE_NUMBER,
	VIEW_COLUMN_DATE,
	VIEW_COLUMN_AUTHOR,
	VIEW_COLUMN_REF,
	VIEW_COLUMN_ID,
	VIEW_COLUMN_COMMIT_TITLE,
};

static bool
refs_get_column_data(struct view *view, const struct line *line, struct view_column_data *column_data)
{
	const struct reference *reference = line->data;

	column_data->author = reference->author;
	column_data->date = &reference->time;
	column_data->id = reference->ref->id;
	column_data->ref = reference->ref;
	column_data->commit_title = reference->title;

	return TRUE;
}

static enum request
refs_request(struct view *view, enum request request, struct line *line)
{
	struct reference *reference = line->data;

	switch (request) {
	case REQ_REFRESH:
		load_refs(TRUE);
		refresh_view(view);
		return REQ_NONE;

	case REQ_ENTER:
	{
		const struct ref *ref = reference->ref;
		const char *all_references_argv[] = {
			GIT_MAIN_LOG_CUSTOM(encoding_arg, commit_order_arg(), "",
				refs_is_all(reference) ? "--all" : ref->name, "")
		};

		open_argv(view, &main_view, all_references_argv, NULL, OPEN_SPLIT);
		return REQ_NONE;
	}
	case REQ_JUMP_COMMIT:
	{
		int lineno;

		for (lineno = 0; lineno < view->lines; lineno++) {
			struct reference *reference = view->line[lineno].data;

			if (!strncasecmp(reference->ref->id, view->env->search, strlen(view->env->search))) {
				select_view_line(view, lineno);
				report_clear();
				return REQ_NONE;
			}
		}
	}
	default:
		return request;
	}
}

static bool
refs_read(struct view *view, char *line)
{
	struct reference template = {};
	char *author;
	char *title;
	size_t i;

	if (!line)
		return TRUE;

	if (!*line)
		return FALSE;

	author = io_memchr(&view->io, line, 0);
	title = io_memchr(&view->io, author, 0);

	if (author)
		parse_author_line(author, &template.author, &template.time);

	for (i = 0; i < view->lines; i++) {
		struct reference *reference = view->line[i].data;

		if (strcmp(reference->ref->id, line))
			continue;

		reference->author = template.author;
		reference->time = template.time;

		if (title)
			string_expand(reference->title, sizeof(reference->title), title, 1);

		view->line[i].dirty = TRUE;
		view_column_info_update(view, &view->line[i]);
	}

	return TRUE;
}

static bool
refs_open_visitor(void *data, const struct ref *ref)
{
	struct view *view = data;
	struct reference *reference;
	bool is_all = ref == refs_all;
	struct line *line;

	line = add_line_alloc(view, &reference, LINE_DEFAULT, 0, is_all);
	if (!line)
		return FALSE;

	reference->ref = ref;
	view_column_info_update(view, line);

	return TRUE;
}

static bool
refs_open(struct view *view, enum open_flags flags)
{
	const char *refs_log[] = {
		"git", "log", encoding_arg, "--no-color", "--date=raw",
			"--pretty=format:%H%x00%an <%ae> %ad%x00%s",
			"--all", "--simplify-by-decoration", NULL
	};

	if (!refs_all) {
		struct ref *ref = calloc(1, sizeof(*refs_all) + strlen(REFS_ALL_NAME));

		if (ref) {
			strncpy(ref->name, REFS_ALL_NAME, strlen(REFS_ALL_NAME));
			refs_all = ref;
		}
	}

	if (!refs_all || !begin_update(view, NULL, refs_log, OPEN_RELOAD)) {
		report("Failed to load reference data");
		return FALSE;
	}

	refs_open_visitor(view, refs_all);
	foreach_ref(refs_open_visitor, view);

	return TRUE;
}

static void
refs_select(struct view *view, struct line *line)
{
	struct reference *reference = line->data;

	if (refs_is_all(reference)) {
		string_copy(view->ref, REFS_ALL_NAME);
		return;
	}
	string_copy_rev(view->ref, reference->ref->id);
	string_copy_rev(view->env->commit, reference->ref->id);
	string_copy_rev(view->env->head, reference->ref->id);
	string_copy_rev(view->env->ref, reference->ref->name);
}

static struct view_ops refs_ops = {
	"reference",
	argv_env.head,
	VIEW_REFRESH,
	0,
	refs_open,
	refs_read,
	view_column_draw,
	refs_request,
	view_column_grep,
	refs_select,
	NULL,
	refs_get_column_data,
	refs_columns,
	ARRAY_SIZE(refs_columns),
};

DEFINE_VIEW(refs);

/* vim: set ts=8 sw=8 noexpandtab: */
