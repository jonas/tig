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

#ifndef TIG_DIFF_H
#define TIG_DIFF_H

#include "tig/view.h"

struct diff_state {
	bool after_commit_title;
	bool after_diff;
	bool reading_diff_stat;
	bool combined_diff;
};

enum request diff_common_edit(struct view *view, enum request request, struct line *line);
bool diff_common_read(struct view *view, const char *data, struct diff_state *state);
bool diff_common_draw(struct view *view, struct line *line, unsigned int lineno);
enum request diff_common_enter(struct view *view, enum request request, struct line *line);
bool diff_common_add_diff_stat(struct view *view, const char *data);
void diff_common_draw_diff_stat(struct view *view, enum line_type *type, char **text);

unsigned int diff_get_lineno(struct view *view, struct line *line);
const char *diff_get_pathname(struct view *view, struct line *line);

extern struct view diff_view;

static inline void
open_diff_view(struct view *prev, enum open_flags flags)
{
	open_view(prev, &diff_view, flags);
}

#endif
/* vim: set ts=8 sw=8 noexpandtab: */
