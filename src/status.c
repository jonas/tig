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
#include "tig/refdb.h"
#include "tig/repo.h"
#include "tig/options.h"
#include "tig/parse.h"
#include "tig/display.h"
#include "tig/prompt.h"
#include "tig/view.h"
#include "tig/draw.h"
#include "tig/git.h"
#include "tig/status.h"
#include "tig/stage.h"

/*
 * Status backend
 */

static char status_onbranch[SIZEOF_STR];
struct status stage_status;
enum line_type stage_line_type;

/* This should work even for the "On branch" line. */
static inline bool
status_has_none(struct view *view, struct line *line)
{
	return view_has_line(view, line) && !line[1].data;
}

/* Get fields from the diff line:
 * :100644 100644 06a5d6ae9eca55be2e0e585a152e6b1336f2b20e 0000000000000000000000000000000000000000 M
 */
static inline bool
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
		return FALSE;

	file->status = *status;

	string_copy_rev(file->old.rev, old_rev);
	string_copy_rev(file->new.rev, new_rev);

	file->old.mode = strtoul(old_mode, NULL, 8);
	file->new.mode = strtoul(new_mode, NULL, 8);

	file->old.name[0] = file->new.name[0] = 0;

	return TRUE;
}

static bool
status_run(struct view *view, const char *argv[], char status, enum line_type type)
{
	struct status *unmerged = NULL;
	char *buf;
	struct io io;

	if (!io_run(&io, IO_RD, repo.cdup, opt_env, argv))
		return FALSE;

	add_line_nodata(view, type);

	while ((buf = io_get(&io, 0, TRUE))) {
		struct status *file = unmerged;

		if (!file) {
			if (!add_line_alloc(view, &file, type, 0, FALSE))
				goto error_out;
		}

		/* Parse diff info part. */
		if (status) {
			file->status = status;
			if (status == 'A')
				string_copy(file->old.rev, NULL_ID);

		} else if (!file->status || file == unmerged) {
			if (!status_get_diff(file, buf, strlen(buf)))
				goto error_out;

			buf = io_get(&io, 0, TRUE);
			if (!buf)
				break;

			/* Collapse all modified entries that follow an
			 * associated unmerged entry. */
			if (unmerged == file) {
				unmerged->status = 'U';
				unmerged = NULL;
			} else if (file->status == 'U') {
				unmerged = file;
			}
		}

		/* Grab the old name for rename/copy. */
		if (!*file->old.name &&
		    (file->status == 'R' || file->status == 'C')) {
			string_ncopy(file->old.name, buf, strlen(buf));

			buf = io_get(&io, 0, TRUE);
			if (!buf)
				break;
		}

		/* git-ls-files just delivers a NUL separated list of
		 * file names similar to the second half of the
		 * git-diff-* output. */
		string_ncopy(file->new.name, buf, strlen(buf));
		if (!*file->old.name)
			string_copy(file->old.name, file->new.name);
		file = NULL;
	}

	if (io_error(&io)) {
error_out:
		io_done(&io);
		return FALSE;
	}

	if (!view->line[view->lines - 1].data)
		add_line_nodata(view, LINE_STAT_NONE);

	io_done(&io);
	return TRUE;
}

static const char *status_diff_index_argv[] = { GIT_DIFF_STAGED_FILES("-z") };
static const char *status_diff_files_argv[] = { GIT_DIFF_UNSTAGED_FILES("-z") };

static const char *status_list_other_argv[] = {
	"git", "ls-files", "-z", "--others", "--exclude-standard", repo.prefix, NULL, NULL, NULL
};

static const char *status_list_no_head_argv[] = {
	"git", "ls-files", "-z", "--cached", "--exclude-standard", NULL
};

const char *update_index_argv[] = {
	"git", "update-index", "-q", "--unmerged", "--refresh", NULL
};

/* Restore the previous line number to stay in the context or select a
 * line with something that can be updated. */
void
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

