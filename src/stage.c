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
#include "tig/argv.h"
#include "tig/options.h"
#include "tig/parse.h"
#include "tig/display.h"
#include "tig/prompt.h"
#include "tig/view.h"
#include "tig/draw.h"
#include "tig/git.h"
#include "tig/pager.h"
#include "tig/diff.h"
#include "tig/status.h"

struct stage_state {
	struct diff_state diff;
};

static inline bool
stage_diff_done(struct line *line, struct line *end)
{
	return line >= end ||
	       line->type == LINE_DIFF_CHUNK ||
	       line->type == LINE_DIFF_HEADER;
}

static bool
stage_diff_write(struct io *io, struct line *line, struct line *end)
{
	while (line < end) {
		if (!io_write(io, line->data, strlen(line->data)) ||
		    !io_write(io, "\n", 1))
			return FALSE;
		line++;
		if (stage_diff_done(line, end))
			break;
	}

	return TRUE;
}

static bool
stage_diff_single_write(struct io *io, bool staged,
			struct line *line, struct line *single, struct line *end)
{
	enum line_type write_as_normal = staged ? LINE_DIFF_ADD : LINE_DIFF_DEL;
	enum line_type ignore = staged ? LINE_DIFF_DEL : LINE_DIFF_ADD;

	while (line < end) {
		const char *prefix = "";
		const char *data = line->data;

		if (line == single) {
			/* Write the complete line. */

		} else if (line->type == write_as_normal) {
			prefix = " ";
			data = data + 1;

		} else if (line->type == ignore) {
			data = NULL;
		}

		if (data && !io_printf(io, "%s%s\n", prefix, data))
			return FALSE;

		line++;
		if (stage_diff_done(line, end))
			break;
	}

	return TRUE;
}

static bool
stage_apply_line(struct io *io, struct line *diff_hdr, struct line *chunk, struct line *single, struct line *end)
{
	struct chunk_header header;
	bool staged = stage_line_type == LINE_STAT_STAGED;
	int diff = single->type == LINE_DIFF_DEL ? -1 : 1;

	if (!parse_chunk_header(&header, chunk->data))
		return FALSE;

	if (staged)
		header.old.lines = header.new.lines - diff;
	else
		header.new.lines = header.old.lines + diff;

	return stage_diff_write(io, diff_hdr, chunk) &&
	       io_printf(io, "@@ -%lu,%lu +%lu,%lu @@\n",
		       header.old.position, header.old.lines,
		       header.new.position, header.new.lines) &&
	       stage_diff_single_write(io, staged, chunk + 1, single, end);
}

static bool
stage_apply_chunk(struct view *view, struct line *chunk, struct line *single, bool revert)
{
	const char *apply_argv[SIZEOF_ARG] = {
		"git", "apply", "--whitespace=nowarn", NULL
	};
	struct line *diff_hdr;
	struct io io;
	int argc = 3;

	diff_hdr = find_prev_line_by_type(view, chunk, LINE_DIFF_HEADER);
	if (!diff_hdr)
		return FALSE;

	if (!revert)
		apply_argv[argc++] = "--cached";
	if (revert || stage_line_type == LINE_STAT_STAGED)
		apply_argv[argc++] = "-R";
	apply_argv[argc++] = "-";
	apply_argv[argc++] = NULL;
	if (!io_run(&io, IO_WR, repo.cdup, opt_env, apply_argv))
		return FALSE;

	if (single != NULL) {
		if (!stage_apply_line(&io, diff_hdr, chunk, single, view->line + view->lines))
			chunk = NULL;

	} else {
		if (!stage_diff_write(&io, diff_hdr, chunk) ||
		    !stage_diff_write(&io, chunk, view->line + view->lines))
			chunk = NULL;
	}

	io_done(&io);

	return chunk ? TRUE : FALSE;
}

static bool
stage_update(struct view *view, struct line *line, bool single)
{
	struct line *chunk = NULL;

	if (!is_initial_commit() && stage_line_type != LINE_STAT_UNTRACKED)
		chunk = find_prev_line_by_type(view, line, LINE_DIFF_CHUNK);

	if (chunk) {
		if (!stage_apply_chunk(view, chunk, single ? line : NULL, FALSE)) {
			report("Failed to apply chunk");
			return FALSE;
		}

	} else if (!stage_status.status) {
		view = view->parent;

		for (line = view->line; view_has_line(view, line); line++)
			if (line->type == stage_line_type)
				break;

		if (!status_update_files(view, line + 1)) {
			report("Failed to update files");
			return FALSE;
		}

	} else if (!status_update_file(&stage_status, stage_line_type)) {
		report("Failed to update file");
		return FALSE;
	}

	return TRUE;
}

