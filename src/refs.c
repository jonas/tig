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
#define REFS_ALL_NAME "All references"
#define REFS_TAGS_NAME "All tags"
#define REFS_BRANCHES_NAME "All branches"
#define REFS_REMOTES_NAME "All remotes"
#define refs_is_all(reference) ((reference)->ref == refs_all)

enum refs_filter {
	REFS_FILTER_NONE     = 0,
	REFS_FILTER_TAGS     = 1 << 0,
	REFS_FILTER_BRANCHES = 1 << 1,
	REFS_FILTER_REMOTES  = 1 << 2,
} refs_filter = REFS_FILTER_NONE;

static bool
refs_get_column_data(struct view *view, const struct line *line, struct view_column_data *column_data)
{
	const struct reference *reference = line->data;

	column_data->author = reference->author;
	column_data->date = &reference->time;
	column_data->id = reference->ref->id;
	column_data->ref = reference->ref;
	column_data->commit_title = reference->title;

	return true;
}

static enum request
refs_request(struct view *view, enum request request, struct line *line)
{
	struct reference *reference = line->data;

	switch (request) {
	case REQ_REFRESH:
		load_refs(true);
		refresh_view(view);
		return REQ_NONE;

	case REQ_ENTER:
	{
		const struct ref *ref = reference->ref;
		const char *all_references_argv[] = {
			GIT_MAIN_LOG(encoding_arg, commit_order_arg(),
				"%(mainargs)", "",
				refs_is_all(reference) ? "--all" : ref->name, "",
				show_notes_arg(), log_custom_pretty_arg())
		};

		if (!argv_format(main_view.env, &main_view.argv, all_references_argv, false, false))
			report("Failed to format argument");
		else
			open_view(view, &main_view, OPEN_SPLIT | OPEN_PREPARED);
		return REQ_NONE;
	}
	default:
		return request;
	}
}

static bool
refs_read(struct view *view, struct buffer *buf, bool force_stop)
{
	struct reference template = {0};
	char *author;
	char *title;
	size_t i;

	if (!buf)
		return true;

	if (!*buf->data)
		return false;

	author = io_memchr(buf, buf->data, 0);
	title = io_memchr(buf, author, 0);

	if (author)
		parse_author_line(author, &template.author, &template.time);

	for (i = 0; i < view->lines; i++) {
		struct reference *reference = view->line[i].data;

		if (strcmp(reference->ref->id, buf->data))
			continue;

		reference->author = template.author;
		reference->time = template.time;

		if (title)
			string_expand(reference->title, sizeof(reference->title), title, strlen(title), 1);

		view->line[i].dirty = true;
		view_column_info_update(view, &view->line[i]);
	}

	return true;
}

static bool
refs_open_visitor(void *data, const struct ref *ref)
{
	struct view *view = data;
	struct reference *reference;
	bool is_all = ref == refs_all;
	struct line *line;

        if (!is_all)
		switch (refs_filter) {
		case REFS_FILTER_TAGS:
			if (ref->type != REFERENCE_TAG && ref->type != REFERENCE_LOCAL_TAG)
				return true;
			break;
		case REFS_FILTER_BRANCHES:
			if (ref->type != REFERENCE_BRANCH && ref->type != REFERENCE_HEAD)
				return true;
			break;
		case REFS_FILTER_REMOTES:
			if (ref->type != REFERENCE_TRACKED_REMOTE && ref->type != REFERENCE_REMOTE)
				return true;
			break;
		case REFS_FILTER_NONE:
		default:
			break;
		}

	line = add_line_alloc(view, &reference, LINE_DEFAULT, 0, is_all);
	if (!line)
		return false;

	reference->ref = ref;
	view_column_info_update(view, line);

	return true;
}

static const char **refs_argv;

static enum status_code
refs_open(struct view *view, enum open_flags flags)
{
	const char *refs_log[] = {
		"git", "log", encoding_arg, "--no-color", "--date=raw",
			opt_mailmap ? "--pretty=format:%H%x00%aN <%aE> %ad%x00%s"
				    : "--pretty=format:%H%x00%an <%ae> %ad%x00%s",
			"--all", "--simplify-by-decoration", NULL
	};
	enum status_code code;
	const char *name = REFS_ALL_NAME;
	int i;

	if (is_initial_view(view)) {
		refs_argv = opt_cmdline_args;
		opt_cmdline_args = NULL;
	}

	for (i = 0; refs_argv && refs_argv[i]; ++i) {
		if (!strncmp(refs_argv[i], "--tags", 6)) {
			refs_filter = REFS_FILTER_TAGS;
			name = REFS_TAGS_NAME;
		} else if (!strncmp(refs_argv[i], "--branches", 10)) {
			refs_filter = REFS_FILTER_BRANCHES;
			name = REFS_BRANCHES_NAME;
		} else if (!strncmp(refs_argv[i], "--remotes", 9)) {
			refs_filter = REFS_FILTER_REMOTES;
			name = REFS_REMOTES_NAME;
		}
	}

	if (!refs_all) {
		int name_length = strlen(name);
		struct ref *ref = calloc(1, sizeof(*refs_all) + name_length);

		if (!ref)
			return ERROR_OUT_OF_MEMORY;

		strncpy(ref->name, name, name_length);
		refs_all = ref;
	}

	code = begin_update(view, NULL, refs_log, OPEN_RELOAD);
	if (code != SUCCESS)
		return code;

	if (!view->lines)
		if (!(view->sort.current = get_view_column(view, VIEW_COLUMN_REF)))
			die("Failed to setup the refs view");
	refs_open_visitor(view, refs_all);
	foreach_ref(refs_open_visitor, view);
	resort_view(view, true);

	watch_register(&view->watch, WATCH_HEAD | WATCH_REFS);

	return SUCCESS;
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
	string_copy_rev(view->env->head, reference->ref->id);
	string_ncopy(view->env->ref, reference->ref->name, strlen(reference->ref->name));
	ref_update_env(view->env, reference->ref, false);
}

static struct view_ops refs_ops = {
	"reference",
	argv_env.head,
	VIEW_REFRESH | VIEW_SORTABLE,
	0,
	refs_open,
	refs_read,
	view_column_draw,
	refs_request,
	view_column_grep,
	refs_select,
	NULL,
	view_column_bit(AUTHOR) | view_column_bit(COMMIT_TITLE) |
		view_column_bit(DATE) | view_column_bit(ID) |
		view_column_bit(LINE_NUMBER) | view_column_bit(REF),
	refs_get_column_data,
};

DEFINE_VIEW(refs);

/* vim: set ts=8 sw=8 noexpandtab: */
