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
#include "tig/refdb.h"
#include "tig/repo.h"
#include "tig/options.h"
#include "tig/parse.h"
#include "tig/display.h"
#include "tig/prompt.h"
#include "tig/view.h"
#include "tig/search.h"
#include "tig/draw.h"
#include "tig/git.h"
#include "tig/watch.h"
#include "tig/status.h"
#include "tig/main.h"
#include "tig/stage.h"

/*
 * Status backend
 */

static char status_onbranch[SIZEOF_STR];
static bool show_untracked_only = false;

void
open_status_view(struct view *prev, bool untracked_only, enum open_flags flags)
{
	if (show_untracked_only != untracked_only) {
		show_untracked_only = untracked_only;
		flags |= OPEN_RELOAD;
	}
	open_view(prev, &status_view, flags);
}

/* This should work even for the "On branch" line. */
static inline bool
status_has_none(struct view *view, struct line *line)
{
	return view_has_line(view, line) && !line[1].data;
}

/* Get fields from the diff line:
 * :100644 100644 06a5d6ae9eca55be2e0e585a152e6b1336f2b20e 0000000000000000000000000000000000000000 M
 */
inline bool
status_get_diff(struct status *file, const char *buf, size_t bufsize)
{
	const char *old_mode = buf +  1;
	const char *new_mode = buf +  8;
	const char *old_rev  = buf + 15;
	const char *new_rev  = buf + 56;
	const char *status   = buf + 97;

	if (bufsize < 98 ||
	    old_mode[-1] != ':' ||
	    new_mode[-1] != ' ' ||
	    old_rev[-1]  != ' ' ||
	    new_rev[-1]  != ' ' ||
	    status[-1]   != ' ')
		return false;

	file->status = *status;

	string_copy_rev(file->old.rev, old_rev);
	string_copy_rev(file->new.rev, new_rev);

	file->old.mode = strtoul(old_mode, NULL, 8);
	file->new.mode = strtoul(new_mode, NULL, 8);

	file->old.name[0] = file->new.name[0] = 0;

	return true;
}

static bool
status_run(struct view *view, const char *argv[], char status, enum line_type type)
{
	struct status *unmerged = NULL;
	struct buffer buf;
	struct io io;

	if (!io_run(&io, IO_RD, repo.exec_dir, NULL, argv))
		return false;

	add_line_nodata(view, type);

	while (io_get(&io, &buf, 0, true)) {
		struct line *line;
		struct status parsed = {0};
		struct status *file = &parsed;

		/* Parse diff info part. */
		if (status) {
			file->status = status;
			if (status == 'A')
				string_copy(file->old.rev, NULL_ID);

		} else {
			if (!status_get_diff(&parsed, buf.data, buf.size))
				goto error_out;

			if (!io_get(&io, &buf, 0, true))
				break;
		}

		/* Grab the old name for rename/copy. */
		if (!*file->old.name &&
		    (file->status == 'R' || file->status == 'C')) {
			string_ncopy(file->old.name, buf.data, buf.size);

			if (!io_get(&io, &buf, 0, true))
				break;
		}

		/* git-ls-files just delivers a NUL separated list of
		 * file names similar to the second half of the
		 * git-diff-* output. */
		string_ncopy(file->new.name, buf.data, buf.size);
		if (!*file->old.name)
			string_copy(file->old.name, file->new.name);

		/* Collapse all modified entries that follow an associated
		 * unmerged entry. */
		if (unmerged && !strcmp(unmerged->new.name, file->new.name)) {
			unmerged->status = 'U';
			unmerged = NULL;
			continue;
		}

		line = add_line_alloc(view, &file, type, 0, false);
		if (!line)
			goto error_out;
		*file = parsed;
		view_column_info_update(view, line);
		if (file->status == 'U')
			unmerged = file;
	}

	if (io_error(&io)) {
error_out:
		io_done(&io);
		return false;
	}

	if (!view->line[view->lines - 1].data) {
		add_line_nodata(view, LINE_STAT_NONE);
		if (type == LINE_STAT_STAGED)
			watch_apply(&view->watch, WATCH_INDEX_STAGED_NO);
		else if (type == LINE_STAT_UNSTAGED)
			watch_apply(&view->watch, WATCH_INDEX_UNSTAGED_NO);
		else if (type == LINE_STAT_UNTRACKED)
			watch_apply(&view->watch, WATCH_INDEX_UNTRACKED_NO);
	} else {
		if (type == LINE_STAT_STAGED)
			watch_apply(&view->watch, WATCH_INDEX_STAGED_YES);
		else if (type == LINE_STAT_UNSTAGED)
			watch_apply(&view->watch, WATCH_INDEX_UNSTAGED_YES);
		else if (type == LINE_STAT_UNTRACKED)
			watch_apply(&view->watch, WATCH_INDEX_UNTRACKED_YES);
	}

	io_done(&io);
	return true;
}

