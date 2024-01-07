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

#ifndef TIG_WATCH_H
#define TIG_WATCH_H

#include "tig/tig.h"
#include "tig/types.h"

enum watch_event {
	WATCH_EVENT_SWITCH_VIEW,
	WATCH_EVENT_AFTER_COMMAND,
	WATCH_EVENT_LOAD,
	WATCH_EVENT_PERIODIC,
};

enum watch_trigger {
	WATCH_NONE			= 0,
	WATCH_INDEX_STAGED_YES		= 1 << 0,
	WATCH_INDEX_STAGED_NO		= 1 << 1,
	WATCH_INDEX_UNSTAGED_YES	= 1 << 2,
	WATCH_INDEX_UNSTAGED_NO		= 1 << 3,
	WATCH_INDEX_UNTRACKED_YES	= 1 << 4,
	WATCH_INDEX_UNTRACKED_NO	= 1 << 5,
	WATCH_HEAD			= 1 << 6,
	WATCH_STASH			= 1 << 7,
	WATCH_REFS			= 1 << 8,

	WATCH_INDEX_STAGED = WATCH_INDEX_STAGED_YES | WATCH_INDEX_STAGED_NO,
	WATCH_INDEX_UNSTAGED = WATCH_INDEX_UNSTAGED_YES | WATCH_INDEX_UNSTAGED_NO,
	WATCH_INDEX_UNTRACKED = WATCH_INDEX_UNTRACKED_YES | WATCH_INDEX_UNTRACKED_NO,
	WATCH_INDEX = WATCH_INDEX_STAGED | WATCH_INDEX_UNSTAGED | WATCH_INDEX_UNTRACKED,
};

struct watch {
	struct watch *next;
	enum watch_trigger triggers;
	enum watch_trigger changed;
	enum watch_trigger state;
};

void watch_register(struct watch *watch, enum watch_trigger triggers);
void watch_unregister(struct watch *watch);
bool watch_dirty(struct watch *watch);
enum watch_trigger watch_update(enum watch_event event);
enum watch_trigger watch_update_single(struct watch *watch, enum watch_event event);
void watch_apply(struct watch *source, enum watch_trigger changed);
int watch_periodic(int interval);

#endif
/* vim: set ts=8 sw=8 noexpandtab: */
