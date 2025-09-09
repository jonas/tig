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

#include "tig/argv.h"
#include "tig/view.h"
#include "tig/search.h"
#include "tig/prompt.h"
#include "tig/draw.h"

extern const struct menu_item toggle_menu_items[];

static const char collapse_expand_names[2][13] = {
	"Collapse all",
	"Expand all"
};
static struct keymap collapse_expand_keymap = {
	collapse_expand_names[0], NULL, 0, false
};
static struct keymap toggle_menu_keymap = {
	"toggle", NULL, 0, false
};

/*
 * Help backend
 */

struct help_state {
	int keys_width;
	int name_width;
};

struct help {
	enum request request;
	const char *key;
	union {
		struct keymap *keymap;
		struct menu_item *menu;
	} item;
	union {
		const char *text;
		const struct request_info *req_info;
	} data;
};

static bool
help_draw(struct view *view, struct line *line, unsigned int lineno)
{
	struct help *help = line->data;
	const struct keymap *keymap = help->item.keymap;
	struct help_state *state = view->private;

	if (line->type == LINE_SECTION) {
		draw_formatted(view, line->type, "[%c] %s %s",
				keymap->hidden ? '+' : '-', keymap->name,
				keymap == &collapse_expand_keymap ? "sections" : "bindings");

	} else if (line->type == LINE_HELP_GROUP || !keymap) {
		draw_text(view, line->type, help->data.text);

	} else if (line->type == LINE_HELP_TOGGLE) {
		struct menu_item *item = help->item.menu;
		char key[2];

		if (!string_nformat(key, sizeof(key), NULL, "%c", item->hotkey))
			return true;
		if (draw_field(view, LINE_DEFAULT, key, state->keys_width + 2, ALIGN_RIGHT, false))
			return true;

		if (draw_field(view, LINE_HELP_ACTION, item->data, state->name_width, ALIGN_LEFT, false))
			return true;

		draw_formatted(view, LINE_DEFAULT, "Toggle %s", item->text);

	} else if (help->request > REQ_RUN_REQUESTS) {
		struct run_request *req = get_run_request(help->request);

		if (draw_field(view, LINE_DEFAULT, help->key, state->keys_width + 2, ALIGN_RIGHT, false))
			return true;

		/* If there is req->help text to draw, then first draw req->name as a fixed-width field */
		if (req->help) {
			if (draw_field(view, LINE_HELP_ACTION, req->name, state->name_width, ALIGN_LEFT, false))
				return true;
			draw_text(view, LINE_DEFAULT, req->help);
		}

		/* Else just draw req->name as free-form text */
		else draw_text(view, LINE_HELP_ACTION, req->name);

	} else {
		const struct request_info *req_info = help->data.req_info;

		if (draw_field(view, LINE_DEFAULT, help->key, state->keys_width + 2, ALIGN_RIGHT, false))
			return true;

		/* If there is req_info->help text to draw, then first draw req_info->name as a fixed-width field */
		if (req_info->help) {
			if (draw_field(view, LINE_HELP_ACTION, enum_name(req_info->name), state->name_width, ALIGN_LEFT, false))
				return true;
			draw_text(view, LINE_DEFAULT, req_info->help);
		}

		/* Else just draw req_info->name as free-form text */
		else draw_text(view, LINE_HELP_ACTION, enum_name(req_info->name));
	}

	return true;
}

static bool
help_grep(struct view *view, struct line *line)
{
	struct help *help = line->data;
	const struct keymap *keymap = help->item.keymap;

	if (line->type == LINE_SECTION) {
		const char *text[] = { keymap->name, NULL };

		return grep_text(view, text);

	} else if (line->type == LINE_HELP_GROUP || !keymap) {
		const char *text[] = { help->data.text, NULL };

		return grep_text(view, text);

	} else if (line->type == LINE_HELP_TOGGLE) {
		char key[2];
		const char *text[] = { key, help->item.menu->data, "Toggle", help->item.menu->text, NULL };

		if (!string_nformat(key, sizeof(key), NULL, "%c", help->item.menu->hotkey))
			return false;

		return grep_text(view, text);

	} else if (help->request > REQ_RUN_REQUESTS) {
		struct run_request *req = get_run_request(help->request);
		const char *text[] = { help->key, req->name, req->help, NULL };

		return grep_text(view, text);

	} else {
		const struct request_info *req_info = help->data.req_info;
		const char *text[] = { help->key, enum_name(req_info->name), req_info->help, NULL };

		return grep_text(view, text);
	}
}

struct help_request_iterator {
	struct view *view;
	struct keymap *keymap;
};

static bool
add_help_line(struct view *view, struct help **help_ptr, struct keymap *keymap, enum line_type type)
{
	struct help *help;

	if (!add_line_alloc(view, &help, type, 0, false))
		return false;
	help->item.keymap = keymap;
	if (help_ptr)
		*help_ptr = help;
	return true;
}