static const char *status_diff_index_argv[] = { GIT_DIFF_STAGED_FILES("-z") };
static const char *status_diff_files_argv[] = { GIT_DIFF_UNSTAGED_FILES("-z") };

static const char *status_list_other_argv[] = {
	"git", "ls-files", "-z", "--others", "--exclude-standard", NULL, NULL, NULL
};

static const char *status_list_no_head_argv[] = {
	"git", "ls-files", "-z", "--cached", "--exclude-standard", NULL
};

/* Restore the previous line number to stay in the context or select a
 * line with something that can be updated. */
static void
status_restore(struct view *view)
{
	if (!check_position(&view->prev_pos))
		return;

	if (view->prev_pos.lineno >= view->lines)
		view->prev_pos.lineno = view->lines - 1;
	while (view->prev_pos.lineno < view->lines && !view->line[view->prev_pos.lineno].data)
		view->prev_pos.lineno++;
	while (view->prev_pos.lineno > 0 && !view->line[view->prev_pos.lineno].data)
		view->prev_pos.lineno--;

	/* If the above fails, always skip the "On branch" line. */
	if (view->prev_pos.lineno < view->lines)
		view->pos.lineno = view->prev_pos.lineno;
	else
		view->pos.lineno = 1;

	if (view->prev_pos.offset > view->pos.lineno)
		view->pos.offset = view->pos.lineno;
	else if (view->prev_pos.offset < view->lines)
		view->pos.offset = view->prev_pos.offset;

	clear_position(&view->prev_pos);
}

static bool
status_branch_tracking_info(char *buf, size_t buf_len, const char *head,
			    const char *remote)
{
	if (!string_nformat(buf, buf_len, NULL, "%s...%s",
			    head, remote)) {
		return false;
	}

	const char *tracking_info_argv[] = {
		"git", "rev-list", "--left-right", buf, NULL
	};

	struct io io;

	if (!io_run(&io, IO_RD, repo.exec_dir, NULL, tracking_info_argv)) {
		return false;
	}

	struct buffer result = { 0 };
	int ahead = 0;
	int behind = 0;

	while (io_get(&io, &result, '\n', true)) {
		if (result.size > 0 && result.data) {
			if (result.data[0] == '<') {
				ahead++;
			} else if (result.data[0] == '>') {
				behind++;
			}
		}
	}

	bool io_failed = io_error(&io);
	io_done(&io);

	if (io_failed) {
		return false;
	}

	if (ahead == 0 && behind == 0) {
		return string_nformat(buf, buf_len, NULL,
				      "Your branch is up-to-date with '%s'.",
				      remote);
	} else if (ahead > 0 && behind > 0) {
		return string_nformat(buf, buf_len, NULL,
				      "Your branch and '%s' have diverged, "
				      "and have %d and %d different commits "
				      "each, respectively",
				      remote, ahead, behind);
	} else if (ahead > 0) {
		return string_nformat(buf, buf_len, NULL,
				      "Your branch is ahead of '%s' by "
				      "%d commit%s.", remote, ahead,
				      ahead > 1 ? "s" : "");
	} else if (behind > 0) {
		return string_nformat(buf, buf_len, NULL,
				      "Your branch is behind '%s' by "
				      "%d commit%s.", remote, behind,
				      behind > 1 ? "s" : "");
	}

	return false;
}

