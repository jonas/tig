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

#include "tig/repo.h"
#include "tig/options.h"
#include "tig/parse.h"
#include "tig/watch.h"
#include "tig/graph.h"
#include "tig/display.h"
#include "tig/view.h"
#include "tig/draw.h"
#include "tig/git.h"
#include "tig/status.h"
#include "tig/stage.h"
#include "tig/main.h"
#include "tig/diff.h"
#include "tig/search.h"

/*
 * Main view backend
 */

DEFINE_ALLOCATOR(realloc_reflogs, char *, 32)

bool
main_status_exists(struct view *view, enum line_type type)
{
	struct main_state *state;

	refresh_view(view);

	state = view->private;
	state->goto_line_type = type;
	if (type == LINE_STAT_STAGED && state->add_changes_staged)
		return true;
	if (type == LINE_STAT_UNSTAGED && state->add_changes_unstaged)
		return true;
	if (type == LINE_STAT_UNTRACKED && state->add_changes_untracked)
		return true;

	return false;
}

static bool main_add_changes(struct view *view, struct main_state *state, const char *parent);

static void
main_register_commit(struct view *view, struct commit *commit, const char *ids, bool is_boundary)
{
	struct main_state *state = view->private;
	struct graph *graph = state->graph;

	string_copy_rev(commit->id, ids);

	/* FIXME: lazily check index state here instead of in main_open. */
	if ((state->add_changes_untracked || state->add_changes_unstaged || state->add_changes_staged) && is_head_commit(commit->id)) {
		main_add_changes(view, state, ids);
		state->add_changes_untracked = state->add_changes_unstaged = state->add_changes_staged = false;
	}

	if (state->with_graph)
		graph->add_commit(graph, &commit->graph, commit->id, ids, is_boundary);
}

static struct commit *
main_add_commit(struct view *view, enum line_type type, struct commit *template,
		const char *title, bool custom)
{
	struct main_state *state = view->private;
	size_t titlelen;
	struct commit *commit;
	char buf[SIZEOF_STR / 2];
	struct line *line;

	/* FIXME: More graceful handling of titles; append "..." to
	 * shortened titles, etc. */
	string_expand(buf, sizeof(buf), title, strlen(title), 1);
	title = buf;
	titlelen = strlen(title);

	line = add_line_alloc(view, &commit, type, titlelen, custom);
	if (!line)
		return NULL;

	*commit = *template;
	strncpy(commit->title, title, titlelen);
	memset(template, 0, sizeof(*template));
	state->reflogmsg[0] = 0;

	view_column_info_update(view, line);

	if ((opt_start_on_head && is_head_commit(commit->id)) ||
	    (view->env->goto_id[0] && !strncmp(view->env->goto_id, commit->id, SIZEOF_REV - 1)))
		select_view_line(view, line->lineno + 1);

	return commit;
}

static inline void
main_flush_commit(struct view *view, struct commit *commit)
{
	if (*commit->id)
		main_add_commit(view, LINE_MAIN_COMMIT, commit, "", false);
}

static bool
main_add_changes_commit(struct view *view, enum line_type type, const char *parent, const char *title)
{
	char ids[SIZEOF_STR] = NULL_ID " ";
	struct main_state *state = view->private;
	struct graph *graph = state->graph;
	struct commit commit = {{0}};
	struct timeval now;
	struct timezone tz;

	if (!parent)
		return true;

	if (*parent)
		string_copy_rev(ids + STRING_SIZE(NULL_ID " "), parent);
	else
		ids[STRING_SIZE(NULL_ID)] = 0;

	if (!time_now(&now, &tz)) {
		commit.time.tz = tz.tz_minuteswest * 60;
		commit.time.sec = now.tv_sec - commit.time.tz;
	}

	commit.author = &unknown_ident;
	main_register_commit(view, &commit, ids, false);
	if (state->with_graph && *parent)
		graph->render_parents(graph, &commit.graph);

	if (!main_add_commit(view, type, &commit, title, true))
		return false;

	if (state->goto_line_type == type)
		select_view_line(view, view->lines - 1);

	return true;
}

static bool
main_check_index(struct view *view, struct main_state *state)
{
	struct index_diff diff;

	if (!index_diff(&diff, opt_show_untracked, false))
		return false;

	if (!diff.untracked) {
		watch_apply(&view->watch, WATCH_INDEX_UNTRACKED_NO);
	} else {
		watch_apply(&view->watch, WATCH_INDEX_UNTRACKED_YES);
		state->add_changes_untracked = true;
	}

	if (!diff.unstaged) {
		watch_apply(&view->watch, WATCH_INDEX_UNSTAGED_NO);
	} else {
		watch_apply(&view->watch, WATCH_INDEX_UNSTAGED_YES);
		state->add_changes_unstaged = true;
	}

	if (!diff.staged) {
		watch_apply(&view->watch, WATCH_INDEX_STAGED_NO);
	} else {
		watch_apply(&view->watch, WATCH_INDEX_STAGED_YES);
		state->add_changes_staged = true;
	}

	return true;
}

