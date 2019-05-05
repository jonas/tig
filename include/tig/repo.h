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

#ifndef TIG_REPO_H
#define TIG_REPO_H

#include "tig/tig.h"

typedef char repo_ref[SIZEOF_REF];
typedef char repo_rev[SIZEOF_REV];
typedef char repo_str[SIZEOF_STR];

#define REPO_INFO(_) \
	_(repo_ref, head) \
	_(repo_rev, head_id) \
	_(repo_ref, remote) \
	_(repo_str, cdup) \
	_(repo_str, prefix) \
	_(repo_str, git_dir) \
	_(repo_str, worktree) \
	_(repo_str, exec_dir) \
	_(bool,     is_inside_work_tree)

#define REPO_INFO_FIELDS(type, name)	type name;

struct repo_info {
	REPO_INFO(REPO_INFO_FIELDS)
};

extern struct repo_info repo;

enum status_code load_repo_info(void);
enum status_code load_repo_head(void);

struct index_diff {
	int staged;
	int unstaged;
	int untracked;
};

bool index_diff(struct index_diff *diff, bool untracked, bool count_all);
bool update_index(void);

#endif
/* vim: set ts=8 sw=8 noexpandtab: */
