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

#include "tig/tig.h"
#include "tig/repo.h"
#include "tig/io.h"
#include "tig/refdb.h"
#include "tig/git.h"

#define REPO_INFO_GIT_DIR	"--git-dir"
#define REPO_INFO_WORK_TREE	"--is-inside-work-tree"
#define REPO_INFO_SHOW_CDUP	"--show-cdup"
#define REPO_INFO_SHOW_PREFIX	"--show-prefix"
#define REPO_INFO_SYMBOLIC_HEAD	"--symbolic-full-name"
#define REPO_INFO_RESOLVED_HEAD	"HEAD"

struct repo_info_state {
	const char **argv;
};

static enum status_code
read_repo_info(char *name, size_t namelen, char *value, size_t valuelen, void *data)
{
	struct repo_info_state *state = data;
	const char *arg = *state->argv ? *state->argv++ : "";

	if (!strcmp(arg, REPO_INFO_GIT_DIR)) {
		string_ncopy(repo.git_dir, name, namelen);

	} else if (!strcmp(arg, REPO_INFO_WORK_TREE)) {
		/* This can be 3 different values depending on the
		 * version of git being used. If git-rev-parse does not
		 * understand --is-inside-work-tree it will simply echo
		 * the option else either "true" or "false" is printed.
		 * Default to true for the unknown case. */
		repo.is_inside_work_tree = strcmp(name, "false") ? true : false;

	} else if (!strcmp(arg, REPO_INFO_SHOW_CDUP)) {
		string_ncopy(repo.cdup, name, namelen);
		if (repo.is_inside_work_tree)
			string_ncopy(repo.exec_dir, name, namelen);

	} else if (!strcmp(arg, REPO_INFO_SHOW_PREFIX)) {
		/* Some versions of Git does not emit anything for --show-prefix
		 * when the user is in the repository root directory. Try to detect
		 * this special case by looking at the emitted value. If it looks
		 * like a commit ID and there's no cdup path assume that no value
		 * was emitted. */
		if (!*repo.cdup && namelen == 40 && iscommit(name))
			return read_repo_info(name, namelen, value, valuelen, data);

		string_ncopy(repo.prefix, name, namelen);

	} else if (!strcmp(arg, REPO_INFO_RESOLVED_HEAD)) {
		string_ncopy(repo.head_id, name, namelen);

	} else if (!strcmp(arg, REPO_INFO_SYMBOLIC_HEAD)) {
		if (!prefixcmp(name, "refs/heads/")) {
			const char *head = name + STRING_SIZE("refs/heads/");

			string_ncopy(repo.head, head, strlen(head) + 1);
			add_ref(repo.head_id, name, repo.remote, repo.head);
		}
		state->argv++;
	}

	return SUCCESS;
}

static enum status_code
reload_repo_info(const char **rev_parse_argv)
{
	struct io io;
	struct repo_info_state state = { rev_parse_argv + 2 };

	return io_run_load(&io, rev_parse_argv, "\n", read_repo_info, &state);
}

enum status_code
load_repo_info(void)
{
	const char *rev_parse_argv[] = {
		"git", "rev-parse", REPO_INFO_GIT_DIR, REPO_INFO_WORK_TREE,
			REPO_INFO_SHOW_CDUP, REPO_INFO_SHOW_PREFIX, \
			REPO_INFO_RESOLVED_HEAD, REPO_INFO_SYMBOLIC_HEAD, "HEAD",
			NULL
	};

	memset(&repo, 0, sizeof(repo));
	return reload_repo_info(rev_parse_argv);
}

enum status_code
load_repo_head(void)
{
	const char *rev_parse_argv[] = {
		"git", "rev-parse", REPO_INFO_RESOLVED_HEAD,
			REPO_INFO_SYMBOLIC_HEAD, "HEAD", NULL
	};

	memset(repo.head, 0, sizeof(repo.head));
	memset(repo.head_id, 0, sizeof(repo.head_id));
	return reload_repo_info(rev_parse_argv);
}

struct repo_info repo;

/*
 * Git index utils.
 */

bool
update_index(void)
{
	const char *update_index_argv[] = {
		"git", "update-index", "-q", "--unmerged", "--refresh", NULL
	};

	return io_run_bg(update_index_argv, repo.exec_dir);
}

bool
index_diff(struct index_diff *diff, bool untracked, bool count_all)
{
	const char *untracked_arg = !untracked ? "--untracked-files=no" :
				     count_all ? "--untracked-files=all" :
						 "--untracked-files=normal";
	const char *status_argv[] = {
		"git", "status", "--porcelain", "-z", untracked_arg, NULL
	};
	struct io io;
	struct buffer buf;
	bool ok = true;

	memset(diff, 0, sizeof(*diff));

	if (!io_run(&io, IO_RD, repo.exec_dir, NULL, status_argv))
		return false;

	while (io_get(&io, &buf, 0, true) && (ok = buf.size > 3)) {
		if (buf.data[0] == '?')
			diff->untracked++;
		/* Ignore staged but unmerged entries. */
		else if (buf.data[0] != ' ' && buf.data[0] != 'U')
			diff->staged++;
		if (buf.data[1] != ' ' && buf.data[1] != '?')
			diff->unstaged++;
		if (!count_all && diff->staged && diff->unstaged &&
		    (!untracked || diff->untracked))
			break;

		/* Skip source filename in rename */
		if (buf.data[0] == 'R') {
			io_get(&io, &buf, 0, true);
		}
	}

	if (io_error(&io))
		ok = false;

	io_done(&io);
	return ok;
}

/* vim: set ts=8 sw=8 noexpandtab: */