static bool
stage_revert(struct view *view, struct line *line)
{
	struct line *chunk = NULL;

	if (!is_initial_commit() && stage_line_type == LINE_STAT_UNSTAGED)
		chunk = find_prev_line_by_type(view, line, LINE_DIFF_CHUNK);

	if (chunk) {
		if (!prompt_yesno("Are you sure you want to revert changes?"))
			return FALSE;

		if (!stage_apply_chunk(view, chunk, NULL, TRUE)) {
			report("Failed to revert chunk");
			return FALSE;
		}
		return TRUE;

	} else {
		return status_revert(stage_status.status ? &stage_status : NULL,
				     stage_line_type, FALSE);
	}
}

static struct line *
stage_insert_chunk(struct view *view, struct chunk_header *header,
		   struct line *from, struct line *to, struct line *last_unchanged_line)
{
	char buf[SIZEOF_STR];
	char *chunk_line;
	unsigned long from_lineno = last_unchanged_line - view->line;
	unsigned long to_lineno = to - view->line;
	unsigned long after_lineno = to_lineno;

	if (!string_format(buf, "@@ -%lu,%lu +%lu,%lu @@",
			header->old.position, header->old.lines,
			header->new.position, header->new.lines))
		return NULL;

	chunk_line = strdup(buf);
	if (!chunk_line)
		return NULL;

	free(from->data);
	from->data = chunk_line;

	if (!to)
		return from;

	if (!add_line_at(view, after_lineno++, buf, LINE_DIFF_CHUNK, strlen(buf) + 1, FALSE))
		return NULL;

	while (from_lineno < to_lineno) {
		struct line *line = &view->line[from_lineno++];

		if (!add_line_at(view, after_lineno++, line->data, line->type, strlen(line->data) + 1, FALSE))
			return FALSE;
	}

	return view->line + after_lineno;
}

static void
stage_split_chunk(struct view *view, struct line *chunk_start)
{
	struct chunk_header header;
	struct line *last_changed_line = NULL, *last_unchanged_line = NULL, *pos;
	int chunks = 0;

	if (!chunk_start || !parse_chunk_header(&header, chunk_start->data)) {
		report("Failed to parse chunk header");
		return;
	}

	header.old.lines = header.new.lines = 0;

	for (pos = chunk_start + 1; view_has_line(view, pos); pos++) {
		const char *chunk_line = pos->data;

		if (*chunk_line == '@' || *chunk_line == '\\')
			break;

		if (*chunk_line == ' ') {
			header.old.lines++;
			header.new.lines++;
			if (last_unchanged_line < last_changed_line)
				last_unchanged_line = pos;
			continue;
		}

		if (last_changed_line && last_changed_line < last_unchanged_line) {
			unsigned long chunk_start_lineno = pos - view->line;
			unsigned long diff = pos - last_unchanged_line;

			pos = stage_insert_chunk(view, &header, chunk_start, pos, last_unchanged_line);

			header.old.position += header.old.lines - diff;
			header.new.position += header.new.lines - diff;
			header.old.lines = header.new.lines = diff;

			chunk_start = view->line + chunk_start_lineno;
			last_changed_line = last_unchanged_line = NULL;
			chunks++;
		}

		if (*chunk_line == '-') {
			header.old.lines++;
			last_changed_line = pos;
		} else if (*chunk_line == '+') {
			header.new.lines++;
			last_changed_line = pos;
		}
	}

	if (chunks) {
		stage_insert_chunk(view, &header, chunk_start, NULL, NULL);
		redraw_view(view);
		report("Split the chunk in %d", chunks + 1);
	} else {
		report("The chunk cannot be split");
	}
}

