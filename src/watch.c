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
	watch->changed = WATCH_NONE;
	watch->state = WATCH_NONE;
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
	struct index_diff diff;
};

static bool
check_file_mtime(time_t *last_modified, const char *path_fmt, ...)
{
	char path[SIZEOF_STR];
	struct stat stat;
	int retval;
	bool has_changed = !!*last_modified;

	FORMAT_BUFFER(path, sizeof(path), path_fmt, retval, false);

	if (retval < 0 ||
	    lstat(path, &stat) < 0 ||
	    stat.st_mtime <= *last_modified)
		return false;

	*last_modified = stat.st_mtime;
	return has_changed;
}

static enum watch_trigger
watch_head_handler(struct watch_handler *handler, enum watch_event event, enum watch_trigger check)
{
	const struct ref *head;

	if (check_file_mtime(&handler->last_modified, "%s/HEAD", repo.git_dir))
		return WATCH_HEAD;

	// FIXME: check branch
	if ((head = get_ref_head()) &&
	    check_file_mtime(&handler->last_modified, "%s/refs/heads/%s", repo.git_dir, head->name))
		return WATCH_HEAD;

	return WATCH_NONE;
}

static enum watch_trigger
watch_stash_handler(struct watch_handler *handler, enum watch_event event, enum watch_trigger check)
{
	if (check_file_mtime(&handler->last_modified, "%s/refs/stash", repo.git_dir))
		return WATCH_STASH;

	return WATCH_NONE;
}

static enum watch_trigger
watch_index_handler(struct watch_handler *handler, enum watch_event event, enum watch_trigger check)
{
	enum watch_trigger changed = WATCH_NONE;
	struct index_diff diff;

	if (event == WATCH_EVENT_AFTER_COMMAND)
		return check_file_mtime(&handler->last_modified, "%s/index", repo.git_dir)
			? check : WATCH_NONE;

	if (event == WATCH_EVENT_SWITCH_VIEW)
		return WATCH_NONE;

	if (!index_diff(&diff, opt_show_untracked, opt_status_show_untracked_files))
		return check_file_mtime(&handler->last_modified, "%s/index", repo.git_dir)
			? check : WATCH_NONE;

	if (check & WATCH_INDEX_STAGED && diff.staged != handler->diff.staged) {
		changed |= WATCH_INDEX_STAGED;
		handler->diff.staged = diff.staged;
	}

	if (check & WATCH_INDEX_UNSTAGED && diff.unstaged != handler->diff.unstaged) {
		changed |= WATCH_INDEX_UNSTAGED;
		handler->diff.unstaged = diff.unstaged;
	}

	if (check & WATCH_INDEX_UNTRACKED && diff.untracked != handler->diff.untracked) {
		changed |= WATCH_INDEX_UNTRACKED;
		handler->diff.untracked = diff.untracked;
	}

	if (changed)
		handler->last_modified = time(NULL);

	return changed;
}

static enum watch_trigger
watch_refs_handler(struct watch_handler *handler, enum watch_event event,
		   enum watch_trigger check)
{
	if (event == WATCH_EVENT_AFTER_COMMAND)
		load_refs(true);

	return WATCH_NONE;
}

static struct watch_handler watch_handlers[] = {
	{ watch_index_handler, WATCH_INDEX },
	{ watch_head_handler, WATCH_HEAD },
	{ watch_stash_handler, WATCH_STASH },
	{ watch_refs_handler, WATCH_HEAD | WATCH_REFS },
};

static bool
watch_no_refresh(enum watch_event event)
{
	return opt_refresh_mode == REFRESH_MODE_MANUAL ||
	       (opt_refresh_mode == REFRESH_MODE_AFTER_COMMAND &&
		event != WATCH_EVENT_AFTER_COMMAND);
}

static void
watch_apply_changes(struct watch *source, enum watch_event event,
		    enum watch_trigger changed)
{
	struct watch *watch;

	if (watch_no_refresh(event))
		return;

	for (watch = watches; watch; watch = watch->next) {
		enum watch_trigger triggers = changed & watch->triggers;

		if (source == watch) {
			source->state |= triggers;
			continue;
		}

		if (event == WATCH_EVENT_AFTER_COMMAND) {
			watch->state = WATCH_NONE;
			triggers = watch->triggers;
		}

		watch->changed |= triggers;
	}
}

void
watch_apply(struct watch *source, enum watch_trigger changed)
{
	watch_apply_changes(source, WATCH_EVENT_LOAD, changed);
}

static enum watch_trigger
watch_update_event(enum watch_event event, enum watch_trigger trigger,
		   enum watch_trigger changed)
{
	time_t timestamp = 0;
	int i;

	if (watch_no_refresh(event))
		return WATCH_NONE;

	if (event == WATCH_EVENT_AFTER_COMMAND)
		timestamp = time(NULL);

	for (i = 0; i < ARRAY_SIZE(watch_handlers); i++) {
		struct watch_handler *handler = &watch_handlers[i];

		if (event == WATCH_EVENT_AFTER_COMMAND) {
			changed = handler->triggers;
			handler->last_modified = timestamp;
			continue;
		}

		if (*repo.git_dir &&
		    (trigger & handler->triggers) &&
		    (changed | handler->triggers) != changed)
			changed |= handler->check(handler, event, trigger);
	}

	if (changed)
		watch_apply_changes(NULL, event, changed);

	return changed;
}

#define watch_trigger_unmask(triggers, set) ((triggers) & ~(set))

static inline enum watch_trigger
watch_unchanged_triggers(struct watch *watch)
{
	return watch_trigger_unmask(watch->triggers, watch->changed);
}

enum watch_trigger
watch_update_single(struct watch *watch, enum watch_event event)
{
	enum watch_trigger trigger = watch_unchanged_triggers(watch);

	return watch_update_event(event, trigger, watch->changed);
}

enum watch_trigger
watch_update(enum watch_event event)
{
	enum watch_trigger trigger = WATCH_NONE;
	struct watch *watch;

	/* Collect triggers to check. Skkipping watches that are already
	 * marked dirty to avoid unnecessary checks. */
	for (watch = watches; watch; watch = watch->next)
		trigger |= watch_unchanged_triggers(watch);

	return watch_update_event(event, trigger, WATCH_NONE);
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
	enum watch_trigger old_index = watch->state & WATCH_INDEX;
	enum watch_trigger new_index = watch->changed & WATCH_INDEX;
	enum watch_trigger index = watch_trigger_unmask(new_index, old_index);
	enum watch_trigger other = watch_trigger_unmask(watch->changed, WATCH_INDEX);
	bool dirty = !!(index | other);

	watch->changed = WATCH_NONE;
	return dirty;
}

/* vim: set ts=8 sw=8 noexpandtab: */