static bool
help_keys_visitor(void *data, const char *group, struct keymap *keymap,
		  enum request request, const char *key,
		  const struct request_info *req_info, const struct run_request *run_req)
{
	struct help_request_iterator *iterator = data;
	struct view *view = iterator->view;
	struct help_state *state = view->private;
	struct help *help;

	if (iterator->keymap != keymap) {
		iterator->keymap = keymap;
		if (!add_help_line(view, &help, keymap, LINE_SECTION))
			return false;
	}

	if (keymap->hidden)
		return true;

	if (group) {
		if (!add_help_line(view, &help, keymap, LINE_HELP_GROUP))
			return false;
		help->data.text = group;
	}

	if (!add_help_line(view, &help, keymap, LINE_DEFAULT))
		return false;

	int len = strlen(key);
	state->keys_width = MAX(state->keys_width, len);
	/* Since the key string is available, cache it for when help lines need to be drawn */
	help->key = len > 0 ? strdup(key) : NULL;

	help->request = request;

	if (req_info) {
		help->data.req_info = req_info;
		/* Include req_info->name in the MAX calculation but only if there is help text */
		if (req_info->help && strlen(req_info->help) > 0)
			state->name_width = MAX(state->name_width, strlen(enum_name(req_info->name)));
	}

	if (run_req) {
		/* Include run_req->name in the MAX calculation but only if there is help text */
		if (run_req->help && strlen(run_req->help) > 0)
			state->name_width = MAX(state->name_width, strlen(run_req->name));
	}

	return true;
}

static bool
help_collapse_expand_keys_visitor(void *data, const char *group, struct keymap *keymap,
		  enum request request, const char *key,
		  const struct request_info *req_info, const struct run_request *run_req)
{
	struct help_request_iterator *iterator = data;

	if (iterator->keymap != keymap) {
		iterator->keymap = keymap;
		keymap->hidden = collapse_expand_keymap.hidden;
	}

	return true;
}

static enum status_code
help_open(struct view *view, enum open_flags flags)
{
	struct help_request_iterator iterator = { view };
	struct help_state *state = view->private;
	struct help *help;

	/* Need to free any key strings that have been cached */
	int i; for (i = 0; i < view->lines; i++) {
		const struct help *h = view->line[i].data;
		free((void *)h->key);
	}

	reset_view(view);

	if (!add_help_line(view, &help, NULL, LINE_HEADER))
		return ERROR_OUT_OF_MEMORY;
	help->data.text = "Quick reference for tig keybindings:";

	if (!add_help_line(view, &help, &collapse_expand_keymap, LINE_SECTION))
		return ERROR_OUT_OF_MEMORY;

	if (!add_help_line(view, &help, NULL, LINE_DEFAULT))
		return ERROR_OUT_OF_MEMORY;
	help->data.text = "";

	if (!foreach_key(help_keys_visitor, &iterator, true))
		return error("Failed to render key bindings");

	if (!add_help_line(view, &help, &toggle_menu_keymap, LINE_SECTION))
		return ERROR_OUT_OF_MEMORY;
	if (!toggle_menu_keymap.hidden) {
		int i;

		if (!add_help_line(view, &help, NULL, LINE_HELP_GROUP))
			return ERROR_OUT_OF_MEMORY;
		help->data.text = "Toggle keys (enter: o <key>):";

		for (i = 0; toggle_menu_items[i].data; i++) {
			state->name_width = MAX(state->name_width, strlen(toggle_menu_items[i].data));
			if (!add_help_line(view, &help, (struct keymap *)&toggle_menu_items[i], LINE_HELP_TOGGLE))
				return ERROR_OUT_OF_MEMORY;
		}
	}

	return SUCCESS;
}

static enum request
help_request(struct view *view, enum request request, struct line *line)
{
	struct help *help = line->data;

	switch (request) {
	case REQ_ENTER:
		if (line->type == LINE_SECTION) {
			struct keymap *keymap = help->item.keymap;

			keymap->hidden = !keymap->hidden;
			if (keymap == &collapse_expand_keymap) {
				struct help_request_iterator iterator = { view };

				collapse_expand_keymap.name = collapse_expand_names[keymap->hidden];
				foreach_key(help_collapse_expand_keys_visitor, &iterator, true);
				toggle_menu_keymap.hidden = keymap->hidden;
			}
			refresh_view(view);
		}
		return REQ_NONE;

	case REQ_REFRESH:
		refresh_view(view);
		return REQ_NONE;

	default:
		return request;
	}
}

static void
help_select(struct view *view, struct line *line)
{
}

static struct view_ops help_ops = {
	"line",
	"",
	VIEW_NO_GIT_DIR | VIEW_REFRESH,
	sizeof(struct help_state),
	help_open,
	NULL,
	help_draw,
	help_request,
	help_grep,
	help_select,
	NULL,
};

DEFINE_VIEW(help);

/* vim: set ts=8 sw=8 noexpandtab: */
