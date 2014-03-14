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

#include "tig/argv.h"
#include "tig/refs.h"
#include "tig/repo.h"
#include "tig/options.h"
#include "tig/display.h"
#include "tig/parse.h"
#include "tig/pager.h"
#include "tig/diff.h"
#include "tig/draw.h"

#define DIFF_LINE_COMMIT_TITLE 1

static bool
diff_open(struct view *view, enum open_flags flags)
{
	const char *diff_argv[] = {
		"git", "show", encoding_arg, "--pretty=fuller", "--root",
			"--patch-with-stat",
			show_notes_arg(), diff_context_arg(), ignore_space_arg(),
			"%(diffargs)", "%(cmdlineargs)", "--no-color", "%(commit)",
			"--", "%(fileargs)", NULL
	};

	return begin_update(view, NULL, diff_argv, flags);
}

bool
diff_common_read(struct view *view, const char *data, struct diff_state *state)
{
	enum line_type type = get_line_type(data);

	if (!view->lines && type != LINE_COMMIT)
		state->reading_diff_stat = TRUE;

	if (state->combined_diff && !state->after_diff && data[0] == ' ' && data[1] != ' ')
		state->reading_diff_stat = TRUE;

	if (state->reading_diff_stat) {
		size_t len = strlen(data);
		char *pipe = strchr(data, '|');
		bool has_histogram = data[len - 1] == '-' || data[len - 1] == '+';
		bool has_bin_diff = pipe && strstr(pipe, "Bin") && strstr(pipe, "->");
		bool has_rename = data[len - 1] == '0' && (strstr(data, "=>") || !strncmp(data, " ...", 4));
		bool has_no_change = pipe && strstr(pipe, " 0");

		if (pipe && (has_histogram || has_bin_diff || has_rename || has_no_change)) {
			return add_line_text(view, data, LINE_DIFF_STAT) != NULL;
		} else {
			state->reading_diff_stat = FALSE;
		}

	} else if (!strcmp(data, "---")) {
		state->reading_diff_stat = TRUE;
	}

	if (!state->after_commit_title && !prefixcmp(data, "    ")) {
		struct line *line = add_line_text(view, data, LINE_DEFAULT);

		if (line)
			line->user_flags |= DIFF_LINE_COMMIT_TITLE;
		state->after_commit_title = TRUE;
		return line != NULL;
	}

	if (type == LINE_DIFF_HEADER) {
		const int len = STRING_SIZE("diff --");

		state->after_diff = TRUE;
		if (!strncmp(data + len, "combined ", strlen("combined ")) ||
		    !strncmp(data + len, "cc ", strlen("cc ")))
			state->combined_diff = TRUE;

	} else if (type == LINE_PP_MERGE) {
		state->combined_diff = TRUE;
	}

	/* ADD2 and DEL2 are only valid in combined diff hunks */
	if (!state->combined_diff && (type == LINE_DIFF_ADD2 || type == LINE_DIFF_DEL2))
		type = LINE_DEFAULT;

	return pager_common_read(view, data, type);
}

static bool
diff_find_stat_entry(struct view *view, struct line *line, enum line_type type)
{
	struct line *marker = find_next_line_by_type(view, line, type);

	return marker &&
		line == find_prev_line_by_type(view, marker, LINE_DIFF_HEADER);
}

enum request
diff_common_enter(struct view *view, enum request request, struct line *line)
{
	if (line->type == LINE_DIFF_STAT) {
		int file_number = 0;

		while (view_has_line(view, line) && line->type == LINE_DIFF_STAT) {
			file_number++;
			line--;
		}

		for (line = view->line; view_has_line(view, line); line++) {
			line = find_next_line_by_type(view, line, LINE_DIFF_HEADER);
			if (!line)
				break;

			if (diff_find_stat_entry(view, line, LINE_DIFF_INDEX)
			    || diff_find_stat_entry(view, line, LINE_DIFF_SIMILARITY)) {
				if (file_number == 1) {
					break;
				}
				file_number--;
			}
		}

		if (!line) {
			report("Failed to find file diff");
			return REQ_NONE;
		}

		select_view_line(view, line - view->line);
		report_clear();
		return REQ_NONE;

	} else {
		return pager_request(view, request, line);
	}
}

static bool
diff_common_draw_part(struct view *view, enum line_type *type, char **text, char c, enum line_type next_type)
{
	char *sep = strchr(*text, c);

	if (sep != NULL) {
		*sep = 0;
		draw_text(view, *type, *text);
		*sep = c;
		*text = sep;
		*type = next_type;
	}

	return sep != NULL;
}

