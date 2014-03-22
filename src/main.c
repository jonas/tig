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

#include "tig/repo.h"
#include "tig/options.h"
#include "tig/parse.h"
#include "tig/graph.h"
#include "tig/display.h"
#include "tig/view.h"
#include "tig/draw.h"
#include "tig/git.h"
#include "tig/status.h"
#include "tig/main.h"
#include "tig/diff.h"

/*
 * Revision graph
 */

static const enum line_type graph_colors[] = {
	LINE_PALETTE_0,
	LINE_PALETTE_1,
	LINE_PALETTE_2,
	LINE_PALETTE_3,
	LINE_PALETTE_4,
	LINE_PALETTE_5,
	LINE_PALETTE_6,
};

static enum line_type get_graph_color(struct graph_symbol *symbol)
{
	if (symbol->commit)
		return LINE_GRAPH_COMMIT;
	assert(symbol->color < ARRAY_SIZE(graph_colors));
	return graph_colors[symbol->color];
}

static bool
draw_graph_utf8(struct view *view, struct graph_symbol *symbol, enum line_type color, bool first)
{
	const char *chars = graph_symbol_to_utf8(symbol);

	return draw_text(view, color, chars + !!first);
}

static bool
draw_graph_ascii(struct view *view, struct graph_symbol *symbol, enum line_type color, bool first)
{
	const char *chars = graph_symbol_to_ascii(symbol);

	return draw_text(view, color, chars + !!first);
}

static bool
draw_graph_chtype(struct view *view, struct graph_symbol *symbol, enum line_type color, bool first)
{
	const chtype *chars = graph_symbol_to_chtype(symbol);

	return draw_graphic(view, color, chars + !!first, 2 - !!first, FALSE);
}

typedef bool (*draw_graph_fn)(struct view *, struct graph_symbol *, enum line_type, bool);

static bool draw_graph(struct view *view, struct graph_canvas *canvas)
{
	static const draw_graph_fn fns[] = {
		draw_graph_ascii,
		draw_graph_chtype,
		draw_graph_utf8
	};
	draw_graph_fn fn = fns[opt_line_graphics];
	int i;

	for (i = 0; i < canvas->size; i++) {
		struct graph_symbol *symbol = &canvas->symbols[i];
		enum line_type color = get_graph_color(symbol);

		if (fn(view, symbol, color, i == 0))
			return TRUE;
	}

	return draw_text(view, LINE_DEFAULT, " ");
}

/*
 * Main view backend
 */

DEFINE_ALLOCATOR(realloc_reflogs, char *, 32)

static void
main_register_commit(struct view *view, struct commit *commit, const char *ids, bool is_boundary)
{
	struct main_state *state = view->private;

	string_copy_rev(commit->id, ids);
	if (state->with_graph)
		graph_add_commit(&state->graph, &commit->graph, commit->id, ids, is_boundary);
}

static struct commit *
main_add_commit(struct view *view, enum line_type type, struct commit *template,
		const char *title, bool custom)
{
	struct main_state *state = view->private;
	size_t titlelen = strlen(title);
	struct commit *commit;
	char buf[SIZEOF_STR / 2];

	/* FIXME: More graceful handling of titles; append "..." to
	 * shortened titles, etc. */
	string_expand(buf, sizeof(buf), title, 1);
	title = buf;
	titlelen = strlen(title);

	if (!add_line_alloc(view, &commit, type, titlelen, custom))
		return NULL;

	*commit = *template;
	strncpy(commit->title, title, titlelen);
	state->graph.canvas = &commit->graph;
	memset(template, 0, sizeof(*template));
	state->reflogmsg[0] = 0;
	return commit;
}

static inline void
main_flush_commit(struct view *view, struct commit *commit)
{
	if (*commit->id)
		main_add_commit(view, LINE_MAIN_COMMIT, commit, "", FALSE);
}

static bool
main_has_changes(const char *argv[])
{
	struct io io;

	if (!io_run(&io, IO_BG, NULL, opt_env, argv, -1))
		return FALSE;
	io_done(&io);
	return io.status == 1;
}

