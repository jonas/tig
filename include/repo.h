/* Copyright (c) 2006-2014 Jonas Fonseca <fonseca@diku.dk>
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

#ifndef REPO_H
#define REPO_H

#include "tig.h"

struct repo_info {
	char head[SIZEOF_REF];
	char remote[SIZEOF_REF];
	char cdup[SIZEOF_STR];
	char prefix[SIZEOF_STR];
	char git_dir[SIZEOF_STR];
	bool is_inside_work_tree;
};

extern struct repo_info repo;

int load_repo_info(void);

#endif
/* vim: set ts=8 sw=8 noexpandtab: */
