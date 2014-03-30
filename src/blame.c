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
#include "tig/refs.h"
#include "tig/repo.h"
#include "tig/options.h"
#include "tig/parse.h"
#include "tig/display.h"
#include "tig/view.h"
#include "tig/draw.h"
#include "tig/git.h"
#include "tig/diff.h"

/*
 * Blame backend
 *
 * Loading the blame view is a two phase job:
 *
 *  1. File content is read either using argv_env.file from the
 *     filesystem or using git-cat-file.
 *  2. Then blame information is incrementally added by
 *     reading output from git-blame.
 */

struct blame_history_state {
	char id[SIZEOF_REV];		/* SHA1 ID. */
	const char *filename;		/* Name of file. */
};

static struct view_history blame_view_history = { sizeof(struct blame_history_state) };

struct blame {
	struct blame_commit *commit;
	unsigned long lineno;
	char text[1];
};

struct blame_state {
	struct blame_commit *commit;
	char author[SIZEOF_STR];
	int blamed;
	bool done_reading;
	bool auto_filename_display;
	/* The history state for the current view is cached in the view
	 * state so it always matches what was used to load the current blame
	 * view. */
	struct blame_history_state history_state;
};

static bool
blame_detect_filename_display(struct view *view)
{
	bool show_filenames = FALSE;
	const char *filename = NULL;
	int i;

	if (opt_blame_options) {
		for (i = 0; opt_blame_options[i]; i++) {
			if (prefixcmp(opt_blame_options[i], "-C"))
				continue;

			show_filenames = TRUE;
		}
	}

	for (i = 0; i < view->lines; i++) {
		struct blame *blame = view->line[i].data;

		if (blame->commit && blame->commit->id[0]) {
			if (!filename)
				filename = blame->commit->filename;
			else if (strcmp(filename, blame->commit->filename))
				show_filenames = TRUE;
		}
	}

	return show_filenames;
}

static bool
blame_open(struct view *view, enum open_flags flags)
{
	struct blame_state *state = view->private;
	const char *file_argv[] = { repo.cdup, view->env->file , NULL };
	char path[SIZEOF_STR];
	size_t i;

	if (is_initial_view(view)) {
		/* Finish validating and setting up blame options */
		if (!opt_file_argv || opt_file_argv[1] || (opt_rev_argv && opt_rev_argv[1]))
			usage("Invalid number of options to blame");

		if (opt_rev_argv) {
			string_ncopy(view->env->ref, opt_rev_argv[0], strlen(opt_rev_argv[0]));
		}

		string_ncopy(view->env->file, opt_file_argv[0], strlen(opt_file_argv[0]));

		opt_blame_options = opt_cmdline_argv;
		opt_cmdline_argv = NULL;
	}

	if (!view->env->file[0]) {
		report("No file chosen, press %s to open tree view",
			get_view_key(view, REQ_VIEW_TREE));
		return FALSE;
	}

	if (!view->prev && *repo.prefix && !(flags & (OPEN_RELOAD | OPEN_REFRESH))) {
		string_copy(path, view->env->file);
		if (!string_format(view->env->file, "%s%s", repo.prefix, path)) {
			report("Failed to setup the blame view");
			return FALSE;
		}
	}

	if (*view->env->ref || !begin_update(view, repo.cdup, file_argv, flags)) {
		const char *blame_cat_file_argv[] = {
			"git", "cat-file", "blob", "%(ref):%(file)", NULL
		};

		if (!begin_update(view, repo.cdup, blame_cat_file_argv, flags))
			return FALSE;
	}

	/* First pass: remove multiple references to the same commit. */
	for (i = 0; i < view->lines; i++) {
		struct blame *blame = view->line[i].data;

		if (blame->commit && blame->commit->id[0])
			blame->commit->id[0] = 0;
		else
			blame->commit = NULL;
	}

	/* Second pass: free existing references. */
	for (i = 0; i < view->lines; i++) {
		struct blame *blame = view->line[i].data;

		if (blame->commit)
			free(blame->commit);
	}

	if (!(flags & OPEN_RELOAD))
		reset_view_history(&blame_view_history);
	string_copy_rev(state->history_state.id, view->env->ref);
	state->history_state.filename = get_path(view->env->file);
	if (!state->history_state.filename)
		return FALSE;
	string_format(view->vid, "%s", view->env->file);
	string_format(view->ref, "%s ...", view->env->file);

	return TRUE;
}

