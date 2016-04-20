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

#ifndef TIG_PAGER_H
#define TIG_PAGER_H

#include "tig/view.h"

bool pager_get_column_data(struct view *view, const struct line *line, struct view_column_data *column_data);
bool pager_read(struct view *view, struct buffer *buf, bool force_stop);
bool pager_common_read(struct view *view, const char *data, enum line_type type, struct line **line);
enum request pager_request(struct view *view, enum request request, struct line *line);
void pager_select(struct view *view, struct line *line);

extern struct view pager_view;

static inline void
open_pager_view(struct view *prev, enum open_flags flags)
{
	open_view(prev, &pager_view, flags);
}

#endif
/* vim: set ts=8 sw=8 noexpandtab: */