static void
status_update_onbranch(void)
{
	static const char *paths[][3] = {
		{ "rebase-apply/rebasing",	"rebase-apply/head-name",	"Rebasing" },
		{ "rebase-apply/applying",	"rebase-apply/head-name",	"Applying mailbox to" },
		{ "rebase-apply/",		"rebase-apply/head-name",	"Rebasing mailbox onto" },
		{ "rebase-merge/interactive",	"rebase-merge/head-name",	"Interactive rebase" },
		{ "rebase-merge/",		"rebase-merge/head-name",	"Rebase merge" },
		{ "MERGE_HEAD",			NULL,				"Merging" },
		{ "BISECT_LOG",			NULL,				"Bisecting" },
		{ "HEAD",			NULL,				"On branch" },
	};
	char buf[SIZEOF_STR];
	struct stat stat;
	int i;

	if (is_initial_commit()) {
		string_copy(status_onbranch, "Initial commit");
		return;
	}

	for (i = 0; i < ARRAY_SIZE(paths); i++) {
		const char *prefix = paths[i][2];
		const char *head = repo.head;
		const char *tracking_info = "";

		if (!string_format(buf, "%s/%s", repo.git_dir, paths[i][0]) ||
		    lstat(buf, &stat) < 0)
			continue;

		if (paths[i][1]) {
			struct io io;

			if (io_open(&io, "%s/%s", repo.git_dir, paths[i][1]) &&
			    io_read_buf(&io, buf, sizeof(buf), false)) {
				head = buf;
				if (!prefixcmp(head, "refs/heads/"))
					head += STRING_SIZE("refs/heads/");
			}
		}

		if (!*head && !strcmp(paths[i][0], "HEAD") && *repo.head_id) {
			const struct ref *ref = get_canonical_ref(repo.head_id);

			prefix = "HEAD detached at";
			head = repo.head_id;

			if (ref && strcmp(ref->name, "HEAD"))
				head = ref->name;
		} else if (!paths[i][1] && *repo.remote) {
			if (status_branch_tracking_info(buf, sizeof(buf),
							head, repo.remote)) {
				tracking_info = buf;
			}
		}

		const char *fmt = *tracking_info == '\0' ? "%s %s" : "%s %s. %s";

		if (!string_format(status_onbranch, fmt,
				   prefix, head, tracking_info))
			string_copy(status_onbranch, repo.head);
		return;
	}

	string_copy(status_onbranch, "Not currently on any branch");
}

static bool
status_read_untracked(struct view *view)
{
	if (!opt_status_show_untracked_files)
		return add_line_nodata(view, LINE_STAT_UNTRACKED)
		    && add_line_nodata(view, LINE_STAT_NONE);

	status_list_other_argv[ARRAY_SIZE(status_list_other_argv) - 3] =
		opt_status_show_untracked_dirs ? NULL : "--directory";
	status_list_other_argv[ARRAY_SIZE(status_list_other_argv) - 2] =
		opt_status_show_untracked_dirs ? NULL : "--no-empty-directory";

	return status_run(view, status_list_other_argv, '?', LINE_STAT_UNTRACKED);
}

/* First parse staged info using git-diff-index(1), then parse unstaged
 * info using git-diff-files(1), and finally untracked files using
 * git-ls-files(1). */
static enum status_code
status_open(struct view *view, enum open_flags flags)
{
	const char **staged_argv = is_initial_commit() ?
		status_list_no_head_argv : status_diff_index_argv;
	char staged_status = staged_argv == status_list_no_head_argv ? 'A' : 0;

	if (!(repo.is_inside_work_tree || *repo.worktree))
		return error("The status view requires a working tree");

	reset_view(view);

	/* FIXME: Watch untracked files and on-branch info. */
	watch_register(&view->watch, WATCH_INDEX);

	add_line_nodata(view, LINE_HEADER);
	status_update_onbranch();

	update_index();

	if ((!show_untracked_only && !status_run(view, staged_argv, staged_status, LINE_STAT_STAGED)) ||
	    (!show_untracked_only && !status_run(view, status_diff_files_argv, 0, LINE_STAT_UNSTAGED)) ||
	    !status_read_untracked(view))
		return error("Failed to load status data");

	/* Restore the exact position or use the specialized restore
	 * mode? */
	status_restore(view);
	return SUCCESS;
}

