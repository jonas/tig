/* Copyright (c) 2006-2026 Jonas Fonseca <jonas.fonseca@gmail.com>
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
	struct time author_time;	/* Date of the last activity. */
	const struct ident *committer;	/* Last committer. */
	struct time commit_time;	/* Date of the last activity. */
	char title[128];		/* First line of the commit message. */
	const struct ref *ref;		/* Name and commit ID information. */
};

struct refs_state {
	bool keep_lineno;		/* Keep the cursor on the line it was opened at? */
	unsigned long lineno;		/* The line the cursor was opened at. */
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
	REFS_FILTER_ALL	     = 1 << 3,
} refs_filter = REFS_FILTER_NONE;

static bool
refs_get_column_data(struct view *view, const struct line *line, struct view_column_data *column_data)
{
	const struct reference *reference = line->data;
	struct view_column *column = get_view_column(view, VIEW_COLUMN_DATE);
	bool use_author_date = column && column->opt.date.use_author;

	column_data->author = reference->author;
	column_data->committer = reference->committer;
	column_data->date = use_author_date
				? &reference->author_time
				: &reference->commit_time;
	column_data->id = reference->ref->id;
	column_data->ref = reference->ref;
	column_data->commit_title = reference->title;

	return true;
}

/* The order in which the refs are grouped when sorting by the ref column. */
enum refs_group {
	REFS_GROUP_ALL,
	REFS_GROUP_HEAD,
	REFS_GROUP_MASTER,
	REFS_GROUP_MAIN,
	REFS_GROUP_BRANCH,
	REFS_GROUP_REMOTE,
	REFS_GROUP_OTHER,
};

static enum refs_group
refs_get_group(const struct ref *ref)
{
	if (ref == refs_all)
		return REFS_GROUP_ALL;

	if (ref_is_remote(ref))
		return REFS_GROUP_REMOTE;

	if (ref->type == REFERENCE_HEAD)
		return REFS_GROUP_HEAD;

	if (ref->type != REFERENCE_BRANCH)
		return REFS_GROUP_OTHER;

	if (!strcmp(ref->name, "master"))
		return REFS_GROUP_MASTER;

	if (!strcmp(ref->name, "main"))
		return REFS_GROUP_MAIN;

	return REFS_GROUP_BRANCH;
}

static bool
refs_compare(struct view *view, enum view_column_type column,
	     const struct line *line1, const struct line *line2, int *cmp)
{
	const struct reference *reference1 = line1->data;
	const struct reference *reference2 = line2->data;
	enum refs_group group1, group2;

	if (column != VIEW_COLUMN_REF)
		return false;

	group1 = refs_get_group(reference1->ref);
	group2 = refs_get_group(reference2->ref);
	*cmp = group1 - group2;

	/* List the branches with the most recent commits first. This uses the
	 * commit date regardless of which date the date column displays. */
	if (!*cmp && group1 == REFS_GROUP_BRANCH)
		*cmp = timecmp(&reference2->commit_time, &reference1->commit_time);

	if (!*cmp)
		*cmp = group1 == REFS_GROUP_REMOTE
			? strcmp(reference1->ref->name, reference2->ref->name)
			: ref_compare(reference1->ref, reference2->ref);

	return true;
}

static const struct ref *
refs_get_selected(struct view *view)
{
	const struct reference *reference;

	if (view->pos.lineno >= view->lines)
		return NULL;

	reference = view->line[view->pos.lineno].data;
	return reference->ref;
}

/* Sort the lines while keeping the cursor on the selected reference. */
static void
refs_resort(struct view *view, const struct ref *selected)
{
	size_t i;

	resort_view(view, true);

	for (i = 0; i < view->lines; i++) {
		struct reference *reference = view->line[i].data;

		view->line[i].dirty = view->line[i].cleareol = 1;
		if (reference->ref == selected)
			goto_view_line(view, view->pos.offset, i);
	}
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
				refs_is_all(reference) ? "--all" : ref->id, "",
				show_notes_arg(), log_custom_pretty_arg())
		};
		enum open_flags flags = view_is_displayed(view) ? OPEN_SPLIT : OPEN_DEFAULT;

		if (!argv_format(main_view.env, &main_view.argv, all_references_argv, 0))
			report("Failed to format argument");
		else
			open_main_view(view, flags | OPEN_PREPARED);
		return REQ_NONE;
	}
	default:
		return request;
	}
}