static void
main_add_changes_commit(struct view *view, enum line_type type, const char *parent, const char *title)
{
	char ids[SIZEOF_STR] = NULL_ID " ";
	struct main_state *state = view->private;
	struct commit commit = {};
	struct timeval now;
	struct timezone tz;

	if (!parent)
		return;

	string_copy_rev(ids + STRING_SIZE(NULL_ID " "), parent);

	if (!gettimeofday(&now, &tz)) {
		commit.time.tz = tz.tz_minuteswest * 60;
		commit.time.sec = now.tv_sec - commit.time.tz;
	}

	commit.author = &unknown_ident;
	main_register_commit(view, &commit, ids, FALSE);
	if (main_add_commit(view, type, &commit, title, TRUE) && state->with_graph)
		graph_render_parents(&state->graph);
}

static void
main_add_changes_commits(struct view *view, struct main_state *state, const char *parent)
{
	const char *staged_argv[] = { GIT_DIFF_STAGED_FILES("--quiet") };
	const char *unstaged_argv[] = { GIT_DIFF_UNSTAGED_FILES("--quiet") };
	const char *staged_parent = NULL_ID;
	const char *unstaged_parent = parent;

	if (!is_head_commit(parent))
		return;

	state->added_changes_commits = TRUE;

	io_run_bg(update_index_argv);

	if (!main_has_changes(unstaged_argv)) {
		unstaged_parent = NULL;
		staged_parent = parent;
	}

	if (!main_has_changes(staged_argv)) {
		staged_parent = NULL;
	}

	main_add_changes_commit(view, LINE_STAT_STAGED, staged_parent, "Staged changes");
	main_add_changes_commit(view, LINE_STAT_UNSTAGED, unstaged_parent, "Unstaged changes");
}

static size_t
main_find_argv(const char *arg, const char *argv[], size_t argc)
{
	int i;

	for (i = argc - 1; i >= 0; i--) {
		size_t prefixlen = strlen(argv[i]);

		if (!strncmp(arg, argv[i], prefixlen))
			return prefixlen;
	}

	return 0;
}

static void
main_check_argv(struct view *view, const char *argv[])
{
	static const char *no_graph_search_args[] = {
		"-S", "-G", "--grep="
	};
	struct main_state *state = view->private;
	int i;

	for (i = 0; argv[i]; i++) {
		const char *arg = argv[i];
		size_t len = main_find_argv(arg, no_graph_search_args,
					    ARRAY_SIZE(no_graph_search_args));

		if (len > 0) {
			state->with_graph = FALSE;
			if (!*view->env->search)
				string_ncopy(view->env->search,
					     arg + len, strlen(arg + len));
			break;
		}
	}
}

static bool
main_open(struct view *view, enum open_flags flags)
{
	const char *main_argv[] = {
		GIT_MAIN_LOG(encoding_arg, commit_order_arg(), "%(cmdlineargs)", "%(revargs)", "%(fileargs)")
	};
	struct main_state *state = view->private;

	state->with_graph = opt_show_rev_graph &&
			    opt_commit_order != COMMIT_ORDER_REVERSE;

	if (state->with_graph && opt_cmdline_argv) {
		main_check_argv(view, opt_cmdline_argv);
	}

	if (flags & OPEN_PAGER_MODE) {
		state->added_changes_commits = TRUE;
		state->with_graph = FALSE;
	}

	return begin_update(view, NULL, main_argv, flags);
}

static void
main_done(struct view *view)
{
	struct main_state *state = view->private;
	int i;

	for (i = 0; i < view->lines; i++) {
		struct commit *commit = view->line[i].data;

		free(commit->graph.symbols);
	}

	for (i = 0; i < state->reflogs; i++)
		free(state->reflog[i]);
	free(state->reflog);
}

#define MAIN_NO_COMMIT_REFS 1
#define main_check_commit_refs(line)	!((line)->user_flags & MAIN_NO_COMMIT_REFS)
#define main_mark_no_commit_refs(line)	((line)->user_flags |= MAIN_NO_COMMIT_REFS)

static inline struct ref_list *
main_get_commit_refs(struct line *line, struct commit *commit)
{
	struct ref_list *refs = NULL;

	if (main_check_commit_refs(line) && !(refs = get_ref_list(commit->id)))
		main_mark_no_commit_refs(line);

	return refs;
}

