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
	struct graph *graph;
	struct commit current;
	char **reflog;
	size_t reflogs;
	int reflog_width;
	char reflogmsg[SIZEOF_STR / 2];
	enum line_type goto_line_type;
	bool in_header;
	bool with_graph;
	bool first_parent;
	bool add_changes_staged;
	bool add_changes_unstaged;
};

bool main_get_column_data(struct view *view, const struct line *line, struct view_column_data *column_data);
bool main_read(struct view *view, struct buffer *buf, bool force_stop);
enum request main_request(struct view *view, enum request request, struct line *line);
void main_select(struct view *view, struct line *line);
void main_done(struct view *view);
bool main_status_exists(struct view *view, enum line_type type);

extern struct view main_view;

static inline void
open_main_view(struct view *prev, enum open_flags flags)
{
	open_view(prev, &main_view, flags);
}

#endif
/* vim: set ts=8 sw=8 noexpandtab: */
