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
#include "tig/view.h"
#include "tig/display.h"
#include "tig/options.h"
#include "tig/prompt.h"

typedef enum input_status (*input_handler)(void *data, char *buf, struct key_input *input);

static char *
prompt_input(const char *prompt, input_handler handler, void *data)
{
	enum input_status status = INPUT_OK;
	static char buf[SIZEOF_STR];
	unsigned char chars_length[SIZEOF_STR];
	struct key_input input;
	int pos = 0, chars = 0;

	buf[pos] = 0;

	while (status == INPUT_OK || status == INPUT_SKIP) {
		report("%s%.*s", prompt, pos, buf);

		switch (get_input(pos + 1, &input, FALSE)) {
		case KEY_RETURN:
		case KEY_ENTER:
		case '\n':
			status = pos ? INPUT_STOP : INPUT_CANCEL;
			break;

		case KEY_BACKSPACE:
			if (pos > 0) {
				int len = chars_length[--chars];

				pos -= len;
				buf[pos] = 0;
			} else {
				status = INPUT_CANCEL;
			}
			break;

		case KEY_ESC:
			status = INPUT_CANCEL;
			break;

		default:
			if (pos >= sizeof(buf)) {
				report("Input string too long");
				return NULL;
			}

			status = handler(data, buf, &input);
			if (status == INPUT_OK) {
				int len = strlen(input.data.bytes);

				string_ncopy_do(buf + pos, sizeof(buf) - pos, input.data.bytes, len);
				pos += len;
				chars_length[chars++] = len;
			}
		}
	}

	report_clear();

	if (status == INPUT_CANCEL)
		return NULL;

	buf[pos++] = 0;

	return buf;
}

static enum input_status
prompt_yesno_handler(void *data, char *buf, struct key_input *input)
{
	unsigned long c = key_input_to_unicode(input);

	if (c == 'y' || c == 'Y')
		return INPUT_STOP;
	if (c == 'n' || c == 'N')
		return INPUT_CANCEL;
	return INPUT_SKIP;
}

bool
prompt_yesno(const char *prompt)
{
	char prompt2[SIZEOF_STR];

	if (!string_format(prompt2, "%s [Yy/Nn]", prompt))
		return FALSE;

	return !!prompt_input(prompt2, prompt_yesno_handler, NULL);
}

static enum input_status
read_prompt_handler(void *data, char *buf, struct key_input *input)
{
	unsigned long c = key_input_to_unicode(input);

	return unicode_width(c, 8) ? INPUT_OK : INPUT_SKIP;
}

char *
read_prompt(const char *prompt)
{
	return prompt_input(prompt, read_prompt_handler, NULL);
}

bool
prompt_menu(const char *prompt, const struct menu_item *items, int *selected)
{
	enum input_status status = INPUT_OK;
	struct key_input input;
	int size = 0;

	while (items[size].text)
		size++;

	assert(size > 0);

	while (status == INPUT_OK) {
		const struct menu_item *item = &items[*selected];
		char hotkey[] = { '[', (char) item->hotkey, ']', ' ', 0 };
		int i;

		report("%s (%d of %d) %s%s", prompt, *selected + 1, size,
			item->hotkey ? hotkey : "", item->text);

		switch (get_input(COLS - 1, &input, FALSE)) {
		case KEY_RETURN:
		case KEY_ENTER:
		case '\n':
			status = INPUT_STOP;
			break;

		case KEY_LEFT:
		case KEY_UP:
			*selected = *selected - 1;
			if (*selected < 0)
				*selected = size - 1;
			break;

		case KEY_RIGHT:
		case KEY_DOWN:
			*selected = (*selected + 1) % size;
			break;

		case KEY_ESC:
			status = INPUT_CANCEL;
			break;

		default:
			for (i = 0; items[i].text; i++)
				if (items[i].hotkey == input.data.bytes[0]) {
					*selected = i;
					status = INPUT_STOP;
					break;
				}
		}
	}

	report_clear();

	return status != INPUT_CANCEL;
}

enum request
run_prompt_command(struct view *view, char *cmd)
{
	enum request request;

	if (!cmd)
		return REQ_NONE;

	if (string_isnumber(cmd)) {
		int lineno = view->pos.lineno + 1;

		if (parse_int(&lineno, cmd, 1, view->lines + 1) == SUCCESS) {
			select_view_line(view, lineno - 1);
			report_clear();
		} else {
			report("Unable to parse '%s' as a line number", cmd);
		}
	} else if (iscommit(cmd)) {
		string_ncopy(view->env->search, cmd, strlen(cmd));
		return REQ_JUMP_COMMIT;

	} else if (strlen(cmd) == 1) {
		struct key_input input = { { cmd[0] } };

		return get_keybinding(&view->ops->keymap, &input);

	} else if (cmd[0] == '/' || cmd[0] == '?') {
		const char *search = cmd + 1;

		if (!strcmp(search, view->env->search))
			return cmd[0] == '/' ? REQ_FIND_NEXT : REQ_FIND_PREV;

		string_ncopy(view->env->search, search, strlen(search));
		return cmd[0] == '/' ? REQ_SEARCH : REQ_SEARCH_BACK;

	} else if (cmd[0] == '!') {
		struct view *next = VIEW(REQ_VIEW_PAGER);
		const char *argv[SIZEOF_ARG];
		int argc = 0;

		cmd++;
		/* When running random commands, initially show the
		 * command in the title. However, it maybe later be
		 * overwritten if a commit line is selected. */
		string_ncopy(next->ref, cmd, strlen(cmd));

		if (!argv_from_string(argv, &argc, cmd)) {
			report("Too many arguments");
		} else if (!argv_format(view->env, &next->argv, argv, FALSE, TRUE)) {
			report("Argument formatting failed");
		} else {
			next->dir = NULL;
			open_view(view, REQ_VIEW_PAGER, OPEN_PREPARED);
		}

	} else {
		request = get_request(cmd);
		if (request != REQ_UNKNOWN)
			return request;

		char *args = strchr(cmd, ' ');
		if (args) {
			*args++ = 0;
			if (set_option(cmd, args) == SUCCESS) {
				request = !view->unrefreshable ? REQ_REFRESH : REQ_SCREEN_REDRAW;
				if (!strcmp(cmd, "color"))
					init_colors();
				resize_display();
				redraw_display(TRUE);
			}
		}
		return request;
	}
	return REQ_NONE;
}

enum request
open_prompt(struct view *view)
{
	char *cmd = read_prompt(":");

	return run_prompt_command(view, cmd);
}

/* vim: set ts=8 sw=8 noexpandtab: */
