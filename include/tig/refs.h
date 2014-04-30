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

#ifndef TIG_REFS_H
#define TIG_REFS_H

#include "tig/view.h"

extern struct view refs_view;

static inline void
open_refs_view(struct view *prev, enum open_flags flags)
{
	open_view(prev, &refs_view, flags);
}

#endif
/* vim: set ts=8 sw=8 noexpandtab: */