static enum request
stage_request(struct view *view, enum request request, struct line *line)
{
	switch (request) {
	case REQ_STATUS_UPDATE:
		if (!stage_update(view, line, FALSE))
			return REQ_NONE;
		break;

	case REQ_STATUS_REVERT:
		if (!stage_revert(view, line))
			return REQ_NONE;
		break;

	case REQ_STAGE_UPDATE_LINE:
		if (stage_line_type == LINE_STAT_UNTRACKED ||
		    stage_status.status == 'A') {
			report("Staging single lines is not supported for new files");
			return REQ_NONE;
		}
		if (line->type != LINE_DIFF_DEL && line->type != LINE_DIFF_ADD) {
			report("Please select a change to stage");
			return REQ_NONE;
		}
		if (!stage_update(view, line, TRUE))
			return REQ_NONE;
		break;


	case REQ_STAGE_SPLIT_CHUNK:
		if (stage_line_type == LINE_STAT_UNTRACKED ||
		    !(line = find_prev_line_by_type(view, line, LINE_DIFF_CHUNK))) {
			report("No chunks to split in sight");
			return REQ_NONE;
		}
		stage_split_chunk(view, line);
		return REQ_NONE;

	case REQ_EDIT:
		if (!stage_status.new.name[0])
			return diff_common_edit(view, request, line);

		if (stage_status.status == 'D') {
			report("File has been deleted.");
			return REQ_NONE;
		}

		if (stage_line_type == LINE_STAT_UNTRACKED) {
			open_editor(stage_status.new.name, (line - view->line) + 1);
		} else {
			open_editor(stage_status.new.name, diff_get_lineno(view, line));
		}
		break;

	case REQ_REFRESH:
		/* Reload everything(including current branch information) ... */
		load_refs(TRUE);
		break;

	case REQ_VIEW_BLAME:
		if (stage_line_type == LINE_STAT_UNTRACKED) {
			report("Nothing to blame here");
			return REQ_NONE;
		}

		if (stage_status.new.name[0]) {
			string_copy(view->env->file, stage_status.new.name);
		} else {
			const char *file = diff_get_pathname(view, line);

			if (file)
				string_ncopy(view->env->file, file, strlen(file));
		}

		view->env->ref[0] = 0;
		view->env->lineno = diff_get_lineno(view, line);
		if (view->env->lineno > 0)
			view->env->lineno--;
		return request;

	case REQ_ENTER:
		return diff_common_enter(view, request, line);

	default:
		return request;
	}

	refresh_view(view->parent);

	/* Check whether the staged entry still exists, and close the
	 * stage view if it doesn't. */
	if (!status_exists(view->parent, &stage_status, stage_line_type)) {
		status_restore(view->parent);
		return REQ_VIEW_CLOSE;
	}

	refresh_view(view);

	return REQ_NONE;
}

static bool
stage_open(struct view *view, enum open_flags flags)
{
	const char *no_head_diff_argv[] = {
		GIT_DIFF_STAGED_INITIAL(encoding_arg, diff_context_arg(), ignore_space_arg(),
			stage_status.new.name)
	};
	const char *index_show_argv[] = {
		GIT_DIFF_STAGED(encoding_arg, diff_context_arg(), ignore_space_arg(),
			stage_status.old.name, stage_status.new.name)
	};
	const char *files_show_argv[] = {
		GIT_DIFF_UNSTAGED(encoding_arg, diff_context_arg(), ignore_space_arg(),
			stage_status.old.name, stage_status.new.name)
	};
	/* Diffs for unmerged entries are empty when passing the new
	 * path, so leave out the new path. */
	const char *files_unmerged_argv[] = {
		"git", "diff-files", encoding_arg, "--root", "--patch-with-stat",
			diff_context_arg(), ignore_space_arg(), "--",
			stage_status.old.name, NULL
	};
	static const char *file_argv[] = { repo.cdup, stage_status.new.name, NULL };
	const char **argv = NULL;

	if (!stage_line_type) {
		report("No stage content, press %s to open the status view and choose file",
			get_view_key(view, REQ_VIEW_STATUS));
		return FALSE;
	}

	if (!pager_column_init(view))
		return FALSE;

	view->encoding = NULL;

	switch (stage_line_type) {
	case LINE_STAT_STAGED:
		if (is_initial_commit()) {
			argv = no_head_diff_argv;
		} else {
			argv = index_show_argv;
		}
		break;

	case LINE_STAT_UNSTAGED:
		if (stage_status.status != 'U')
			argv = files_show_argv;
		else
			argv = files_unmerged_argv;
		break;

	case LINE_STAT_UNTRACKED:
		argv = file_argv;
		view->encoding = get_path_encoding(stage_status.old.name, default_encoding);
		break;

	case LINE_STAT_HEAD:
	default:
		die("line type %d not handled in switch", stage_line_type);
	}

	if (!status_stage_info(view->ref, stage_line_type, &stage_status)
		|| !argv_copy(&view->argv, argv)) {
		report("Failed to open staged view");
		return FALSE;
	}

	view->vid[0] = 0;
	view->dir = repo.cdup;
	return begin_update(view, NULL, NULL, flags);
}

static bool
stage_read(struct view *view, char *data)
{
	struct stage_state *state = view->private;

	if (stage_line_type == LINE_STAT_UNTRACKED)
		return pager_common_read(view, data, LINE_DEFAULT, NULL);

	if (data && diff_common_read(view, data, &state->diff))
		return TRUE;

	return pager_read(view, data);
}

static struct view_ops stage_ops = {
	"line",
	argv_env.status,
	VIEW_DIFF_LIKE | VIEW_REFRESH,
	sizeof(struct stage_state),
	stage_open,
	stage_read,
	view_column_draw,
	stage_request,
	view_column_grep,
	pager_select,
	NULL,
	pager_get_column_data,
};

DEFINE_VIEW(stage);

/* vim: set ts=8 sw=8 noexpandtab: */