static struct blame_commit *
get_blame_commit(struct view *view, const char *id)
{
	size_t i;

	for (i = 0; i < view->lines; i++) {
		struct blame *blame = view->line[i].data;

		if (!blame->commit)
			continue;

		if (!strncmp(blame->commit->id, id, SIZEOF_REV - 1))
			return blame->commit;
	}

	{
		struct blame_commit *commit = calloc(1, sizeof(*commit));

		if (commit)
			string_ncopy(commit->id, id, SIZEOF_REV);
		return commit;
	}
}

static struct blame_commit *
read_blame_commit(struct view *view, const char *text, struct blame_state *state)
{
	struct blame_header header;
	struct blame_commit *commit;
	struct blame *blame;

	if (!parse_blame_header(&header, text, view->lines))
		return NULL;

	commit = get_blame_commit(view, text);
	if (!commit)
		return NULL;

	state->blamed += header.group;
	while (header.group--) {
		struct line *line = &view->line[header.lineno + header.group - 1];

		blame = line->data;
		blame->commit = commit;
		blame->lineno = header.orig_lineno + header.group - 1;
		line->dirty = 1;
	}

	return commit;
}

static bool
blame_read_file(struct view *view, const char *text, struct blame_state *state)
{
	if (!text) {
		const char *blame_argv[] = {
			"git", "blame", encoding_arg, "%(blameargs)", "--incremental",
				*view->env->ref ? view->env->ref : "--incremental", "--", view->env->file, NULL
		};

		if (failed_to_load_initial_view(view))
			die("No blame exist for %s", view->vid);

		if (view->lines == 0 || !begin_update(view, repo.cdup, blame_argv, OPEN_EXTRA)) {
			report("Failed to load blame data");
			return TRUE;
		}

		if (view->env->lineno > 0) {
			select_view_line(view, view->env->lineno);
			view->env->lineno = 0;
		}

		state->done_reading = TRUE;
		return FALSE;

	} else {
		size_t textlen = strlen(text);
		struct blame *blame;

		if (!add_line_alloc(view, &blame, LINE_ID, textlen, FALSE))
			return FALSE;

		blame->commit = NULL;
		strncpy(blame->text, text, textlen);
		blame->text[textlen] = 0;
		return TRUE;
	}
}

static bool
blame_read(struct view *view, char *line)
{
	struct blame_state *state = view->private;

	if (!state->done_reading)
		return blame_read_file(view, line, state);

	if (!line) {
		state->auto_filename_display = blame_detect_filename_display(view);
		string_format(view->ref, "%s", view->vid);
		if (view_is_displayed(view)) {
			update_view_title(view);
			redraw_view_from(view, 0);
		}
		return TRUE;
	}

	if (!state->commit) {
		state->commit = read_blame_commit(view, line, state);
		string_format(view->ref, "%s %2zd%%", view->vid,
			      view->lines ? state->blamed * 100 / view->lines : 0);

	} else if (parse_blame_info(state->commit, state->author, line)) {
		bool update_view_columns = TRUE;
		int i;

		if (!state->commit->filename)
			return FALSE;

		for (i = 0; i < view->lines; i++) {
			struct line *line = &view->line[i];
			struct blame *blame = line->data;

			if (blame && blame->commit == state->commit) {
				line->dirty = 1;
				if (update_view_columns)
					view_columns_info_update(view, line);
				update_view_columns = FALSE;
			}
		}

		state->commit = NULL;
	}

	return TRUE;
}

