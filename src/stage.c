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
#include "tig/main.h"
#include "tig/stage.h"

static struct status stage_status;
static enum line_type stage_line_type;

void
open_stage_view(struct view *prev, struct status *status, enum line_type type, enum open_flags flags)
{
	if (type) {
		stage_line_type = type;
		if (status)
			stage_status = *status;
		else
			memset(&stage_status, 0, sizeof(stage_status));
	}

	open_view(prev, &stage_view, flags);
}

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
		const char *text = box_text(line);

		if (!io_write(io, text, strlen(text)) ||
		    !io_write(io, "\n", 1))
			return false;
		line++;
		if (stage_diff_done(line, end))
			break;
	}

	return true;
}

static bool
stage_diff_single_write(struct io *io, bool staged,
			struct line *line, struct line *single, struct line *end)
{
	enum line_type write_as_normal = staged ? LINE_DIFF_ADD : LINE_DIFF_DEL;
	enum line_type ignore = staged ? LINE_DIFF_DEL : LINE_DIFF_ADD;

	while (line < end) {
		const char *prefix = "";
		const char *data = box_text(line);

		if (line == single) {
			/* Write the complete line. */

		} else if (line->type == write_as_normal) {
			prefix = " ";
			data = data + 1;

		} else if (line->type == ignore) {
			data = NULL;
		}

		if (data && !io_printf(io, "%s%s\n", prefix, data))
			return false;

		line++;
		if (stage_diff_done(line, end))
			break;
	}

	return true;
}

static bool
stage_apply_line(struct io *io, struct line *diff_hdr, struct line *chunk, struct line *single, struct line *end)
{
	struct chunk_header header;
	bool staged = stage_line_type == LINE_STAT_STAGED;
	int diff = single->type == LINE_DIFF_DEL ? -1 : 1;

	if (!parse_chunk_header(&header, box_text(chunk)))
		return false;

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
		return false;

	if (!revert)
		apply_argv[argc++] = "--cached";
	if (revert || stage_line_type == LINE_STAT_STAGED)
		apply_argv[argc++] = "-R";
	apply_argv[argc++] = "-";
	apply_argv[argc++] = NULL;
	if (!io_run(&io, IO_WR, repo.exec_dir, NULL, apply_argv))
		return false;

	if (single != NULL) {
		if (!stage_apply_line(&io, diff_hdr, chunk, single, view->line + view->lines))
			chunk = NULL;

	} else {
		if (!stage_diff_write(&io, diff_hdr, chunk) ||
		    !stage_diff_write(&io, chunk, view->line + view->lines))
			chunk = NULL;
	}

	return io_done(&io) && chunk;
}

static bool
stage_update_files(struct view *view, enum line_type type)
{
	struct line *line;

	if (view->parent != &status_view) {
		bool updated = false;

		for (line = view->line; (line = find_next_line_by_type(view, line, LINE_DIFF_CHUNK)); line++) {
			if (!stage_apply_chunk(view, line, NULL, false)) {
				report("Failed to apply chunk");
				return false;
			}
			updated = true;
		}

		return updated;
	}

	view = view->parent;
	line = find_next_line_by_type(view, view->line, type);
	return line && status_update_files(view, line + 1);
}

static bool
stage_update(struct view *view, struct line *line, bool single)
{
	struct line *chunk = NULL;

	if (!is_initial_commit() && stage_line_type != LINE_STAT_UNTRACKED)
		chunk = find_prev_line_by_type(view, line, LINE_DIFF_CHUNK);

	if (chunk) {
		if (!stage_apply_chunk(view, chunk, single ? line : NULL, false)) {
			report("Failed to apply chunk");
			return false;
		}

	} else if (!stage_status.status) {
		if (!stage_update_files(view, stage_line_type)) {
			report("Failed to update files");
			return false;
		}

	} else if (!status_update_file(&stage_status, stage_line_type)) {
		report("Failed to update file");
		return false;
	}

	return true;
}