static bool
refs_read(struct view *view, struct buffer *buf, bool force_stop)
{
	struct refs_state *state = view->private;
	struct reference template = {0};
	char *author;
	char *committer;
	char *title;
	size_t i;

	if (!buf) {
		/* The commit dates used for ordering the branches are only
		 * known once everything has been read. Nothing is redrawn
		 * when loading is stopped, so leave the lines as displayed. */
		if (force_stop)
			return true;

		/* Follow the selected reference if the cursor was moved. */
		if (state->keep_lineno && view->pos.lineno == state->lineno)
			refs_resort(view, NULL);
		else
			refs_resort(view, refs_get_selected(view));
		return true;
	}

	if (!*buf->data)
		return false;

	author = io_memchr(buf, buf->data, 0);
	committer = io_memchr(buf, author, 0);
	title = io_memchr(buf, committer, 0);

	if (author)
		parse_author_line(author, &template.author, &template.author_time);

	if (committer)
		parse_author_line(committer, &template.committer, &template.commit_time);

	for (i = 0; i < view->lines; i++) {
		struct reference *reference = view->line[i].data;

		if (strcmp(reference->ref->id, buf->data))
			continue;

		reference->author = template.author;
		reference->author_time = template.author_time;
		reference->committer = template.committer;
		reference->commit_time = template.commit_time;

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
	const struct ref_format *fmt = get_ref_format(opt_reference_format, ref);
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
			if (ref->type == REFERENCE_STASH ||
			    ref->type == REFERENCE_NOTE ||
			    ref->type == REFERENCE_PREFETCH ||
			    (!strcmp(fmt->start, "hide:") && !*fmt->end))
				return true;
			break;
		case REFS_FILTER_ALL:
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
			opt_mailmap ? "--pretty=format:%H%x00%aN <%aE> %ad%x00%cN <%cE> %cd%x00%s"
				    : "--pretty=format:%H%x00%an <%ae> %ad%x00%cn <%ce> %cd%x00%s",
			"--all", "--decorate-refs=", "--simplify-by-decoration",
			NULL
	};
	struct refs_state *state = view->private;
	const struct ref *selected = refs_get_selected(view);
	enum status_code code;
	const char *name = REFS_ALL_NAME;
	size_t lineno;
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
		} else if (!strncmp(refs_argv[i], "--all", 5)) {
			refs_filter = REFS_FILTER_ALL;
		}
	}

	if (!refs_all) {
		int name_length = strlen(name);
		struct ref *ref = calloc(1, sizeof(*refs_all) + name_length);

		if (!ref)
			return ERROR_OUT_OF_MEMORY;

		strcpy(ref->name, name);
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

	/* When reloading, restore the cursor to the reference it was on
	 * rather than to the line, which can now hold another reference. */
	state->keep_lineno = true;
	for (lineno = 0; selected && lineno < view->lines; lineno++) {
		const struct reference *reference = view->line[lineno].data;

		if (reference->ref == selected) {
			view->prev_pos.lineno = lineno;
			state->keep_lineno = false;
			break;
		}
	}
	state->lineno = view->prev_pos.lineno;

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
	view->env->blob[0] = 0;
}

static struct view_ops refs_ops = {
	"reference",
	argv_env.head,
	VIEW_REFRESH | VIEW_SORTABLE | VIEW_LOG_LIKE,
	sizeof(struct refs_state),
	refs_open,
	refs_read,
	view_column_draw,
	refs_request,
	view_column_grep,
	refs_select,
	NULL,
	view_column_bit(AUTHOR) | view_column_bit(COMMITTER) | view_column_bit(COMMIT_TITLE) |
		view_column_bit(DATE) | view_column_bit(ID) |
		view_column_bit(LINE_NUMBER) | view_column_bit(REF),
	refs_get_column_data,
	refs_compare,
};

DEFINE_VIEW(refs);

/* vim: set ts=8 sw=8 noexpandtab: */
