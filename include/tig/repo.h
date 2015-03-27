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

struct repo_info {
	char head[SIZEOF_REF];
	char head_id[SIZEOF_REV];
	char remote[SIZEOF_REF];
	char cdup[SIZEOF_STR];
	char prefix[SIZEOF_STR];
	char git_dir[SIZEOF_STR];
	bool is_inside_work_tree;
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