static bool
stage_revert(struct view *view, struct line *line)
{
	struct line *chunk = NULL;

	if (!is_initial_commit() && stage_line_type == LINE_STAT_UNSTAGED)
		chunk = find_prev_line_by_type(view, line, LINE_DIFF_CHUNK);

	if (chunk) {
		if (!prompt_yesno("Are you sure you want to revert changes?"))
			return false;

		if (!stage_apply_chunk(view, chunk, NULL, true)) {
			report("Failed to revert chunk");
			return false;
		}
		return true;

	} else {
		return status_revert(stage_status.status ? &stage_status : NULL,
				     stage_line_type, false);
	}
}

static struct line *
stage_insert_chunk(struct view *view, struct chunk_header *header,
		   struct line *from, struct line *to, struct line *last_unchanged_line)
{
	struct box *box;
	unsigned long from_lineno = last_unchanged_line - view->line;
	unsigned long to_lineno = to - view->line;
	unsigned long after_lineno = to_lineno;
	int i;

	box = from->data;
	for (i = 0; i < box->cells; i++)
		box->cell[i].length = 0;

	if (!append_line_format(view, from, "@@ -%lu,%lu +%lu,%lu @@",
			header->old.position, header->old.lines,
			header->new.position, header->new.lines))
		return NULL;

	if (!to)
		return from;

	// Next diff chunk line
	if (!add_line_text_at(view, after_lineno++, "", LINE_DIFF_CHUNK, 1))
		return NULL;

	while (from_lineno < to_lineno) {
		struct line *line = &view->line[from_lineno++];
		const char *text = box_text(line);

		if (!add_line_text_at(view, after_lineno++, text, line->type, 1))
			return false;
	}

	return view->line + after_lineno;
}

