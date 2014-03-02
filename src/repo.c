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

#include "tig/tig.h"
#include "tig/repo.h"
#include "tig/io.h"
#include "tig/refs.h"

#define REPO_INFO_GIT_DIR	"--git-dir"
#define REPO_INFO_WORK_TREE	"--is-inside-work-tree"
#define REPO_INFO_SHOW_CDUP	"--show-cdup"
#define REPO_INFO_SHOW_PREFIX	"--show-prefix"
#define REPO_INFO_SYMBOLIC_HEAD	"--symbolic-full-name"
#define REPO_INFO_RESOLVED_HEAD	"HEAD"

struct repo_info_state {
	const char **argv;
	char head_id[SIZEOF_REV];
};

static int
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
		repo.is_inside_work_tree = strcmp(name, "false") ? TRUE : FALSE;

	} else if (!strcmp(arg, REPO_INFO_SHOW_CDUP)) {
		string_ncopy(repo.cdup, name, namelen);

	} else if (!strcmp(arg, REPO_INFO_SHOW_PREFIX)) {
		string_ncopy(repo.prefix, name, namelen);

	} else if (!strcmp(arg, REPO_INFO_RESOLVED_HEAD)) {
		string_ncopy(state->head_id, name, namelen);

	} else if (!strcmp(arg, REPO_INFO_SYMBOLIC_HEAD)) {
		if (!prefixcmp(name, "refs/heads/")) {
			char *offset = name + STRING_SIZE("refs/heads/");

			string_ncopy(repo.head, offset, strlen(offset) + 1);
			add_ref(state->head_id, name, repo.remote, repo.head);
		}
		state->argv++;
	}

	return OK;
}

int
load_repo_info(void)
{
	const char *rev_parse_argv[] = {
		"git", "rev-parse", REPO_INFO_GIT_DIR, REPO_INFO_WORK_TREE,
			REPO_INFO_SHOW_CDUP, REPO_INFO_SHOW_PREFIX, \
			REPO_INFO_RESOLVED_HEAD, REPO_INFO_SYMBOLIC_HEAD, "HEAD",
			NULL
	};
	struct repo_info_state state = { rev_parse_argv + 2 };

	return io_run_load(rev_parse_argv, "=", read_repo_info, &state);
}

struct repo_info repo;

/* vim: set ts=8 sw=8 noexpandtab: */
