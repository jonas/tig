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

#ifndef TIG_DRAW_H
#define TIG_DRAW_H

#include "tig/tig.h"
#include "tig/line.h"
#include "tig/view.h"
#include "tig/refdb.h"
#include "tig/util.h"

enum align {
	ALIGN_LEFT,
	ALIGN_RIGHT
};

bool draw_text(struct view *view, enum line_type type, const char *string);
bool PRINTF_LIKE(3, 4) draw_formatted(struct view *view, enum line_type type, const char *format, ...);
bool draw_graphic(struct view *view, enum line_type type, const chtype graphic[], size_t size, bool separator);
bool draw_field(struct view *view, enum line_type type, const char *text, int width, enum align align, bool trim);
bool draw_lineno(struct view *view, struct view_column *column, unsigned int lineno, bool add_offset);
bool view_column_draw(struct view *view, struct line *line, unsigned int lineno);

void redraw_view(struct view *view);
void redraw_view_from(struct view *view, int lineno);
void redraw_view_dirty(struct view *view);
bool draw_view_line(struct view *view, unsigned int lineno);

#endif
/* vim: set ts=8 sw=8 noexpandtab: */