static void
stage_split_chunk(struct view *view, struct line *chunk_start)
{
	struct chunk_header header;
	struct line *last_changed_line = NULL, *last_unchanged_line = NULL, *pos;
	int chunks = 0;

	if (!chunk_start || !parse_chunk_header(&header, box_text(chunk_start))) {
		report("Failed to parse chunk header");
		return;
	}

	header.old.lines = header.new.lines = 0;

	for (pos = chunk_start + 1; view_has_line(view, pos); pos++) {
		const char *chunk_line = box_text(pos);

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

static bool
stage_exists(struct view *view, struct status *status, enum line_type type)
{
	struct view *parent = view->parent;

	if (parent == &status_view)
		return status_exists(parent, status, type);

	if (parent == &main_view)
		return main_status_exists(parent, type);

	return false;
}

static bool
stage_chunk_is_wrapped(struct view *view, struct line *line)
{
	struct line *pos = find_prev_line_by_type(view, line, LINE_DIFF_HEADER);

	if (!opt_wrap_lines || !pos)
		return false;

	for (; pos <= line; pos++)
		if (pos->wrapped)
			return true;

	return false;
}

static bool
find_deleted_line_in_head(struct view *view, struct line *line) {
	struct io io;
	struct buffer buffer;
	unsigned long line_number_in_head, line_number = 0;
	long bias_by_staged_changes = 0;
	char buf[SIZEOF_STR] = "";
	char file_in_head_pathspec[sizeof("HEAD:") + SIZEOF_STR],
		file_in_index_pathspec[sizeof(":") + SIZEOF_STR];
	const char *file_in_head = NULL;
	const char *ls_tree_argv[] = {
		"git", "ls-tree", "-z", "HEAD", view->env->file, NULL
	};
	const char *diff_argv[] = {
		"git", "diff", "--root", file_in_head_pathspec, file_in_index_pathspec,
		"--no-color", NULL
	};

	if (line->type != LINE_DIFF_DEL)
		return false;

	// Check if the file exists in HEAD.
	io_run_buf(ls_tree_argv, buf, sizeof(buf), repo.exec_dir, false);
	if (buf[0]) {
		file_in_head = view->env->file;
	} else { // The file might might be renamed in the index. Find its old name.
		struct status file_status;
		const char *diff_index_argv[] = {
			"git", "diff-index", "--root", "--cached", "-C",
			"--diff-filter=ACR", "-z", "HEAD", NULL
		};
		if (!io_run(&io, IO_RD, repo.exec_dir, NULL, diff_index_argv) || io.status)
			return false;
		while (io_get(&io, &buffer, 0, true)) {
			if (!status_get_diff(&file_status, buffer.data, buffer.size))
				return false;
			if (file_status.status != 'A') {
				if (!io_get(&io, &buffer, 0, true))
					return false;
				string_ncopy(file_status.old.name, buffer.data, buffer.size);
			}
			if (!io_get(&io, &buffer, 0, true))
				return false;
			string_ncopy(file_status.new.name, buffer.data, buffer.size);
			if (strcmp(file_status.new.name, view->env->file))
				continue;
			// Quit if the file does not exist in HEAD.
			if (file_status.status == 'A') {
				return false;
			}
			file_in_head = file_status.old.name;
			break;
		}
	}

	if (!file_in_head)
		return false;

	// We want to compute the line number in HEAD. The current view is a diff
	// of (un)staged changes on top of HEAD.
	line_number_in_head = diff_get_lineno(view, line, /*old=*/true);
	assert(line_number_in_head);

	// When looking at staged changes, we already have the correct
	// line number in HEAD.
	if (stage_line_type == LINE_STAT_STAGED)
		goto found_line;

	// If we are in an unstaged diff, we also need to take into
	// account the staged changes to this file, since they happened
	// between HEAD and our diff.
	sprintf(file_in_head_pathspec, "HEAD:%s", file_in_head);
	sprintf(file_in_index_pathspec, ":%s", view->env->file);
	if (!io_run(&io, IO_RD, repo.exec_dir, NULL, diff_argv) || io.status)
		return false;
	// line_number_in_head is still the line number in the staged
	// version of the file. Go through the staged changes up to our
	// line number and count the additions and deletions on the way,
	// to compute the line number before the staged changes.
	while (line_number < line_number_in_head && io_get(&io, &buffer, '\n', true)) {
		enum line_type type = get_line_type(buffer.data);
		if (type == LINE_DIFF_CHUNK) {
			struct chunk_header header;
			if (!parse_chunk_header(&header, buffer.data))
				return false;
			line_number = header.new.position;
			continue;
		}
		if (!line_number) {
			continue;
		}
		if (type == LINE_DIFF_DEL) {
			bias_by_staged_changes--;
			continue;
		}
		assert(type == LINE_DIFF_ADD || type == LINE_DEFAULT ||
		       // These are just context lines that happen to start with [-+].
		       type == LINE_DIFF_ADD2 || type == LINE_DIFF_DEL2);
		if (type == LINE_DIFF_ADD)
			bias_by_staged_changes++;
		line_number++;
	}

	line_number_in_head -= bias_by_staged_changes;

found_line:
	if (file_in_head != view->env->file)
		string_ncopy(view->env->file, file_in_head, strlen(file_in_head));
	view->env->goto_lineno = line_number_in_head;
	return true;
}

static enum request
stage_request(struct view *view, enum request request, struct line *line)
{
	switch (request) {
	case REQ_STATUS_UPDATE:
		if (!stage_update(view, line, false))
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
		if (stage_chunk_is_wrapped(view, line)) {
			report("Staging is not supported for wrapped lines");
			return REQ_NONE;
		}
		if (!stage_update(view, line, true))
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
			open_editor(stage_status.new.name, diff_get_lineno(view, line, false));
		}
		break;

	case REQ_REFRESH:
		/* Reload everything(including current branch information) ... */
		load_refs(true);
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
		if (find_deleted_line_in_head(view, line))
			string_copy(view->env->ref, "HEAD");
		else
			view->env->goto_lineno = diff_get_lineno(view, line, false);
		if (view->env->goto_lineno > 0)
			view->env->goto_lineno--;
		return request;

	case REQ_ENTER:
		return diff_common_enter(view, request, line);

	default:
		return request;
	}

	/* Check whether the staged entry still exists, and close the
	 * stage view if it doesn't. */
	if (view->parent && !stage_exists(view, &stage_status, stage_line_type)) {
		stage_line_type = 0;
		return REQ_VIEW_CLOSE;
	}

	refresh_view(view);

	return REQ_NONE;
}

static void
stage_select(struct view *view, struct line *line)
{
	const char *changes_msg = stage_line_type == LINE_STAT_STAGED ? "Staged changes"
				: stage_line_type == LINE_STAT_UNSTAGED ? "Unstaged changes"
				: NULL;

	diff_common_select(view, line, changes_msg);
}

static enum status_code
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
			DIFF_ARGS, diff_context_arg(), ignore_space_arg(), "--",
			stage_status.old.name, NULL
	};
	static const char *file_argv[] = { repo.exec_dir, stage_status.new.name, NULL };
	const char **argv = NULL;
	struct stage_state *state = view->private;
	enum status_code code;

	if (!stage_line_type)
		return error("No stage content, press %s to open the status view and choose file",
			     get_view_key(view, REQ_VIEW_STATUS));

	view->encoding = NULL;

	switch (stage_line_type) {
	case LINE_STAT_STAGED:
		watch_register(&view->watch, WATCH_INDEX_STAGED);
		if (is_initial_commit()) {
			argv = no_head_diff_argv;
		} else {
			argv = index_show_argv;
		}
		break;

	case LINE_STAT_UNSTAGED:
		watch_register(&view->watch, WATCH_INDEX_UNSTAGED);
		if (stage_status.status != 'U')
			argv = files_show_argv;
		else
			argv = files_unmerged_argv;
		break;

	case LINE_STAT_UNTRACKED:
		watch_register(&view->watch, WATCH_INDEX_UNTRACKED);
		argv = file_argv;
		view->encoding = get_path_encoding(stage_status.old.name, default_encoding);
		break;

	default:
		die("line type %d not handled in switch", stage_line_type);
	}

	if (!status_stage_info(view->ref, stage_line_type, &stage_status))
		return error("Failed to open staged view");

	if (stage_line_type != LINE_STAT_UNTRACKED)
		diff_save_line(view, &state->diff, flags);

	view->vid[0] = 0;
	code = begin_update(view, repo.exec_dir, argv, flags);
	if (code == SUCCESS && stage_line_type != LINE_STAT_UNTRACKED) {
		struct stage_state *state = view->private;

		return diff_init_highlight(view, &state->diff);
	}

	return code;
}

