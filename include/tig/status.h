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

#ifndef TIG_STATUS_H
#define TIG_STATUS_H

#include "tig/view.h"
#include "tig/line.h"

struct status {
	char status;
	struct {
		mode_t mode;
		char rev[SIZEOF_REV];
		char name[SIZEOF_STR];
	} old;
	struct {
		mode_t mode;
		char rev[SIZEOF_REV];
		char name[SIZEOF_STR];
	} new;
};

bool status_update_file(struct status *status, enum line_type type);
bool status_update_files(struct view *view, struct line *line);
bool status_get_diff(struct status *file, const char *buf, size_t bufsize);

bool status_revert(struct status *status, enum line_type type, bool has_none);
bool status_exists(struct view *view, struct status *status, enum line_type type);

bool status_stage_info_(char *buf, size_t bufsize,
			enum line_type type, struct status *status);
#define status_stage_info(buf, type, status) \
	status_stage_info_(buf, sizeof(buf), type, status)
extern struct view status_view;

void open_status_view(struct view *prev, bool untracked_only, enum open_flags flags);

#endif
/* vim: set ts=8 sw=8 noexpandtab: */