static bool
main_add_changes(struct view *view, struct main_state *state, const char *parent)
{
	const char *staged_parent = parent;
	const char *unstaged_parent = NULL_ID;
	const char *untracked_parent = NULL_ID;

	if (!state->add_changes_staged) {
		staged_parent = NULL;
		unstaged_parent = parent;
	}

	if (!state->add_changes_unstaged) {
		unstaged_parent = NULL;
		if (!state->add_changes_staged)
			untracked_parent = parent;
	}

	if (!state->add_changes_untracked) {
		untracked_parent = NULL;
	}

	return main_add_changes_commit(view, LINE_STAT_UNTRACKED, untracked_parent, "Untracked changes")
	    && main_add_changes_commit(view, LINE_STAT_UNSTAGED, unstaged_parent, "Unstaged changes")
	    && main_add_changes_commit(view, LINE_STAT_STAGED, staged_parent, "Staged changes");
}

static bool
main_check_argv(struct view *view, const char *argv[])
{
	struct main_state *state = view->private;
	bool with_reflog = false;
	int i;

	for (i = 0; argv[i]; i++) {
		const char *arg = argv[i];
		struct rev_flags rev_flags = {0};

		if (!strcmp(arg, "--graph")) {
			struct view_column *column = get_view_column(view, VIEW_COLUMN_COMMIT_TITLE);

			if (column) {
				column->opt.commit_title.graph = true;
				if (opt_commit_order != COMMIT_ORDER_REVERSE)
					state->with_graph = true;
			}
			argv[i] = "";
			continue;
		}

		if (!strcmp(arg, "--merge")) {
			argv_append(&opt_rev_args, "--boundary");
			continue;
		}

		if (!strcmp(arg, "--first-parent")) {
			state->first_parent = true;
			argv_append(&opt_diff_options, arg);
		}

		if (!argv_parse_rev_flag(arg, &rev_flags))
			continue;

		if (rev_flags.with_reflog)
			with_reflog = true;
		if (!rev_flags.with_graph)
			state->with_graph = false;
		arg += rev_flags.search_offset;
		/* Copy the pattern to search buffer only when starting
		 * from the main view. */
		if (*arg && !*view->ref && !view->prev)
			string_ncopy(view->env->search, arg, strlen(arg));
	}

	return with_reflog;
}

static enum graph_display
main_with_graph(struct view *view, struct view_column *column, enum open_flags flags)
{
	return column && opt_commit_order != COMMIT_ORDER_REVERSE && !open_in_pager_mode(flags) && !opt_log_follow
	       ? column->opt.commit_title.graph : GRAPH_DISPLAY_NO;
}

static enum status_code
main_open(struct view *view, enum open_flags flags)
{
	struct view_column *commit_title_column = get_view_column(view, VIEW_COLUMN_COMMIT_TITLE);
	enum graph_display graph_display = main_with_graph(view, commit_title_column, flags);
	const char *pretty_custom_argv[] = {
		GIT_MAIN_LOG(encoding_arg, commit_order_arg_with_graph(graph_display),
			"%(mainargs)", "%(cmdlineargs)", "%(revargs)", "%(fileargs)",
			show_notes_arg(), log_custom_pretty_arg())
	};
	const char *pretty_raw_argv[] = {
		GIT_MAIN_LOG_RAW(encoding_arg, commit_order_arg_with_graph(graph_display),
			"%(mainargs)", "%(cmdlineargs)", "%(revargs)", "%(fileargs)",
			show_notes_arg())
	};
	struct main_state *state = view->private;
	const char **main_argv = pretty_custom_argv;
	enum watch_trigger changes_triggers = WATCH_NONE;

	if (opt_show_changes && (repo.is_inside_work_tree || *repo.worktree))
		changes_triggers |= WATCH_INDEX;

	state->with_graph = graph_display != GRAPH_DISPLAY_NO;

	if (opt_rev_args && main_check_argv(view, opt_rev_args))
		main_argv = pretty_raw_argv;

	if (state->with_graph) {
		state->graph = init_graph(commit_title_column->opt.commit_title.graph);
		if (!state->graph)
			return ERROR_OUT_OF_MEMORY;
	}

	if (open_in_pager_mode(flags)) {
		changes_triggers = WATCH_NONE;
	}

	{
		/* This calls reset_view() so must be before adding changes commits. */
		enum status_code code = begin_update(view, NULL, main_argv, flags);

		if (code != SUCCESS)
			return code;
	}

	/* Register watch before changes commits are added to record the
	 * start. */
	if (view_can_refresh(view))
		watch_register(&view->watch, WATCH_HEAD | WATCH_REFS | changes_triggers);

	if (changes_triggers)
		main_check_index(view, state);

	return SUCCESS;
}

