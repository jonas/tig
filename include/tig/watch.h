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

#ifndef TIG_WATCH_H
#define TIG_WATCH_H

#include "tig/tig.h"
#include "tig/types.h"

enum watch_event {
	WATCH_EVENT_SWITCH_VIEW,
	WATCH_EVENT_AFTER_EXTERNAL,
	WATCH_EVENT_PERIODIC,
};

enum watch_trigger {
	WATCH_NONE		= 0,
	WATCH_INDEX_STAGED	= 1 << 0,
	WATCH_INDEX_UNSTAGED	= 1 << 1,
	WATCH_HEAD		= 1 << 2,
	WATCH_STASH		= 1 << 3,
};

struct watch {
	struct watch *next;
	enum watch_trigger triggers;
	bool dirty;
};

void watch_register(struct watch *watch, enum watch_trigger triggers);
void watch_unregister(struct watch *watch);
bool watch_dirty(struct watch *watch);
enum watch_trigger watch_update(enum watch_event event);
int watch_periodic(int interval);

#endif
/* vim: set ts=8 sw=8 noexpandtab: */
