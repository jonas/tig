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

struct branch {
	const struct ident *author;	/* Author of the last commit. */
	struct time time;		/* Date of the last activity. */
	char title[128];		/* First line of the commit message. */
	const struct ref *ref;		/* Name and commit ID information. */
};

static const struct ref *branch_all;
#define BRANCH_ALL_NAME	"All branches"
#define branch_is_all(branch) ((branch)->ref == branch_all)

static const enum view_column branch_columns[] = {
	VIEW_COLUMN_DATE,
	VIEW_COLUMN_AUTHOR,
	VIEW_COLUMN_REF,
	VIEW_COLUMN_ID,
	VIEW_COLUMN_COMMIT_TITLE,
};

static bool
branch_get_columns(struct view *view, const struct line *line, struct view_columns *columns)
{
	const struct branch *branch = line->data;

	columns->author = branch->author;
	columns->date = &branch->time;
	columns->id = branch->ref->id;
	columns->ref = branch->ref;
	columns->commit_title = branch->title;

	return TRUE;
}

static enum request
branch_request(struct view *view, enum request request, struct line *line)
{
	struct branch *branch = line->data;

	switch (request) {
	case REQ_REFRESH:
		load_refs(TRUE);
		refresh_view(view);
		return REQ_NONE;

	case REQ_ENTER:
	{
		const struct ref *ref = branch->ref;
		const char *all_branches_argv[] = {
			GIT_MAIN_LOG(encoding_arg, commit_order_arg(), "", branch_is_all(branch) ? "--all" : ref->name, "")
		};

		open_argv(view, &main_view, all_branches_argv, NULL, OPEN_SPLIT);
		return REQ_NONE;
	}
	case REQ_JUMP_COMMIT:
	{
		int lineno;

		for (lineno = 0; lineno < view->lines; lineno++) {
			struct branch *branch = view->line[lineno].data;

			if (!strncasecmp(branch->ref->id, view->env->search, strlen(view->env->search))) {
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
branch_read(struct view *view, char *line)
{
	struct branch template = {};
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
		struct branch *branch = view->line[i].data;

		if (strcmp(branch->ref->id, line))
			continue;

		branch->author = template.author;
		branch->time = template.time;

		if (title)
			string_expand(branch->title, sizeof(branch->title), title, 1);

		view->line[i].dirty = TRUE;
	}

	return TRUE;
}

static bool
branch_open_visitor(void *data, const struct ref *ref)
{
	struct view *view = data;
	struct branch *branch;
	bool is_all = ref == branch_all;
	struct line *line;

	if (ref->tag || ref->ltag)
		return TRUE;

	line = add_line_alloc(view, &branch, LINE_DEFAULT, 0, is_all);
	if (!line)
		return FALSE;

	branch->ref = ref;
	view_columns_info_update(view, line);

	return TRUE;
}

static bool
branch_open(struct view *view, enum open_flags flags)
{
	const char *branch_log[] = {
		"git", "log", encoding_arg, "--no-color", "--date=raw",
			"--pretty=format:%H%x00%an <%ae> %ad%x00%s",
			"--all", "--simplify-by-decoration", NULL
	};

	if (!branch_all) {
		struct ref *ref = calloc(1, sizeof(*branch_all) + strlen(BRANCH_ALL_NAME));

		if (ref) {
			strncpy(ref->name, BRANCH_ALL_NAME, strlen(BRANCH_ALL_NAME));
			branch_all = ref;
		}
	}

	if (!branch_all || !begin_update(view, NULL, branch_log, OPEN_RELOAD)) {
		report("Failed to load branch data");
		return FALSE;
	}

	branch_open_visitor(view, branch_all);
	foreach_ref(branch_open_visitor, view);

	return TRUE;
}

static void
branch_select(struct view *view, struct line *line)
{
	struct branch *branch = line->data;

	if (branch_is_all(branch)) {
		string_copy(view->ref, BRANCH_ALL_NAME);
		return;
	}
	string_copy_rev(view->ref, branch->ref->id);
	string_copy_rev(view->env->commit, branch->ref->id);
	string_copy_rev(view->env->head, branch->ref->id);
	string_copy_rev(view->env->branch, branch->ref->name);
}

static struct view_ops branch_ops = {
	"branch",
	argv_env.head,
	VIEW_REFRESH,
	0,
	branch_open,
	branch_read,
	view_columns_draw,
	branch_request,
	view_columns_grep,
	branch_select,
	NULL,
	branch_get_columns,
	branch_columns,
	ARRAY_SIZE(branch_columns),
};

DEFINE_VIEW(branch);

/* vim: set ts=8 sw=8 noexpandtab: */