bool
main_draw(struct view *view, struct line *line, unsigned int lineno)
{
	struct main_state *state = view->private;
	struct commit *commit = line->data;
	struct ref_list *refs = NULL;

	if (!commit->author)
		return FALSE;

	if (draw_lineno(view, lineno))
		return TRUE;

	if (opt_show_id) {
		if (state->reflogs) {
			const char *id = state->reflog[line->lineno - 1];

			if (draw_id_custom(view, LINE_ID, id, state->reflog_width))
				return TRUE;
		} else if (draw_id(view, commit->id)) {
			return TRUE;
		}
	}

	if (draw_date(view, &commit->time))
		return TRUE;

	if (draw_author(view, commit->author))
		return TRUE;

	if (state->with_graph && draw_graph(view, &commit->graph))
		return TRUE;

	if ((refs = main_get_commit_refs(line, commit)) && draw_refs(view, refs))
		return TRUE;

	if (commit->title)
		draw_commit_title(view, commit->title, 0);
	return TRUE;
}

static bool
main_add_reflog(struct view *view, struct main_state *state, char *reflog)
{
	char *end = strchr(reflog, ' ');
	int id_width;

	if (!end)
		return FALSE;
	*end = 0;

	if (!realloc_reflogs(&state->reflog, state->reflogs, 1)
	    || !(reflog = strdup(reflog)))
		return FALSE;

	state->reflog[state->reflogs++] = reflog;
	id_width = strlen(reflog);
	if (state->reflog_width < id_width) {
		state->reflog_width = id_width;
		if (opt_show_id)
			view->force_redraw = TRUE;
	}

	return TRUE;
}

/* Reads git log --pretty=raw output and parses it into the commit struct. */
bool
main_read(struct view *view, char *line)
{
	struct main_state *state = view->private;
	struct graph *graph = &state->graph;
	enum line_type type;
	struct commit *commit = &state->current;

	if (!line) {
		main_flush_commit(view, commit);

		if (failed_to_load_initial_view(view))
			die("No revisions match the given arguments.");
		if (view->lines > 0) {
			struct commit *last = view->line[view->lines - 1].data;

			view->line[view->lines - 1].dirty = 1;
			if (!last->author) {
				view->lines--;
				free(last);
			}
		}

		if (state->with_graph)
			done_graph(graph);
		return TRUE;
	}

	type = get_line_type(line);
	if (type == LINE_COMMIT) {
		bool is_boundary;

		state->in_header = TRUE;
		line += STRING_SIZE("commit ");
		is_boundary = *line == '-';
		while (*line && !isalnum(*line))
			line++;

		if (!state->added_changes_commits && opt_show_changes && repo.is_inside_work_tree)
			main_add_changes_commits(view, state, line);
		else
			main_flush_commit(view, commit);

		main_register_commit(view, &state->current, line, is_boundary);
		return TRUE;
	}

	if (!*commit->id)
		return TRUE;

	/* Empty line separates the commit header from the log itself. */
	if (*line == '\0')
		state->in_header = FALSE;

	switch (type) {
	case LINE_PP_REFLOG:
		if (!main_add_reflog(view, state, line + STRING_SIZE("Reflog: ")))
			return FALSE;
		break;

	case LINE_PP_REFLOGMSG:
		line += STRING_SIZE("Reflog message: ");
		string_ncopy(state->reflogmsg, line, strlen(line));
		break;

	case LINE_PARENT:
		if (state->with_graph && !graph->has_parents)
			graph_add_parent(graph, line + STRING_SIZE("parent "));
		break;

	case LINE_AUTHOR:
		parse_author_line(line + STRING_SIZE("author "),
				  &commit->author, &commit->time);
		if (state->with_graph)
			graph_render_parents(graph);
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
		main_add_commit(view, LINE_MAIN_COMMIT, commit, line, FALSE);
	}

	return TRUE;
}

