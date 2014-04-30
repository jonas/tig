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

#include "tig/tig.h"
#include "tig/repo.h"
#include "tig/refdb.h"
#include "tig/io.h"
#include "tig/options.h"
#include "tig/watch.h"

static struct watch *watches;

void
watch_register(struct watch *watch, enum watch_trigger triggers)
{
	watch_unregister(watch);

	watch->next = watches;
	watches = watch;

	watch->triggers = triggers;
	watch->dirty = FALSE;
}

void
watch_unregister(struct watch *watch)
{
	struct watch *pos, *prev = NULL;

	for (pos = watches; pos; prev = pos, pos = pos->next) {
		if (watch != pos)
			continue;
		if (!prev)
			watches = watch->next;
		else
			prev->next = watch->next;
		break;
	}

	memset(watch, 0, sizeof(*watch));
}

struct watch_handler {
	enum watch_trigger (*check)(struct watch_handler *handler, enum watch_event event, enum watch_trigger check);
	enum watch_trigger triggers;
	time_t last_modified;
	enum watch_trigger last_check;
};

static bool
check_file_mtime(time_t *last_modified, const char *path_fmt, ...)
{
	char path[SIZEOF_STR];
	struct stat stat;
	int retval;

	FORMAT_BUFFER(path, sizeof(path), path_fmt, retval, FALSE);

	if (retval < 0 ||
	    lstat(path, &stat) < 0 ||
	    stat.st_mtime <= *last_modified)
		return FALSE;

	*last_modified = stat.st_mtime;
	return TRUE;
}

static enum watch_trigger
watch_head_handler(struct watch_handler *handler, enum watch_event event, enum watch_trigger check)
{
	struct ref *head;

	if (*repo.git_dir &&
	    check_file_mtime(&handler->last_modified, "%s/HEAD", repo.git_dir))
		return WATCH_HEAD;

	// FIXME: check branch
	if ((head = get_ref_head()) &&
	    check_file_mtime(&handler->last_modified, "%s/refs/head/%s", repo.git_dir, head->name))
		return WATCH_HEAD;

	return WATCH_NONE;
}

static enum watch_trigger
watch_stash_handler(struct watch_handler *handler, enum watch_event event, enum watch_trigger check)
{
	if (*repo.git_dir &&
	    check_file_mtime(&handler->last_modified, "%s/refs/stash", repo.git_dir))
		return WATCH_STASH;

	return WATCH_NONE;
}

static enum watch_trigger
watch_index_handler(struct watch_handler *handler, enum watch_event event, enum watch_trigger check)
{
	enum watch_trigger changed = WATCH_NONE;
	enum watch_trigger diff = WATCH_NONE;

	if (!*repo.git_dir)
		return WATCH_NONE;

	if (event == WATCH_EVENT_AFTER_EXTERNAL)
		return check_file_mtime(&handler->last_modified, "%s/index", repo.git_dir)
			? check : WATCH_NONE;

	if (!check_file_mtime(&handler->last_modified, "%s/index", repo.git_dir) ||
	    !update_index())
		return WATCH_NONE;

	if (check & WATCH_INDEX_STAGED) {
		if (index_diff_staged())
			changed |= WATCH_INDEX_STAGED;
		else if (handler->last_check & WATCH_INDEX_STAGED)
			diff |= WATCH_INDEX_STAGED;
	}

	if (check & WATCH_INDEX_UNSTAGED) {
		if (index_diff_unstaged())
			changed |= WATCH_INDEX_UNSTAGED;
		else if (handler->last_check & WATCH_INDEX_UNSTAGED)
			diff |= WATCH_INDEX_UNSTAGED;
	}

	handler->last_check = changed;
	changed |= diff;

	if (changed)
		handler->last_modified = time(NULL);

	return changed;
}

static struct watch_handler watch_handlers[] = {
	{ watch_index_handler, WATCH_INDEX_STAGED | WATCH_INDEX_UNSTAGED },
	{ watch_head_handler, WATCH_HEAD },
	{ watch_stash_handler, WATCH_STASH },
};

static bool
watch_no_refresh(enum watch_event event)
{
	return opt_refresh_mode == REFRESH_MODE_MANUEL ||
	       (opt_refresh_mode == REFRESH_MODE_AFTER_COMMAND &&
		event != WATCH_EVENT_AFTER_EXTERNAL);
}

enum watch_trigger
watch_update(enum watch_event event)
{
	enum watch_trigger trigger = WATCH_NONE;
	enum watch_trigger changed = WATCH_NONE;
	struct watch *watch;
	int i;

	if (watch_no_refresh(event))
		return changed;

	/* Collect triggers to check. Skkipping watches that are already
	 * marked dirty to avoid unnecessary checks. */
	for (watch = watches; watch; watch = watch->next)
		if (!watch->dirty)
			trigger |= watch->triggers;

	for (i = 0; trigger && i < ARRAY_SIZE(watch_handlers); i++) {
		struct watch_handler *handler = &watch_handlers[i];

		if (trigger & handler->triggers)
			changed |= handler->check(handler, event, trigger);
	}

	for (watch = watches; watch; watch = watch->next)
		if (changed & watch->triggers)
			watch->dirty = TRUE;

	return changed;
}

int
watch_periodic(int interval)
{
	static time_t last_update;
	int delay = -1;

	if (watches && interval > 0) {
		time_t now = time(NULL);

		if (!last_update)
			last_update = now;
		if (last_update + interval <= now) {
			watch_update(WATCH_EVENT_PERIODIC);
			last_update = now;
		}

		delay = (now - last_update + interval) * 1000;
	}

	return delay;
}

bool
watch_dirty(struct watch *watch)
{
	bool dirty = FALSE;

	if (watch) {
		dirty = watch->dirty;
		watch->dirty = FALSE;
	}

	return dirty;
}

/* vim: set ts=8 sw=8 noexpandtab: */