static bool
stage_read(struct view *view, struct buffer *buf, bool force_stop)
{
	struct stage_state *state = view->private;

	if (stage_line_type == LINE_STAT_UNTRACKED)
		return pager_common_read(view, buf ? buf->data : NULL, LINE_DEFAULT, NULL);

	if (!buf) {
		if (!diff_done_highlight(&state->diff)) {
			report("Failed run the diff-highlight program: %s", opt_diff_highlight);
			return true;
		}
	}

	if (!buf && !view->lines && view->parent) {
		maximize_view(view->parent, true);
		return true;
	}

	if (!buf)
		diff_restore_line(view, &state->diff);

	if (buf && diff_common_read(view, buf->data, &state->diff))
		return true;

	return pager_read(view, buf, force_stop);
}

static struct view_ops stage_ops = {
	"line",
	argv_env.status,
	VIEW_DIFF_LIKE | VIEW_REFRESH | VIEW_FLEX_WIDTH,
	sizeof(struct stage_state),
	stage_open,
	stage_read,
	view_column_draw,
	stage_request,
	view_column_grep,
	stage_select,
	NULL,
	view_column_bit(LINE_NUMBER) | view_column_bit(TEXT),
	pager_get_column_data,
};

DEFINE_VIEW(stage);

/* vim: set ts=8 sw=8 noexpandtab: */