bool
diff_common_draw(struct view *view, struct line *line, unsigned int lineno)
{
	char *text = line->data;
	enum line_type type = line->type;

	if (draw_lineno(view, lineno))
		return TRUE;

	if (line->wrapped && draw_text(view, LINE_DELIMITER, "+"))
		return TRUE;

	if (type == LINE_DIFF_STAT) {
		diff_common_draw_part(view, &type, &text, '|', LINE_DEFAULT);
		if (diff_common_draw_part(view, &type, &text, 'B', LINE_DEFAULT)) {
			/* Handle binary diffstat: Bin <deleted> -> <added> bytes */
			diff_common_draw_part(view, &type, &text, ' ', LINE_DIFF_DEL);
			diff_common_draw_part(view, &type, &text, '-', LINE_DEFAULT);
			diff_common_draw_part(view, &type, &text, ' ', LINE_DIFF_ADD);
			diff_common_draw_part(view, &type, &text, 'b', LINE_DEFAULT);

		} else {
			diff_common_draw_part(view, &type, &text, '+', LINE_DIFF_ADD);
			diff_common_draw_part(view, &type, &text, '-', LINE_DIFF_DEL);
		}
	}

	if (line->user_flags & DIFF_LINE_COMMIT_TITLE)
		draw_commit_title(view, text, 4);
	else
		draw_text(view, type, text);
	return TRUE;
}

static bool
diff_read(struct view *view, char *data)
{
	struct diff_state *state = view->private;

	if (!data) {
		/* Fall back to retry if no diff will be shown. */
		if (view->lines == 0 && opt_file_argv) {
			int pos = argv_size(view->argv)
				- argv_size(opt_file_argv) - 1;

			if (pos > 0 && !strcmp(view->argv[pos], "--")) {
				for (; view->argv[pos]; pos++) {
					free((void *) view->argv[pos]);
					view->argv[pos] = NULL;
				}

				if (view->pipe)
					io_done(view->pipe);
				if (io_run(&view->io, IO_RD, view->dir, opt_env, view->argv))
					return FALSE;
			}
		}
		return TRUE;
	}

	return diff_common_read(view, data, state);
}

static bool
diff_blame_line(const char *ref, const char *file, unsigned long lineno,
		struct blame_header *header, struct blame_commit *commit)
{
	char line_arg[SIZEOF_STR];
	const char *blame_argv[] = {
		"git", "blame", encoding_arg, "-p", line_arg, ref, "--", file, NULL
	};
	struct io io;
	bool ok = FALSE;
	char *buf;

	if (!string_format(line_arg, "-L%ld,+1", lineno))
		return FALSE;

	if (!io_run(&io, IO_RD, repo.cdup, opt_env, blame_argv))
		return FALSE;

	while ((buf = io_get(&io, '\n', TRUE))) {
		if (header) {
			if (!parse_blame_header(header, buf, 9999999))
				break;
			header = NULL;

		} else if (parse_blame_info(commit, buf)) {
			ok = commit->filename != NULL;
			break;
		}
	}

	if (io_error(&io))
		ok = FALSE;

	io_done(&io);
	return ok;
}

unsigned int
diff_get_lineno(struct view *view, struct line *line)
{
	const struct line *header, *chunk;
	unsigned int lineno;
	struct chunk_header chunk_header;

	/* Verify that we are after a diff header and one of its chunks */
	header = find_prev_line_by_type(view, line, LINE_DIFF_HEADER);
	chunk = find_prev_line_by_type(view, line, LINE_DIFF_CHUNK);
	if (!header || !chunk || chunk < header)
		return 0;

	/*
	 * In a chunk header, the number after the '+' sign is the number of its
	 * following line, in the new version of the file. We increment this
	 * number for each non-deletion line, until the given line position.
	 */
	if (!parse_chunk_header(&chunk_header, chunk->data))
		return 0;

	lineno = chunk_header.new.position;
	chunk++;
	while (chunk++ < line)
		if (chunk->type != LINE_DIFF_DEL &&
		    chunk->type != LINE_DIFF_DEL2)
			lineno++;

	return lineno;
}