static void
status_update_onbranch(void)
{
	static const char *paths[][2] = {
		{ "rebase-apply/rebasing",	"Rebasing" },
		{ "rebase-apply/applying",	"Applying mailbox" },
		{ "rebase-apply/",		"Rebasing mailbox" },
		{ "rebase-merge/interactive",	"Interactive rebase" },
		{ "rebase-merge/",		"Rebase merge" },
		{ "MERGE_HEAD",			"Merging" },
		{ "BISECT_LOG",			"Bisecting" },
		{ "HEAD",			"On branch" },
	};
	char buf[SIZEOF_STR];
	struct stat stat;
	int i;

	if (is_initial_commit()) {
		string_copy(status_onbranch, "Initial commit");
		return;
	}

	for (i = 0; i < ARRAY_SIZE(paths); i++) {
		char *head = repo.head;

		if (!string_format(buf, "%s/%s", repo.git_dir, paths[i][0]) ||
		    lstat(buf, &stat) < 0)
			continue;

		if (!*repo.head) {
			struct io io;

			if (io_open(&io, "%s/rebase-merge/head-name", repo.git_dir) &&
			    io_read_buf(&io, buf, sizeof(buf))) {
				head = buf;
				if (!prefixcmp(head, "refs/heads/"))
					head += STRING_SIZE("refs/heads/");
			}
		}

		if (!string_format(status_onbranch, "%s %s", paths[i][1], head))
			string_copy(status_onbranch, repo.head);
		return;
	}

	string_copy(status_onbranch, "Not currently on any branch");
}

/* First parse staged info using git-diff-index(1), then parse unstaged
 * info using git-diff-files(1), and finally untracked files using
 * git-ls-files(1). */
static bool
status_open(struct view *view, enum open_flags flags)
{
	const char **staged_argv = is_initial_commit() ?
		status_list_no_head_argv : status_diff_index_argv;
	char staged_status = staged_argv == status_list_no_head_argv ? 'A' : 0;

	if (repo.is_inside_work_tree == FALSE) {
		report("The status view requires a working tree");
		return FALSE;
	}

	reset_view(view);

	add_line_nodata(view, LINE_STAT_HEAD);
	status_update_onbranch();

	io_run_bg(update_index_argv);

	status_list_other_argv[ARRAY_SIZE(status_list_other_argv) - 3] =
		opt_status_untracked_dirs ? NULL : "--directory";
	status_list_other_argv[ARRAY_SIZE(status_list_other_argv) - 2] =
		opt_status_untracked_dirs ? NULL : "--no-empty-directory";

	if (!status_run(view, staged_argv, staged_status, LINE_STAT_STAGED) ||
	    !status_run(view, status_diff_files_argv, 0, LINE_STAT_UNSTAGED) ||
	    !status_run(view, status_list_other_argv, '?', LINE_STAT_UNTRACKED)) {
		report("Failed to load status data");
		return FALSE;
	}

	/* Restore the exact position or use the specialized restore
	 * mode? */
	status_restore(view);
	return TRUE;
}

static bool
status_draw(struct view *view, struct line *line, unsigned int lineno)
{
	struct status *status = line->data;
	enum line_type type;
	const char *text;

	if (!status) {
		switch (line->type) {
		case LINE_STAT_STAGED:
			type = LINE_STAT_SECTION;
			text = "Changes to be committed:";
			break;

		case LINE_STAT_UNSTAGED:
			type = LINE_STAT_SECTION;
			text = "Changed but not updated:";
			break;

		case LINE_STAT_UNTRACKED:
			type = LINE_STAT_SECTION;
			text = "Untracked files:";
			break;

		case LINE_STAT_NONE:
			type = LINE_DEFAULT;
			text = "  (no files)";
			break;

		case LINE_STAT_HEAD:
			type = LINE_STAT_HEAD;
			text = status_onbranch;
			break;

		default:
			return FALSE;
		}
	} else {
		static char buf[] = { '?', ' ', ' ', ' ', 0 };

		buf[0] = status->status;
		if (draw_text(view, line->type, buf))
			return TRUE;
		type = LINE_DEFAULT;
		text = status->new.name;
	}

	draw_text(view, type, text);
	return TRUE;
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

	case LINE_STAT_HEAD:
		return REQ_NONE;

	default:
		die("line type %d not handled in switch", line->type);
	}

	if (status) {
		stage_status = *status;
	} else {
		memset(&stage_status, 0, sizeof(stage_status));
	}

	stage_line_type = line->type;

	open_stage_view(view, flags);
	return REQ_NONE;
}