void
main_done(struct view *view)
{
	struct main_state *state = view->private;
	int i;

	for (i = 0; i < view->lines; i++) {
		struct commit *commit = view->line[i].data;

		free(commit->graph.symbols);
	}

	if (state->graph)
		state->graph->done(state->graph);

	for (i = 0; i < state->reflogs; i++)
		free(state->reflog[i]);
	free(state->reflog);
}

#define main_check_commit_refs(line)	!((line)->no_commit_refs)
#define main_mark_no_commit_refs(line)	(((struct line *) (line))->no_commit_refs = 1)

static inline const struct ref *
main_get_commit_refs(const struct line *line, struct commit *commit)
{
	const struct ref *refs = NULL;

	if (main_check_commit_refs(line) && !(refs = get_ref_list(commit->id)))
		main_mark_no_commit_refs(line);

	return refs;
}

bool
main_get_column_data(struct view *view, const struct line *line, struct view_column_data *column_data)
{
	struct main_state *state = view->private;
	struct commit *commit = line->data;

	column_data->author = commit->author;
	column_data->date = &commit->time;
	column_data->id = commit->id;

	column_data->commit_title = commit->title;
	if (state->with_graph) {
		column_data->graph = state->graph;
		column_data->graph_canvas = &commit->graph;
	}

	column_data->refs = main_get_commit_refs(line, commit);

	return true;
}

static bool
main_add_reflog(struct view *view, struct main_state *state, char *reflog)
{
	char *end = strchr(reflog, ' ');
	int id_width;

	if (!end)
		return false;
	*end = 0;

	if (!realloc_reflogs(&state->reflog, state->reflogs, 1)
	    || !(reflog = strdup(reflog)))
		return false;

	state->reflog[state->reflogs++] = reflog;
	id_width = strlen(reflog);
	if (state->reflog_width < id_width) {
		struct view_column *column = get_view_column(view, VIEW_COLUMN_ID);

		state->reflog_width = id_width;
		if (column && column->opt.id.display)
			view->force_redraw = true;
	}

	return true;
}

/* Reads git log --pretty=raw output and parses it into the commit struct. */
bool
main_read(struct view *view, struct buffer *buf, bool force_stop)
{
	struct main_state *state = view->private;
	struct graph *graph = state->graph;
	enum line_type type;
	struct commit *commit = &state->current;
	char *line;

	if (!buf) {
		main_flush_commit(view, commit);

		if (!force_stop && failed_to_load_initial_view(view))
			die("No revisions match the given arguments.");
		if (view->lines > 0) {
			struct commit *last = view->line[view->lines - 1].data;

			view->line[view->lines - 1].dirty = 1;
			if (!last->author) {
				view->lines--;
				free(last);
			}
		}

		if (state->graph)
			state->graph->done_rendering(graph);
		return true;
	}

	line = buf->data;
	type = get_line_type(line);
	if (type == LINE_COMMIT) {
		bool is_boundary;
		char *author;

		state->in_header = true;
		line += STRING_SIZE("commit ");
		is_boundary = *line == '-';
		while (*line && !isalnum(*line))
			line++;

		main_flush_commit(view, commit);

		author = io_memchr(buf, line, 0);

		if (state->first_parent) {
			char *parent = strchr(line, ' ');
			char *parent_end = parent ? strchr(parent + 1, ' ') : NULL;

			if (parent_end)
				*parent_end = 0;
		}

		main_register_commit(view, &state->current, line, is_boundary);

		if (author) {
			char *title = io_memchr(buf, author, 0);

			parse_author_line(author, &commit->author, &commit->time);
			if (state->with_graph)
				graph->render_parents(graph, &commit->graph);
			if (title) {
				char *notes = io_memchr(buf, title, 0);

				main_add_commit(view, notes && *notes ? LINE_MAIN_ANNOTATED : LINE_MAIN_COMMIT,
						commit, title, false);
			}
		}

		return true;
	}

	if (!*commit->id)
		return true;

	/* Empty line separates the commit header from the log itself. */
	if (*line == '\0')
		state->in_header = false;

	switch (type) {
	case LINE_PP_REFLOG:
		if (!main_add_reflog(view, state, line + STRING_SIZE("Reflog: ")))
			return false;
		break;

	case LINE_PP_REFLOGMSG:
		line += STRING_SIZE("Reflog message: ");
		string_ncopy(state->reflogmsg, line, strlen(line));
		break;

	case LINE_PARENT:
		if (state->with_graph)
			graph->add_parent(graph, line + STRING_SIZE("parent "));
		break;

	case LINE_AUTHOR:
		parse_author_line(line + STRING_SIZE("author "),
				  &commit->author, &commit->time);
		if (state->with_graph)
			graph->render_parents(graph, &commit->graph);
		break;

	default:
		/* Fill in the commit title if it has not already been set. */
		if (*commit->title)
			break;

		/* Skip lines in the commit header. */
		if (state->in_header)
			break;

		/* Require titles to start with a non-space character at the
		 * offset used by git log. */
		if (strncmp(line, "    ", 4))
			break;
		line += 4;
		/* Well, if the title starts with a whitespace character,
		 * try to be forgiving.  Otherwise we end up with no title. */
		while (isspace(*line))
			line++;
		if (*line == '\0')
			break;
		if (*state->reflogmsg)
			line = state->reflogmsg;
		main_add_commit(view, LINE_MAIN_COMMIT, commit, line, false);
	}

	return true;
}

