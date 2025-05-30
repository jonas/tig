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

static const char collapse_expand_names[2][14] = {
	"Collapse all", "Expand all"
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

	if (line->type == LINE_SECTION && keymap) {
		draw_formatted(view, line->type, "[%c] %s %s",
				keymap->hidden ? '+' : '-', keymap->name,
				(keymap == &collapse_expand_keymap) ? "sections" : "bindings" );

	} else if (line->type == LINE_HELP_GROUP || !keymap) {
		draw_text(view, line->type, help->data.text);

	} else if (line->type == LINE_HELP_TOGGLE) {
		struct menu_item *item = help->item.menu;
		char text_buf[40];

		snprintf(text_buf, sizeof(text_buf), "%c", item->hotkey);
		if (draw_field(view, LINE_DEFAULT, text_buf, 28, ALIGN_RIGHT, false))
			return true;

		if (draw_field(view, LINE_HELP_ACTION, (char*)item->data, 28, ALIGN_LEFT, false))
			return true;

		snprintf(text_buf, sizeof(text_buf), "Toggle %s", item->text);
		draw_text(view, LINE_DEFAULT, text_buf);

	} else if (help->request > REQ_RUN_REQUESTS) {
		struct run_request *req = get_run_request(help->request);
		const char *key = get_keys(keymap, help->request, true);
		const char *sep = format_run_request_flags(req);
		int i;

		if (draw_field(view, LINE_DEFAULT, key, state->keys_width + 2, ALIGN_RIGHT, false))
			return true;

		for (i = 0; req->argv[i]; i++) {
			if (draw_formatted(view, LINE_HELP_ACTION, "%s%s", sep, req->argv[i]))
				return true;
			sep = " ";
		}

	} else {
		const struct request_info *req_info = help->data.req_info;
		const char *key = get_keys(keymap, req_info->request, true);

		if (draw_field(view, LINE_DEFAULT, key, state->keys_width + 2, ALIGN_RIGHT, false))
			return true;

		if (draw_field(view, LINE_HELP_ACTION, enum_name(req_info->name), state->name_width, ALIGN_LEFT, false))
			return true;

		draw_text(view, LINE_DEFAULT, req_info->help);
	}

	return true;
}

static bool
help_grep(struct view *view, struct line *line)
{
	struct help *help = line->data;
	const struct keymap *keymap = help->item.keymap;

	if (line->type == LINE_SECTION && keymap) {
		const char *text[] = { keymap->name, NULL };

		return grep_text(view, text);

	} else if (line->type == LINE_HELP_GROUP || !keymap) {
		const char *text[] = { help->data.text, NULL };

		return grep_text(view, text);

	} else if (line->type == LINE_HELP_TOGGLE) {
		const char *text[] = { help->item.menu->text, NULL };

		return grep_text(view, text);
	} else if (help->request > REQ_RUN_REQUESTS) {
		struct run_request *req = get_run_request(help->request);
		const char *key = get_keys(keymap, help->request, true);
		char buf[SIZEOF_STR] = "";
		const char *text[] = { key, buf, NULL };

		if (!argv_to_string(req->argv, buf, sizeof(buf), " "))
			return false;

		return grep_text(view, text);

	} else {
		const struct request_info *req_info = help->data.req_info;
		const char *key = get_keys(keymap, req_info->request, true);
		const char *text[] = { key, enum_name(req_info->name), req_info->help, NULL };

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

	state->keys_width = MAX(state->keys_width, strlen(key));
	help->request = request;

	if (req_info) {
		state->name_width = MAX(state->name_width, strlen(enum_name(req_info->name)));
		help->data.req_info = req_info;
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
	struct help *help;
	const struct menu_item *menu = toggle_menu_items;
	int i;

	reset_view(view);

	if (!add_help_line(view, &help, NULL, LINE_HEADER))
		return ERROR_OUT_OF_MEMORY;
	help->data.text = "Quick reference for tig keybindings:";

	if (!add_help_line(view, &help, &collapse_expand_keymap, LINE_SECTION))
		return ERROR_OUT_OF_MEMORY;

	if (!add_help_line(view, &help, NULL, LINE_DEFAULT))
		return ERROR_OUT_OF_MEMORY;
	help->data.text = "";

	if (!foreach_key(help_keys_visitor, &iterator, true)) {
		error("Failed to render key bindings");
		return ERROR_OUT_OF_MEMORY;
	}

	if (!add_help_line(view, &help, &toggle_menu_keymap, LINE_SECTION))
		return ERROR_OUT_OF_MEMORY;
	if (!toggle_menu_keymap.hidden) {
		if (!add_help_line(view, &help, NULL, LINE_HELP_GROUP))
			return ERROR_OUT_OF_MEMORY;
		help->data.text = "Toggle keys (enter: o <key>):";

		i = 0;
		while (menu[i].data)
		{
			if (!add_help_line(view, &help, (struct keymap *)&menu[i], LINE_HELP_TOGGLE))
				return ERROR_OUT_OF_MEMORY;
			i++;
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