enum request
main_request(struct view *view, enum request request, struct line *line)
{
	enum open_flags flags = (view_is_displayed(view) && request != REQ_VIEW_DIFF)
				? OPEN_SPLIT : OPEN_DEFAULT;

	switch (request) {
	case REQ_NEXT:
	case REQ_PREVIOUS:
		if (view_is_displayed(view) && display[0] != view)
			return request;
		/* Do not pass navigation requests to the branch view
		 * when the main view is maximized. (GH #38) */
		return request == REQ_NEXT ? REQ_MOVE_DOWN : REQ_MOVE_UP;

	case REQ_VIEW_DIFF:
	case REQ_ENTER:
		if (view_is_displayed(view) && display[0] != view)
			maximize_view(view, TRUE);

		if (line->type == LINE_STAT_UNSTAGED
		    || line->type == LINE_STAT_STAGED) {
			struct view *diff = &diff_view;
			const char *diff_staged_argv[] = {
				GIT_DIFF_STAGED(encoding_arg,
					diff_context_arg(),
					ignore_space_arg(), NULL, NULL)
			};
			const char *diff_unstaged_argv[] = {
				GIT_DIFF_UNSTAGED(encoding_arg,
					diff_context_arg(),
					ignore_space_arg(), NULL, NULL)
			};
			const char **diff_argv = line->type == LINE_STAT_STAGED
				? diff_staged_argv : diff_unstaged_argv;

			open_argv(view, diff, diff_argv, NULL, flags);
			break;
		}

		open_diff_view(view, flags);
		break;

	case REQ_REFRESH:
		load_refs(TRUE);
		refresh_view(view);
		break;

	case REQ_JUMP_COMMIT:
	{
		int lineno;

		for (lineno = 0; lineno < view->lines; lineno++) {
			struct commit *commit = view->line[lineno].data;

			if (!strncasecmp(commit->id, view->env->search, strlen(view->env->search))) {
				select_view_line(view, lineno);
				report_clear();
				return REQ_NONE;
			}
		}

		report("Unable to find commit '%s'", view->env->search);
		break;
	}
	default:
		return request;
	}

	return REQ_NONE;
}

static bool
grep_refs(struct line *line, struct commit *commit, regex_t *regex)
{
	struct ref_list *list;
	regmatch_t pmatch;
	size_t i;

	if (!opt_show_refs || !(list = main_get_commit_refs(line, commit)))
		return FALSE;

	for (i = 0; i < list->size; i++) {
		if (!regexec(regex, list->refs[i]->name, 1, &pmatch, 0))
			return TRUE;
	}

	return FALSE;
}

bool
main_grep(struct view *view, struct line *line)
{
	struct commit *commit = line->data;
	const char *text[] = {
		commit->id,
		commit->title,
		mkauthor(commit->author, opt_author_width, opt_show_author),
		mkdate(&commit->time, opt_show_date),
		NULL
	};

	return grep_text(view, text) || grep_refs(line, commit, view->regex);
}

static struct ref *
main_get_commit_branch(struct line *line, struct commit *commit)
{
	struct ref_list *list = main_get_commit_refs(line, commit);
	struct ref *branch = NULL;
	size_t i;

	for (i = 0; list && i < list->size; i++) {
		struct ref *ref = list->refs[i];

		switch (get_line_type_from_ref(ref)) {
		case LINE_MAIN_HEAD:
		case LINE_MAIN_REF:
			/* Always prefer local branches. */
			return ref;

		default:
			branch = ref;
		}
	}

	return branch;
}

void
main_select(struct view *view, struct line *line)
{
	struct commit *commit = line->data;

	if (line->type == LINE_STAT_STAGED || line->type == LINE_STAT_UNSTAGED) {
		string_ncopy(view->ref, commit->title, strlen(commit->title));
	} else {
		struct ref *branch = main_get_commit_branch(line, commit);

		if (branch)
			string_copy_rev(view->env->branch, branch->name);
		string_copy_rev(view->ref, commit->id);
	}
	string_copy_rev(view->env->commit, commit->id);
}

static struct view_ops main_ops = {
	"commit",
	{ "main" },
	argv_env.head,
	VIEW_SEND_CHILD_ENTER | VIEW_FILE_FILTER | VIEW_LOG_LIKE | VIEW_REFRESH,
	sizeof(struct main_state),
	main_open,
	main_read,
	main_draw,
	main_request,
	main_grep,
	main_select,
	main_done,
};

DEFINE_VIEW(main);

/* vim: set ts=8 sw=8 noexpandtab: */