static bool
status_get_column_data(struct view *view, const struct line *line, struct view_column_data *column_data)
{
	struct status *status = line->data;

	if (!status) {
		static struct view_column group_column;
		const char *text;
		enum line_type type;

		column_data->section = &group_column;
		column_data->section->type = VIEW_COLUMN_SECTION;

		switch (line->type) {
		case LINE_STAT_STAGED:
			type = LINE_SECTION;
			text = "Changes to be committed:";
			break;

		case LINE_STAT_UNSTAGED:
			type = LINE_SECTION;
			text = "Changes not staged for commit:";
			break;

		case LINE_STAT_UNTRACKED:
			type = LINE_SECTION;
			text = "Untracked files:";
			break;

		case LINE_STAT_NONE:
			type = LINE_DEFAULT;
			text = "  (no files)";
			if (!opt_status_show_untracked_files
			    && view->line < line
			    && line[-1].type == LINE_STAT_UNTRACKED)
				text = "  (not shown)";
			break;

		case LINE_HEADER:
			type = LINE_HEADER;
			text = status_onbranch;
			break;

		default:
			return false;
		}

		column_data->section->opt.section.text = text;
		column_data->section->opt.section.type = type;

	} else {
		column_data->status = &status->status;
		column_data->file_name = status->new.name;
	}
	return true;
}

static enum request
status_enter(struct view *view, struct line *line)
{
	struct status *status = line->data;
	enum open_flags flags = view_is_displayed(view) ? OPEN_SPLIT : OPEN_DEFAULT;

	if (line->type == LINE_STAT_NONE ||
	    (!status && line[1].type == LINE_STAT_NONE)) {
		report("No file to diff");
		return REQ_NONE;
	}

	switch (line->type) {
	case LINE_STAT_STAGED:
	case LINE_STAT_UNSTAGED:
		break;

	case LINE_STAT_UNTRACKED:
		if (!status) {
			report("No file to show");
			return REQ_NONE;
		}

		if (!suffixcmp(status->new.name, -1, "/")) {
			report("Cannot display a directory");
			return REQ_NONE;
		}
		break;

	default:
		report("Nothing to enter");
		return REQ_NONE;
	}

	open_stage_view(view, status, line->type, flags);
	return REQ_NONE;
}

bool
status_exists(struct view *view, struct status *status, enum line_type type)
{
	unsigned long lineno;

	refresh_view(view);

	for (lineno = 0; lineno < view->lines; lineno++) {
		struct line *line = &view->line[lineno];
		struct status *pos = line->data;

		if (line->type != type)
			continue;
		if ((!pos && (!status || !status->status) && line[1].data) ||
		    (pos && status && !strcmp(status->new.name, pos->new.name))) {
			select_view_line(view, lineno);
			status_restore(view);
			return true;
		}
	}

	return false;
}


static bool
status_update_prepare(struct io *io, enum line_type type)
{
	const char *staged_argv[] = {
		"git", "update-index", "-z", "--index-info", NULL
	};
	const char *others_argv[] = {
		"git", "update-index", "-z", "--add", "--remove", "--stdin", NULL
	};

	switch (type) {
	case LINE_STAT_STAGED:
		return io_run(io, IO_WR, repo.exec_dir, NULL, staged_argv);

	case LINE_STAT_UNSTAGED:
	case LINE_STAT_UNTRACKED:
		return io_run(io, IO_WR, repo.exec_dir, NULL, others_argv);

	default:
		die("line type %d not handled in switch", type);
		return false;
	}
}