static enum request
diff_trace_origin(struct view *view, struct line *line)
{
	struct line *diff = find_prev_line_by_type(view, line, LINE_DIFF_HEADER);
	struct line *chunk = find_prev_line_by_type(view, line, LINE_DIFF_CHUNK);
	const char *chunk_data;
	int chunk_marker = line->type == LINE_DIFF_DEL ? '-' : '+';
	unsigned long lineno = 0;
	const char *file = NULL;
	char ref[SIZEOF_REF];
	struct blame_header header;
	struct blame_commit commit;

	if (!diff || !chunk || chunk == line) {
		report("The line to trace must be inside a diff chunk");
		return REQ_NONE;
	}

	for (; diff < line && !file; diff++) {
		const char *data = diff->data;

		if (!prefixcmp(data, "--- a/")) {
			file = data + STRING_SIZE("--- a/");
			break;
		}
	}

	if (diff == line || !file) {
		report("Failed to read the file name");
		return REQ_NONE;
	}

	chunk_data = chunk->data;

	if (!parse_chunk_lineno(&lineno, chunk_data, chunk_marker)) {
		report("Failed to read the line number");
		return REQ_NONE;
	}

	if (lineno == 0) {
		report("This is the origin of the line");
		return REQ_NONE;
	}

	for (chunk += 1; chunk < line; chunk++) {
		if (chunk->type == LINE_DIFF_ADD) {
			lineno += chunk_marker == '+';
		} else if (chunk->type == LINE_DIFF_DEL) {
			lineno += chunk_marker == '-';
		} else {
			lineno++;
		}
	}

	if (chunk_marker == '+')
		string_copy(ref, view->vid);
	else
		string_format(ref, "%s^", view->vid);

	if (string_rev_is_null(ref)) {
		string_ncopy(view->env->file, file, strlen(file));
		string_copy(view->env->ref, "");
		view->env->lineno = lineno - 1;

	} else {
		if (!diff_blame_line(ref, file, lineno, &header, &commit)) {
			report("Failed to read blame data");
			return REQ_NONE;
		}

		string_ncopy(view->env->file, commit.filename, strlen(commit.filename));
		string_copy(view->env->ref, header.id);
		view->env->lineno = header.orig_lineno - 1;
	}

	return REQ_VIEW_BLAME;
}

const char *
diff_get_pathname(struct view *view, struct line *line)
{
	const struct line *header;
	const char *dst = NULL;
	const char *prefixes[] = { " b/", "cc ", "combined " };
	int i;

	header = find_prev_line_by_type(view, line, LINE_DIFF_HEADER);
	if (!header)
		return NULL;

	for (i = 0; i < ARRAY_SIZE(prefixes) && !dst; i++)
		dst = strstr(header->data, prefixes[i]);

	return dst ? dst + strlen(prefixes[--i]) : NULL;
}

enum request
diff_common_edit(struct view *view, enum request request, struct line *line)
{
	const char *file = diff_get_pathname(view, line);
	char path[SIZEOF_STR];
	bool has_path = file && string_format(path, "%s%s", repo.cdup, file);

	if (has_path && access(path, R_OK)) {
		report("Failed to open file: %s", file);
		return REQ_NONE;
	}

	open_editor(file, diff_get_lineno(view, line));
	return REQ_NONE;
}

static enum request
diff_request(struct view *view, enum request request, struct line *line)
{
	switch (request) {
	case REQ_VIEW_BLAME:
		return diff_trace_origin(view, line);

	case REQ_EDIT:
		return diff_common_edit(view, request, line);

	case REQ_ENTER:
		return diff_common_enter(view, request, line);

	case REQ_REFRESH:
		if (string_rev_is_null(view->vid))
			refresh_view(view);
		else
			reload_view(view);
		return REQ_NONE;

	default:
		return pager_request(view, request, line);
	}
}

static void
diff_select(struct view *view, struct line *line)
{
	if (line->type == LINE_DIFF_STAT) {
		string_format(view->ref, "Press '%s' to jump to file diff",
			      get_view_key(view, REQ_ENTER));
	} else {
		const char *file = diff_get_pathname(view, line);

		if (file) {
			string_format(view->ref, "Changes to '%s'", file);
			string_format(view->env->file, "%s", file);
			view->env->blob[0] = 0;
		} else {
			string_ncopy(view->ref, view->ops->id, strlen(view->ops->id));
			pager_select(view, line);
		}
	}
}

struct view_ops diff_ops = {
	"line",
	{ "diff" },
	argv_env.commit,
	VIEW_DIFF_LIKE | VIEW_ADD_DESCRIBE_REF | VIEW_ADD_PAGER_REFS | VIEW_FILE_FILTER | VIEW_REFRESH,
	sizeof(struct diff_state),
	diff_open,
	diff_read,
	diff_common_draw,
	diff_request,
	pager_grep,
	diff_select,
};

/* vim: set ts=8 sw=8 noexpandtab: */
