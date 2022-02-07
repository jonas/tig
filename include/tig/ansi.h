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

#ifndef TIG_ANSI_H
#define TIG_ANSI_H

#include "tig/line.h"
#include "tig/tig.h"
#include "tig/view.h"

struct ansi_status {
	short fg;
	short bg;
	unsigned int attr;
};

void split_ansi(const char *string, int *ansi_num, char **ansi_ptrs);
void draw_ansi(struct view *view, int *ansi_num, char **ansi_ptrs, int max_width, size_t skip);
void draw_ansi_line(struct view *view, char *ansi_end_ptr, int after_ansi_len, size_t *skip, int *cur_width, int *widths_of_display);
void wattrset_by_ansi_status(struct view *view, struct ansi_status* cur_ansi_status);

#endif

/* vim: set ts=8 sw=8 noexpandtab: */
