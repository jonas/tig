/* Copyright (c) 2006-2025 Jonas Fonseca <jonas.fonseca@gmail.com>
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

#ifndef TIG_STAGE_H
#define TIG_STAGE_H

#include "tig/view.h"

struct status;

extern struct view stage_view;

void open_stage_view(struct view *prev, struct status *status, enum line_type type, enum open_flags flags);

#endif
/* vim: set ts=8 sw=8 noexpandtab: */