static bool
status_update_write(struct io *io, struct status *status, enum line_type type)
{
	switch (type) {
	case LINE_STAT_STAGED:
		return io_printf(io, "%06o %s\t%s%c", status->old.mode,
				 status->old.rev, status->old.name, 0);

	case LINE_STAT_UNSTAGED:
	case LINE_STAT_UNTRACKED:
		return io_printf(io, "%s%c", status->new.name, 0);

	default:
		die("line type %d not handled in switch", type);
		return false;
	}
}

bool
status_update_file(struct status *status, enum line_type type)
{
	const char *name = status->new.name;
	struct io io;
	bool result;

	if (type == LINE_STAT_UNTRACKED && !suffixcmp(name, strlen(name), "/")) {
		const char *add_argv[] = { "git", "add", "--", name, NULL };

		return io_run_bg(add_argv, repo.exec_dir);
	}

	if (!status_update_prepare(&io, type))
		return false;

	result = status_update_write(&io, status, type);
	return io_done(&io) && result;
}

bool
status_update_files(struct view *view, struct line *line)
{
	char buf[sizeof(view->ref)];
	struct io io;
	bool result = true;
	struct line *pos;
	int files = 0;
	int file, done;
	int cursor_y = -1, cursor_x = -1;

	if (!status_update_prepare(&io, line->type))
		return false;

	for (pos = line; view_has_line(view, pos) && pos->data; pos++)
		files++;

	string_copy(buf, view->ref);
	get_cursor_pos(cursor_y, cursor_x);
	for (file = 0, done = 5; result && file < files; line++, file++) {
		int almost_done = file * 100 / files;

		if (almost_done > done && view_is_displayed(view)) {
			done = almost_done;
			string_format(view->ref, "updating file %d of %d (%d%% done)",
				      file, files, done);
			update_view_title(view);
			set_cursor_pos(cursor_y, cursor_x);
			doupdate();
		}
		result = status_update_write(&io, line->data, line->type);
	}
	string_copy(view->ref, buf);

	return io_done(&io) && result;
}

static bool
status_update(struct view *view)
{
	struct line *line = &view->line[view->pos.lineno];

	assert(view->lines);

	if (!line->data) {
		if (status_has_none(view, line)) {
			report("Nothing to update");
			return false;
		}

		if (!status_update_files(view, line + 1)) {
			report("Failed to update file status");
			return false;
		}

	} else if (!status_update_file(line->data, line->type)) {
		report("Failed to update file status");
		return false;
	}

	return true;
}

bool
status_revert(struct status *status, enum line_type type, bool has_none)
{
	if (!status || type != LINE_STAT_UNSTAGED) {
		if (type == LINE_STAT_STAGED) {
			report("Cannot revert changes to staged files");
		} else if (type == LINE_STAT_UNTRACKED) {
			report("Cannot revert changes to untracked files");
		} else if (has_none) {
			report("Nothing to revert");
		} else {
			report("Cannot revert changes to multiple files");
		}

	} else if (prompt_yesno("Are you sure you want to revert changes?")) {
		char mode[10] = "100644";
		const char *reset_argv[] = {
			"git", "update-index", "--cacheinfo", mode,
				status->old.rev, status->old.name, NULL
		};
		const char *checkout_argv[] = {
			"git", "checkout", "--", status->old.name, NULL
		};

		if (status->status == 'U') {
			string_format(mode, "%5o", status->old.mode);

			if (status->old.mode == 0 && status->new.mode == 0) {
				reset_argv[2] = "--force-remove";
				reset_argv[3] = status->old.name;
				reset_argv[4] = NULL;
			}

			if (!io_run_fg(reset_argv, repo.exec_dir))
				return false;
			if (status->old.mode == 0 && status->new.mode == 0)
				return true;
		}

		return io_run_fg(checkout_argv, repo.exec_dir);
	}

	return false;
}

static void
open_mergetool(const char *file)
{
	const char *mergetool_argv[] = { "git", "mergetool", file, NULL };

	open_external_viewer(mergetool_argv, repo.exec_dir, false, true, false, true, true, "");
}