enum request
main_request(struct view *view, enum request request, struct line *line)
{
	enum open_flags flags = (request != REQ_VIEW_DIFF &&
				 (view_is_displayed(view) ||
				  (line->type == LINE_MAIN_COMMIT && !view_is_displayed(&diff_view)) ||
				  line->type == LINE_STAT_UNSTAGED ||
				  line->type == LINE_STAT_STAGED ||
				  line->type == LINE_STAT_UNTRACKED))
				? OPEN_SPLIT : OPEN_DEFAULT;

	switch (request) {
	case REQ_VIEW_DIFF:
	case REQ_ENTER:
		if ((view_is_displayed(view) && display[0] != view) ||
		    (!view_is_displayed(view) && flags == OPEN_SPLIT))
			maximize_view(view, true);

		if (line->type == LINE_STAT_UNSTAGED
		    || line->type == LINE_STAT_STAGED)
			open_stage_view(view, NULL, line->type, flags);
		else if (line->type == LINE_STAT_UNTRACKED)
			open_status_view(view, true, flags);
		else
			open_diff_view(view, flags);
		break;

	case REQ_REFRESH:
		load_refs(true);
		refresh_view(view);
		break;

	case REQ_PARENT:
		goto_id(view, "%(commit)^", true, false);
		break;

	case REQ_MOVE_NEXT_MERGE:
	case REQ_MOVE_PREV_MERGE:
		find_merge(view, request);
		break;

	default:
		return request;
	}

	return REQ_NONE;
}

void
main_select(struct view *view, struct line *line)
{
	struct commit *commit = line->data;

	if (line->type == LINE_STAT_STAGED || line->type == LINE_STAT_UNSTAGED || line->type == LINE_STAT_UNTRACKED) {
		string_ncopy(view->ref, commit->title, strlen(commit->title));
		status_stage_info(view->env->status, line->type, NULL);
	} else {
		struct main_state *state = view->private;
		const struct ref *ref = main_get_commit_refs(line, commit);

		if (state->reflogs) {
			assert(state->reflogs >= line->lineno);
			string_ncopy(view->ref, state->reflog[line->lineno - 1],
				     strlen(state->reflog[line->lineno - 1]));
		} else {
			string_copy_rev(view->ref, commit->id);
		}
		if (ref)
			ref_update_env(view->env, ref, true);
	}
	string_copy_rev(view->env->commit, commit->id);
}

static struct view_ops main_ops = {
	"commit",
	argv_env.head,
	VIEW_SEND_CHILD_ENTER | VIEW_FILE_FILTER | VIEW_LOG_LIKE | VIEW_REFRESH,
	sizeof(struct main_state),
	main_open,
	main_read,
	view_column_draw,
	main_request,
	view_column_grep,
	main_select,
	main_done,
	view_column_bit(AUTHOR) | view_column_bit(COMMIT_TITLE) |
		view_column_bit(DATE) |	view_column_bit(ID) |
		view_column_bit(LINE_NUMBER),
	main_get_column_data,
};

DEFINE_VIEW(main);

/* vim: set ts=8 sw=8 noexpandtab: */
