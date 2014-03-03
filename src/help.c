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

#include "tig/argv.h"
#include "tig/view.h"
#include "tig/draw.h"
#include "tig/pager.h"

/*
 * Help backend
 */

static bool
help_draw(struct view *view, struct line *line, unsigned int lineno)
{
	if (line->type == LINE_HELP_KEYMAP) {
		struct keymap *keymap = line->data;

		draw_formatted(view, line->type, "[%c] %s bindings",
			       keymap->hidden ? '+' : '-', keymap->name);
		return TRUE;
	} else {
		return pager_draw(view, line, lineno);
	}
}

static bool
help_open_keymap_title(struct view *view, struct keymap *keymap)
{
	add_line(view, keymap, LINE_HELP_KEYMAP, 0, FALSE);
	return keymap->hidden;
}

struct help_request_iterator {
	struct view *view;
	struct keymap *keymap;
	bool add_title;
	const char *group;
};

static bool
help_open_keymap(void *data, const struct request_info *req_info, const char *group)
{
	struct help_request_iterator *iterator = data;
	struct view *view = iterator->view;
	struct keymap *keymap = iterator->keymap;
	const char *key = get_keys(keymap, req_info->request, TRUE);

	if (req_info->request == REQ_NONE || !key || !*key)
		return TRUE;

	if (iterator->add_title && help_open_keymap_title(view, keymap))
		return FALSE;
	iterator->add_title = FALSE;

	if (iterator->group != group) {
		add_line_text(view, group, LINE_HELP_GROUP);
		iterator->group = group;
	}

	add_line_format(view, LINE_DEFAULT, "    %-25s %-20s %s", key,
			enum_name(*req_info), req_info->help);
	return TRUE;
}

static void
help_open_keymap_run_requests(struct help_request_iterator *iterator)
{
	struct view *view = iterator->view;
	struct keymap *keymap = iterator->keymap;
	char buf[SIZEOF_STR];
	const char *group = "External commands:";
	int i;

	for (i = 0; TRUE; i++) {
		struct run_request *req = get_run_request(REQ_NONE + i + 1);
		const char *key;

		if (!req)
			break;

		if (req->keymap != keymap)
			continue;

		key = get_key_name(&req->input);
		if (!*key)
			key = "(no key defined)";

		if (iterator->add_title && help_open_keymap_title(view, keymap))
			return;
		iterator->add_title = FALSE;

		if (group) {
			add_line_text(view, group, LINE_HELP_GROUP);
			group = NULL;
		}

		if (!argv_to_string(req->argv, buf, sizeof(buf), " "))
			return;

		add_line_format(view, LINE_DEFAULT, "    %-25s `%s`", key, buf);
	}
}

static bool
help_open(struct view *view, enum open_flags flags)
{
	struct keymap *keymap;

	reset_view(view);
	add_line_text(view, "Quick reference for tig keybindings:", LINE_DEFAULT);
	add_line_text(view, "", LINE_DEFAULT);

	for (keymap = get_keymaps(); keymap; keymap = keymap->next) {
		struct help_request_iterator iterator = { view, keymap, TRUE };

		if (foreach_request(help_open_keymap, &iterator))
			help_open_keymap_run_requests(&iterator);
	}

	return TRUE;
}

static enum request
help_request(struct view *view, enum request request, struct line *line)
{
	switch (request) {
	case REQ_ENTER:
		if (line->type == LINE_HELP_KEYMAP) {
			struct keymap *keymap = line->data;

			keymap->hidden = !keymap->hidden;
			refresh_view(view);
		}

		return REQ_NONE;
	default:
		return pager_request(view, request, line);
	}
}

static void
help_done(struct view *view)
{
	int i;

	for (i = 0; i < view->lines; i++)
		if (view->line[i].type == LINE_HELP_KEYMAP)
			view->line[i].data = NULL;
}

struct view_ops help_ops = {
	"line",
	{ "help" },
	"",
	VIEW_NO_GIT_DIR,
	0,
	help_open,
	NULL,
	help_draw,
	help_request,
	pager_grep,
	pager_select,
	help_done,
};

/* vim: set ts=8 sw=8 noexpandtab: */