static enum request
status_request(struct view *view, enum request request, struct line *line)
{
	struct status *status = line->data;

	switch (request) {
	case REQ_STATUS_UPDATE:
		if (!status_update(view))
			return REQ_NONE;
		break;

	case REQ_STATUS_REVERT:
		if (!status_revert(status, line->type, status_has_none(view, line)))
			return REQ_NONE;
		break;

	case REQ_STATUS_MERGE:
		if (!status || status->status != 'U') {
			report("Merging only possible for files with unmerged status ('U').");
			return REQ_NONE;
		}
		open_mergetool(status->new.name);
		break;

	case REQ_EDIT:
		if (!status)
			return request;
		if (status->status == 'D') {
			report("File has been deleted.");
			return REQ_NONE;
		}

		open_editor(status->new.name, 0);
		break;

	case REQ_VIEW_BLAME:
		if (line->type == LINE_STAT_UNTRACKED || !status) {
			report("Nothing to blame here");
			return REQ_NONE;
		}
		if (status)
			view->env->ref[0] = 0;
		return request;

	case REQ_ENTER:
		/* After returning the status view has been split to
		 * show the stage view. No further reloading is
		 * necessary. */
		return status_enter(view, line);

	case REQ_REFRESH:
		/* Load the current branch information and then the view. */
		load_repo_head();
		break;

	default:
		return request;
	}

	if (show_untracked_only && view->parent == &main_view && !main_status_exists(view->parent, LINE_STAT_UNTRACKED))
		return REQ_VIEW_CLOSE;

	refresh_view(view);

	return REQ_NONE;
}

bool
status_stage_info_(char *buf, size_t bufsize,
		   enum line_type type, struct status *status)
{
	const char *file = status ? status->new.name : "";
	const char *info;

	switch (type) {
	case LINE_STAT_STAGED:
		if (status && status->status)
			info = "Staged changes to %s";
		else
			info = "Staged changes";
		break;

	case LINE_STAT_UNSTAGED:
		if (status && status->status)
			info = "Unstaged changes to %s";
		else
			info = "Unstaged changes";
		break;

	case LINE_STAT_UNTRACKED:
		info = "Untracked file %s";
		break;

	case LINE_HEADER:
	default:
		info = "";
	}

	return string_nformat(buf, bufsize, NULL, info, file);
}

static void
status_select(struct view *view, struct line *line)
{
	struct status *status = line->data;
	char file[SIZEOF_STR] = "all files";
	const char *text;
	const char *key;

	if (status && !string_format(file, "'%s'", status->new.name))
		return;

	if (!status && line[1].type == LINE_STAT_NONE)
		line++;

	switch (line->type) {
	case LINE_STAT_STAGED:
		text = "Press %s to unstage %s for commit";
		break;

	case LINE_STAT_UNSTAGED:
		text = "Press %s to stage %s for commit";
		break;

	case LINE_STAT_UNTRACKED:
		text = "Press %s to stage %s for addition";
		break;

	default:
		text = "Nothing to update";
	}

	if (status && status->status == 'U') {
		text = "Press %s to resolve conflict in %s";
		key = get_view_key(view, REQ_STATUS_MERGE);

	} else {
		key = get_view_key(view, REQ_STATUS_UPDATE);
	}

	string_format(view->ref, text, key, file);
	status_stage_info(view->env->status, line->type, status);
	if (status)
		string_copy(view->env->file, status->new.name);
}

static struct view_ops status_ops = {
	"file",
	"",
	VIEW_CUSTOM_STATUS | VIEW_SEND_CHILD_ENTER | VIEW_STATUS_LIKE | VIEW_REFRESH,
	0,
	status_open,
	NULL,
	view_column_draw,
	status_request,
	view_column_grep,
	status_select,
	NULL,
	view_column_bit(FILE_NAME) | view_column_bit(LINE_NUMBER) |
		view_column_bit(STATUS),
	status_get_column_data,
};

DEFINE_VIEW(status);

/* vim: set ts=8 sw=8 noexpandtab: */
