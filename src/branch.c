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

static const struct ref branch_all;
#define BRANCH_ALL_NAME	"All branches"
#define branch_is_all(branch) ((branch)->ref == &branch_all)

static const enum sort_field branch_sort_fields[] = {
	SORT_FIELD_NAME, SORT_FIELD_DATE, SORT_FIELD_AUTHOR
};

static struct sort_state branch_sort_state = SORT_STATE(branch_sort_fields);

struct branch_state {
	char id[SIZEOF_REV];
	size_t max_ref_length;
};

static int
branch_compare(const void *l1, const void *l2)
{
	const struct branch *branch1 = ((const struct line *) l1)->data;
	const struct branch *branch2 = ((const struct line *) l2)->data;

	if (branch_is_all(branch1))
		return -1;
	else if (branch_is_all(branch2))
		return 1;

	switch (get_sort_field(branch_sort_state)) {
	case SORT_FIELD_DATE:
		return sort_order(branch_sort_state, timecmp(&branch1->time, &branch2->time));

	case SORT_FIELD_AUTHOR:
		return sort_order(branch_sort_state, ident_compare(branch1->author, branch2->author));

	case SORT_FIELD_NAME:
	default:
		return sort_order(branch_sort_state, strcmp(branch1->ref->name, branch2->ref->name));
	}
}

static struct sortable branch_sortable = { &branch_sort_state, branch_compare };

static bool
branch_draw(struct view *view, struct line *line, unsigned int lineno)
{
	struct branch_state *state = view->private;
	struct branch *branch = line->data;
	enum line_type type = branch_is_all(branch) ? LINE_DEFAULT : get_line_type_from_ref(branch->ref);
	const char *branch_name = branch_is_all(branch) ? BRANCH_ALL_NAME : branch->ref->name;

	if (draw_lineno(view, lineno))
		return TRUE;

	if (draw_date(view, &branch->time))
		return TRUE;

	if (draw_author(view, branch->author))
		return TRUE;

	if (draw_field(view, type, branch_name, state->max_ref_length, ALIGN_LEFT, FALSE))
		return TRUE;

	if (draw_id(view, branch->ref->id))
		return TRUE;

	draw_text(view, LINE_DEFAULT, branch->title);
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
	struct branch_state *state = view->private;
	const char *title = NULL;
	const struct ident *author = NULL;
	struct time time = {};
	size_t i;

	if (!line)
		return TRUE;

	switch (get_line_type(line)) {
	case LINE_COMMIT:
		string_copy_rev_from_commit_line(state->id, line);
		return TRUE;

	case LINE_AUTHOR:
		parse_author_line(line + STRING_SIZE("author "), &author, &time);
		break;

	default:
		title = line + STRING_SIZE("title ");
	}

	for (i = 0; i < view->lines; i++) {
		struct branch *branch = view->line[i].data;

		if (strcmp(branch->ref->id, state->id))
			continue;

		if (author) {
			branch->author = author;
			branch->time = time;
		}

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
	struct branch_state *state = view->private;
	struct branch *branch;
	bool is_all = ref == &branch_all;
	size_t ref_length;

	if (ref->tag || ref->ltag)
		return TRUE;

	if (!add_line_alloc(view, &branch, LINE_DEFAULT, 0, is_all))
		return FALSE;

	ref_length = is_all ? STRING_SIZE(BRANCH_ALL_NAME) : strlen(ref->name);
	if (ref_length > state->max_ref_length)
		state->max_ref_length = ref_length;

	branch->ref = ref;
	return TRUE;
}

static bool
branch_open(struct view *view, enum open_flags flags)
{
	const char *branch_log[] = {
		"git", "log", encoding_arg, "--no-color", "--date=raw",
			"--pretty=format:commit %H%nauthor %an <%ae> %ad%ntitle %s",
			"--all", "--simplify-by-decoration", NULL
	};

	if (!begin_update(view, NULL, branch_log, OPEN_RELOAD)) {
		report("Failed to load branch data");
		return FALSE;
	}

	branch_open_visitor(view, &branch_all);
	foreach_ref(branch_open_visitor, view);

	return TRUE;
}

static bool
branch_grep(struct view *view, struct line *line)
{
	struct branch *branch = line->data;
	const char *text[] = {
		branch->ref->name,
		mkauthor(branch->author, opt_author_width, opt_show_author),
		NULL
	};

	return grep_text(view, text);
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
	sizeof(struct branch_state),
	branch_open,
	branch_read,
	branch_draw,
	branch_request,
	branch_grep,
	branch_select,
	NULL,
	&branch_sortable,
};

DEFINE_VIEW(branch);

/* vim: set ts=8 sw=8 noexpandtab: */
