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

#include "view.h"
#include "graph.h"
#include "util.h"

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

bool main_read(struct view *view, char *line);
bool main_draw(struct view *view, struct line *line, unsigned int lineno);
enum request main_request(struct view *view, enum request request, struct line *line);
bool main_grep(struct view *view, struct line *line);
void main_select(struct view *view, struct line *line);

extern struct view_ops main_ops;

#endif
/* vim: set ts=8 sw=8 noexpandtab: */