static const enum view_column blame_columns[] = {
	VIEW_COLUMN_DATE,
	VIEW_COLUMN_AUTHOR,
	VIEW_COLUMN_FILE_NAME,
	VIEW_COLUMN_ID,
	VIEW_COLUMN_LINE_NUMBER,
	VIEW_COLUMN_TEXT,
};

bool
blame_get_columns(struct view *view, const struct line *line, struct view_columns *columns)
{
	struct blame_state *state = view->private;
	struct blame *blame = line->data;

	if (blame->commit) {
		columns->id = blame->commit->id;
		columns->author = blame->commit->author;
		if (opt_show_filename != FILENAME_AUTO || state->auto_filename_display || view->pipe)
			columns->file_name = blame->commit->filename;
		columns->date = &blame->commit->time;
		columns->commit_title = blame->commit->title;
	}

	columns->text = blame->text;

	return TRUE;
}

static bool
blame_draw(struct view *view, struct line *line, unsigned int lineno)
{
	struct blame_state *state = view->private;
	struct blame *blame = line->data;
	struct time *time = NULL;
	const char *id = NULL, *filename = NULL;
	const struct ident *author = NULL;
	enum line_type id_type = LINE_ID;
	static const enum line_type blame_colors[] = {
		LINE_PALETTE_0,
		LINE_PALETTE_1,
		LINE_PALETTE_2,
		LINE_PALETTE_3,
		LINE_PALETTE_4,
		LINE_PALETTE_5,
		LINE_PALETTE_6,
	};

#define BLAME_COLOR(i) \
	(blame_colors[(i) % ARRAY_SIZE(blame_colors)])

	if (blame->commit && blame->commit->filename) {
		id = blame->commit->id;
		author = blame->commit->author;
		filename = blame->commit->filename;
		time = &blame->commit->time;
		id_type = BLAME_COLOR((long) blame->commit);
	}

	if (draw_date(view, time))
		return TRUE;

	if (draw_author(view, author, opt_author_width ? opt_author_width : view->columns_info[1].width))
		return TRUE;

	if (draw_filename(view, filename, state->auto_filename_display,
			  opt_show_filename_width ? opt_show_filename_width : view->columns_info[2].width))
		return TRUE;

	if (draw_id_custom(view, id_type, id, opt_id_width))
		return TRUE;

	if (draw_lineno(view, lineno))
		return TRUE;

	draw_text(view, LINE_DEFAULT, blame->text);
	return TRUE;
}

static bool
check_blame_commit(struct blame *blame, bool check_null_id)
{
	if (!blame->commit)
		report("Commit data not loaded yet");
	else if (check_null_id && string_rev_is_null(blame->commit->id))
		report("No commit exist for the selected line");
	else
		return TRUE;
	return FALSE;
}

static void
setup_blame_parent_line(struct view *view, struct blame *blame)
{
	char from[SIZEOF_REF + SIZEOF_STR];
	char to[SIZEOF_REF + SIZEOF_STR];
	const char *diff_tree_argv[] = {
		"git", "diff", encoding_arg, "--no-textconv", "--no-extdiff",
			"--no-color", "-U0", from, to, "--", NULL
	};
	struct io io;
	int parent_lineno = -1;
	int blamed_lineno = -1;
	char *line;

	if (!string_format(from, "%s:%s", view->env->ref, view->env->file) ||
	    !string_format(to, "%s:%s", blame->commit->id, blame->commit->filename) ||
	    !io_run(&io, IO_RD, NULL, opt_env, diff_tree_argv))
		return;

	while ((line = io_get(&io, '\n', TRUE))) {
		if (*line == '@') {
			char *pos = strchr(line, '+');

			parent_lineno = atoi(line + 4);
			if (pos)
				blamed_lineno = atoi(pos + 1);

		} else if (*line == '+' && parent_lineno != -1) {
			if (blame->lineno == blamed_lineno - 1 &&
			    !strcmp(blame->text, line + 1)) {
				view->pos.lineno = parent_lineno ? parent_lineno - 1 : 0;
				break;
			}
			blamed_lineno++;
		}
	}

	io_done(&io);
}

