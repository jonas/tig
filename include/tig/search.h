/* Copyright (c) 2006-2024 Jonas Fonseca <jonas.fonseca@gmail.com>
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

#ifndef TIG_SEARCH_H
#define TIG_SEARCH_H

#include "tig/view.h"

void reset_search(struct view *view);
void search_view(struct view *view, enum request request);
void find_next(struct view *view, enum request request);
void find_merge(struct view *view, enum request request);
bool grep_text(struct view *view, const char *text[]);

#endif
/* vim: set ts=8 sw=8 noexpandtab: */
