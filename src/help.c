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

/*
 * Help backend
 */

struct help_state {
	int keys_width;
	int name_width;
};

struct help {
	struct keymap *keymap;
	enum request request;
	union {
		const char *text;
		const struct request_info *req_info;
	} data;
};

static bool
help_draw(struct view *view, struct line *line, unsigned int lineno)
{
	struct help *help = line->data;
	struct keymap *keymap = help->keymap;
	struct help_state *state = view->private;

	if (line->type == LINE_HELP_KEYMAP) {
		draw_formatted(view, line->type, "[%c] %s bindings",
			       keymap->hidden ? '+' : '-', keymap->name);

	} else if (line->type == LINE_HELP_GROUP || !keymap) {
		draw_text(view, line->type, help->data.text);

	} else if (help->request > REQ_RUN_REQUESTS) {
		struct run_request *req = get_run_request(help->request);
		const char *key = get_key_name(&req->input);
		const char *sep = req->internal ? ":" : "!";
		int i;

		if (draw_field(view, LINE_DEFAULT, key, state->keys_width + 2, ALIGN_RIGHT, FALSE))
			return TRUE;

		for (i = 0; req->argv[i]; i++) {
			if (draw_formatted(view, LINE_HELP_ACTION, "%s%s", sep, req->argv[i]))
				return TRUE;
			sep = " ";
		}

	} else {
		const struct request_info *req_info = help->data.req_info;
		const char *key = get_keys(keymap, req_info->request, TRUE);

		if (draw_field(view, LINE_DEFAULT, key, state->keys_width + 2, ALIGN_RIGHT, FALSE))
			return TRUE;

		if (draw_field(view, LINE_HELP_ACTION, enum_name(*req_info), state->name_width, ALIGN_LEFT, FALSE))
			return TRUE;

		draw_text(view, LINE_DEFAULT, req_info->help);
	}

	return TRUE;
}

bool
help_grep(struct view *view, struct line *line)
{
	struct help *help = line->data;
	struct keymap *keymap = help->keymap;

	if (line->type == LINE_HELP_KEYMAP) {
		const char *text[] = { keymap->name, NULL };

		return grep_text(view, text);

	} else if (line->type == LINE_HELP_GROUP || !keymap) {
		const char *text[] = { help->data.text, NULL };

		return grep_text(view, text);

	} else if (help->request > REQ_RUN_REQUESTS) {
		struct run_request *req = get_run_request(help->request);
		const char *key = get_key_name(&req->input);
		char buf[SIZEOF_STR] = "";
		const char *text[] = { key, buf, NULL };

		if (!argv_to_string(req->argv, buf, sizeof(buf), " "))
			return FALSE;

		return grep_text(view, text);

	} else {
		const struct request_info *req_info = help->data.req_info;
		const char *key = get_keys(keymap, req_info->request, TRUE);
		const char *text[] = { key, enum_name(*req_info), req_info->help, NULL };

		return grep_text(view, text);
	}
}

struct help_request_iterator {
	struct view *view;
	struct keymap *keymap;
	bool add_title;
	const char *group;
};

static bool
add_help_line(struct view *view, struct help **help_ptr, struct keymap *keymap, enum line_type type)
{
	struct help *help;

	if (!add_line_alloc(view, &help, type, 0, FALSE))
		return FALSE;
	help->keymap = keymap;
	if (help_ptr)
		*help_ptr = help;
	return TRUE;
}

static bool
add_help_headers(struct help_request_iterator *iterator, const char *group)
{
	struct help *help;

	if (iterator->add_title) {
		iterator->add_title = FALSE;
		if (!add_help_line(iterator->view, &help, iterator->keymap, LINE_HELP_KEYMAP) ||
		    iterator->keymap->hidden)
			return FALSE;
	}

	if (iterator->group != group) {
		iterator->group = group;
		if (!add_help_line(iterator->view, &help, iterator->keymap, LINE_HELP_GROUP))
			return FALSE;
		help->data.text = group;
	}

	return TRUE;
}

static bool
help_open_keymap(void *data, const struct request_info *req_info, const char *group)
{
	struct help_request_iterator *iterator = data;
	struct help_state *state = iterator->view->private;
	struct keymap *keymap = iterator->keymap;
	const char *key = get_keys(keymap, req_info->request, TRUE);
	struct help *help;

	if (req_info->request == REQ_NONE || !key || !*key)
		return TRUE;

	if (!add_help_headers(iterator, group) ||
	    !add_help_line(iterator->view, &help, iterator->keymap, LINE_DEFAULT))
		return FALSE;

	state->keys_width = MAX(state->keys_width, strlen(key));
	state->name_width = MAX(state->name_width, strlen(enum_name(*req_info)));

	help->data.req_info = req_info;
	help->request = req_info->request;

	return TRUE;
}

static void
help_open_keymap_run_requests(struct help_request_iterator *iterator)
{
	struct view *view = iterator->view;
	struct help_state *state = view->private;
	struct keymap *keymap = iterator->keymap;
	const char *group = "External commands:";
	enum request request = REQ_RUN_REQUESTS + 1;
	struct help *help;

	for (; TRUE; request++) {
		struct run_request *req = get_run_request(request);
		const char *key;

		if (!req)
			break;

		if (req->keymap != keymap ||
		    !*(key = get_key_name(&req->input)))
			continue;

		if (!add_help_headers(iterator, group) ||
		    !add_help_line(view, &help, keymap, LINE_DEFAULT))
			return;

		state->keys_width = MAX(state->keys_width, strlen(key));

		help->request = request;
	}
}

static bool
help_open(struct view *view, enum open_flags flags)
{
	struct keymap *keymap;
	struct help *help;

	reset_view(view);

	if (!add_help_line(view, &help, NULL, LINE_DEFAULT))
		return FALSE;
	help->data.text = "Quick reference for tig keybindings:";

	if (!add_help_line(view, &help, NULL, LINE_DEFAULT))
		return FALSE;
	help->data.text = "";

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
	struct help *help = line->data;

	switch (request) {
	case REQ_ENTER:
		if (line->type == LINE_HELP_KEYMAP) {
			struct keymap *keymap = help->keymap;

			keymap->hidden = !keymap->hidden;
			refresh_view(view);
		}
		return REQ_NONE;

	default:
		return request;
	}
}

void
help_select(struct view *view, struct line *line)
{
}

struct view_ops help_ops = {
	"line",
	{ "help" },
	"",
	VIEW_NO_GIT_DIR,
	sizeof(struct help_state),
	help_open,
	NULL,
	help_draw,
	help_request,
	help_grep,
	help_select,
	NULL,
};

/* vim: set ts=8 sw=8 noexpandtab: */