static void
blame_go_forward(struct view *view, struct blame *blame, bool parent)
{
	struct blame_state *state = view->private;
	struct blame_history_state *history_state = &state->history_state;
	struct blame_commit *commit = blame->commit;
	const char *id = parent ? commit->parent_id : commit->id;
	const char *filename = parent ? commit->parent_filename : commit->filename;

	if (!*id && parent) {
		report("The selected commit has no parents");
		return;
	}

	if (!strcmp(history_state->id, id) && !strcmp(history_state->filename, filename)) {
		report("The selected commit is already displayed");
		return;
	}

	if (!push_view_history_state(&blame_view_history, &view->pos, history_state)) {
		report("Failed to save current view state");
		return;
	}

	string_ncopy(view->env->ref, id, sizeof(commit->id));
	string_ncopy(view->env->file, filename, strlen(filename));
	if (parent)
		setup_blame_parent_line(view, blame);
	view->env->lineno = blame->lineno;
	reload_view(view);
}

static void
blame_go_back(struct view *view)
{
	struct blame_history_state history_state;

	if (!pop_view_history_state(&blame_view_history, &view->pos, &history_state)) {
		report("Already at start of history");
		return;
	}

	string_copy(view->env->ref, history_state.id);
	string_ncopy(view->env->file, history_state.filename, strlen(history_state.filename));
	view->env->lineno = view->pos.lineno;
	reload_view(view);
}

static enum request
blame_request(struct view *view, enum request request, struct line *line)
{
	enum open_flags flags = view_is_displayed(view) ? OPEN_SPLIT : OPEN_DEFAULT;
	struct blame *blame = line->data;
	struct view *diff = &diff_view;

	switch (request) {
	case REQ_VIEW_BLAME:
	case REQ_PARENT:
		if (!check_blame_commit(blame, TRUE))
			break;
		blame_go_forward(view, blame, request == REQ_PARENT);
		break;

	case REQ_BACK:
		blame_go_back(view);
		break;

	case REQ_ENTER:
		if (!check_blame_commit(blame, FALSE))
			break;

		if (view_is_displayed(diff) &&
		    !strcmp(blame->commit->id, diff->ref))
			break;

		if (string_rev_is_null(blame->commit->id)) {
			const char *diff_parent_argv[] = {
				GIT_DIFF_BLAME(encoding_arg,
					diff_context_arg(),
					ignore_space_arg(), view->vid)
			};
			const char *diff_no_parent_argv[] = {
				GIT_DIFF_BLAME_NO_PARENT(encoding_arg,
					diff_context_arg(),
					ignore_space_arg(), view->vid)
			};
			const char **diff_index_argv = *blame->commit->parent_id
				? diff_parent_argv : diff_no_parent_argv;

			open_argv(view, diff, diff_index_argv, NULL, flags);
			if (diff->pipe)
				string_copy_rev(diff->ref, NULL_ID);
		} else {
			open_diff_view(view, flags);
		}
		break;

	default:
		return request;
	}

	return REQ_NONE;
}

static void
blame_select(struct view *view, struct line *line)
{
	struct blame *blame = line->data;
	struct blame_commit *commit = blame->commit;

	if (!commit)
		return;

	if (string_rev_is_null(commit->id))
		string_ncopy(view->env->commit, "HEAD", 4);
	else
		string_copy_rev(view->env->commit, commit->id);
}

static struct view_ops blame_ops = {
	"line",
	argv_env.commit,
	VIEW_ALWAYS_LINENO | VIEW_SEND_CHILD_ENTER | VIEW_BLAME_LIKE,
	sizeof(struct blame_state),
	blame_open,
	blame_read,
	blame_draw,
	blame_request,
	view_columns_grep,
	blame_select,
	NULL,
	blame_get_columns,
	blame_columns,
	ARRAY_SIZE(blame_columns),
};

DEFINE_VIEW(blame);

/* vim: set ts=8 sw=8 noexpandtab: */