bool
status_exists(struct view *view, struct status *status, enum line_type type)
{
	unsigned long lineno;

	for (lineno = 0; lineno < view->lines; lineno++) {
		struct line *line = &view->line[lineno];
		struct status *pos = line->data;

		if (line->type != type)
			continue;
		if (!pos && (!status || !status->status) && line[1].data) {
			select_view_line(view, lineno);
			return TRUE;
		}
		if (pos && !strcmp(status->new.name, pos->new.name)) {
			select_view_line(view, lineno);
			return TRUE;
		}
	}

	return FALSE;
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
		return io_run(io, IO_WR, repo.cdup, opt_env, staged_argv);

	case LINE_STAT_UNSTAGED:
	case LINE_STAT_UNTRACKED:
		return io_run(io, IO_WR, repo.cdup, opt_env, others_argv);

	default:
		die("line type %d not handled in switch", type);
		return FALSE;
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
		return FALSE;
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

		return io_run_bg(add_argv);
	}

	if (!status_update_prepare(&io, type))
		return FALSE;

	result = status_update_write(&io, status, type);
	return io_done(&io) && result;
}

bool
status_update_files(struct view *view, struct line *line)
{
	char buf[sizeof(view->ref)];
	struct io io;
	bool result = TRUE;
	struct line *pos;
	int files = 0;
	int file, done;
	int cursor_y = -1, cursor_x = -1;

	if (!status_update_prepare(&io, line->type))
		return FALSE;

	for (pos = line; view_has_line(view, pos) && pos->data; pos++)
		files++;

	string_copy(buf, view->ref);
	getsyx(cursor_y, cursor_x);
	for (file = 0, done = 5; result && file < files; line++, file++) {
		int almost_done = file * 100 / files;

		if (almost_done > done) {
			done = almost_done;
			string_format(view->ref, "updating file %u of %u (%d%% done)",
				      file, files, done);
			update_view_title(view);
			setsyx(cursor_y, cursor_x);
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
			return FALSE;
		}

		if (!status_update_files(view, line + 1)) {
			report("Failed to update file status");
			return FALSE;
		}

	} else if (!status_update_file(line->data, line->type)) {
		report("Failed to update file status");
		return FALSE;
	}

	return TRUE;
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

			if (!io_run_fg(reset_argv, repo.cdup))
				return FALSE;
			if (status->old.mode == 0 && status->new.mode == 0)
				return TRUE;
		}

		return io_run_fg(checkout_argv, repo.cdup);
	}

	return FALSE;
}

static void
open_mergetool(const char *file)
{
	const char *mergetool_argv[] = { "git", "mergetool", file, NULL };

	open_external_viewer(mergetool_argv, repo.cdup, TRUE, "");
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
		load_refs(TRUE);
		break;

	default:
		return request;
	}

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

	case LINE_STAT_HEAD:
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

	case LINE_STAT_HEAD:
	case LINE_STAT_NONE:
		text = "Nothing to update";
		break;

	default:
		die("line type %d not handled in switch", line->type);
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

static bool
status_grep(struct view *view, struct line *line)
{
	struct status *status = line->data;

	if (status) {
		const char buf[2] = { status->status, 0 };
		const char *text[] = { status->new.name, buf, NULL };

		return grep_text(view, text);
	}

	return FALSE;
}

static struct view_ops status_ops = {
	"file",
	"",
	VIEW_CUSTOM_STATUS | VIEW_SEND_CHILD_ENTER | VIEW_STATUS_LIKE | VIEW_REFRESH,
	0,
	status_open,
	NULL,
	status_draw,
	status_request,
	status_grep,
	status_select,
};

DEFINE_VIEW(status);

/* vim: set ts=8 sw=8 noexpandtab: */
