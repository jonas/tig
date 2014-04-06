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

#ifndef TIG_MAIN_H
#define TIG_MAIN_H

#include "tig/view.h"
#include "tig/graph.h"
#include "tig/util.h"

struct commit {
	char id[SIZEOF_REV];		/* SHA1 ID. */
	const struct ident *author;	/* Author of the commit. */
	struct time time;		/* Date from the author ident. */
	struct graph_canvas graph;	/* Ancestry chain graphics. */
	char title[1];			/* First line of the commit message. */
};

struct main_state {
	struct graph graph;
	struct commit current;
	char **reflog;
	size_t reflogs;
	int reflog_width;
	char reflogmsg[SIZEOF_STR / 2];
	bool in_header;
	bool added_changes_commits;
	bool with_graph;
};

bool main_get_column_data(struct view *view, const struct line *line, struct view_column_data *column_data);
bool main_read(struct view *view, char *line);
enum request main_request(struct view *view, enum request request, struct line *line);
void main_select(struct view *view, struct line *line);
void main_done(struct view *view);

extern struct view main_view;

static inline void
open_main_view(struct view *prev, enum open_flags flags)
{
	open_view(prev, &main_view, flags);
}

#endif
/* vim: set ts=8 sw=8 noexpandtab: */
