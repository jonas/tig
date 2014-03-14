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
#include "tig/draw.h"
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

struct prompt_toggle {
	const char *name;
	const char *type;
	enum view_flag flags;
	void *opt;
};

static const struct enum_map *
find_enum_map(const char *type)
{
	static struct {
		const char *type;
		const struct enum_map *map;
	} mappings[] = {
#define DEFINE_ENUM_MAPPING(name, macro) { #name, name##_map },
		ENUM_INFO(DEFINE_ENUM_MAPPING)
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(mappings); i++)
		if (!strcmp(type, mappings[i].type))
			return mappings[i].map;

	die("%s not found in ENUM_INFO", type);
}

static bool
find_arg(const char *argv[], const char *arg)
{
	int i;

	for (i = 0; argv[i]; i++)
		if (!strcmp(argv[i], arg))
			return TRUE;
	return FALSE;
}


static enum view_flag
prompt_toggle_option(struct view *view, const char *argv[],
		     struct prompt_toggle *toggle, char msg[SIZEOF_STR])
{
	char name[SIZEOF_STR];

	enum_name_copy(name, toggle->name, strlen(toggle->name));

	if (!strcmp(toggle->type, "bool")) {
		bool *opt = toggle->opt;

		*opt = !*opt;
		string_format_size(msg, SIZEOF_STR, "set %s = %s", name, *opt ? "yes" : "no");

	} else if (!strncmp(toggle->type, "enum", 4)) {
		const char *type = toggle->type + STRING_SIZE("enum ");
		enum author *opt = toggle->opt;
		const struct enum_map *map = find_enum_map(type);

		*opt = (*opt + 1) % map->size;
		string_format_size(msg, SIZEOF_STR, "set %s = %s", name,
				   enum_name(map->entries[*opt]));

	} else if (!strcmp(toggle->type, "int")) {
		const char *arg = argv[2];
		int diff = arg ? atoi(arg) : 1;
		int *opt = toggle->opt;

		if (!diff)
			diff = *arg == '-' ? -1 : 1;

		if (opt == &opt_diff_context && diff < 0) {
			if (!*opt) {
				report("Diff context cannot be less than zero");
				return VIEW_NO_FLAGS;
			}
			if (*opt < -diff)
				diff = -*opt;
		}

		if (opt == &opt_title_overflow) {
			*opt = *opt ? -*opt : 50;
			if (*opt < 0) {
				string_format_size(msg, SIZEOF_STR, "set %s = no", name);
				return toggle->flags;
			}
		}

		*opt += diff;
		string_format_size(msg, SIZEOF_STR, "set %s = %d", name, *opt);

	} else if (!strcmp(toggle->type, "double")) {
		const char *arg = argv[2];
		double *opt = toggle->opt;
		int sign = 1;
		double diff;

		if (*arg == '-') {
			sign = -1;
			arg++;
		}

		if (parse_step(&diff, arg) != SUCCESS)
			diff = strtod(arg, NULL);

		*opt += sign * diff;
		string_format_size(msg, SIZEOF_STR, "set %s = %.2f", name, *opt);

	} else if (!strcmp(toggle->type, "const char **")) {
		const char ***opt = toggle->opt;
		bool found = TRUE;
		int i;

		for (i = 2; argv[i]; i++) {
			if (!find_arg(*opt, argv[i])) {
				found = FALSE;
				break;
			}
		}

		if (found) {
			int next, pos;

			for (next = 0, pos = 0; (*opt)[pos]; pos++) {
				const char *arg = (*opt)[pos];

				if (find_arg(argv + 2, arg)) {
					free((void *) arg);
					continue;
				}
				(*opt)[next++] = arg;
			}

			(*opt)[next] = NULL;

		} else if (!argv_copy(opt, argv + 2)) {
			report("Failed to append arguments");
			return VIEW_NO_FLAGS;
		}

	} else {
		die("Unsupported `:toggle %s` (%s)", name, toggle->type);
	}

	return toggle->flags;
}

#define VIEW_FLAG_RESET_DISPLAY	((enum view_flag) -1)

static enum view_flag
prompt_toggle(struct view *view, const char *argv[], char msg[SIZEOF_STR])
{
	struct prompt_toggle option_toggles[] = {
#define TOGGLE_OPTIONS(name, type, flags) { #name, #type, flags, &opt_ ## name },
		OPTION_INFO(TOGGLE_OPTIONS)
	};
	const char *name = argv[1];
	size_t namelen = strlen(name);
	int i;

	if (!name) {
		string_format_size(msg, SIZEOF_STR, "%s", "No option name given to :toggle");
		return VIEW_NO_FLAGS;
	}

	for (i = 0; i < ARRAY_SIZE(option_toggles); i++) {
		struct prompt_toggle *toggle = &option_toggles[i];

		if (namelen == strlen(toggle->name) &&
		    !string_enum_compare(toggle->name, name, namelen))
			return prompt_toggle_option(view, argv, toggle, msg);
	}

	string_format_size(msg, SIZEOF_STR, "`:toggle %s` not supported", name);
	return VIEW_NO_FLAGS;
}

enum request
run_prompt_command(struct view *view, const char *argv[])
{
	enum request request;
	const char *cmd = argv[0];

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
		char search[SIZEOF_STR];

		if (!argv_to_string(argv, search, sizeof(search), " ")) {
			report("Failed to copy search string");
			return REQ_NONE;
		}

		if (!strcmp(search + 1, view->env->search))
			return cmd[0] == '/' ? REQ_FIND_NEXT : REQ_FIND_PREV;

		string_ncopy(view->env->search, search + 1, strlen(search + 1));
		return cmd[0] == '/' ? REQ_SEARCH : REQ_SEARCH_BACK;

	} else if (cmd[0] == '!') {
		struct view *next = VIEW(REQ_VIEW_PAGER);
		bool copied;

		/* Trim the leading '!'. */
		argv[0] = cmd + 1;
		copied = argv_format(view->env, &next->argv, argv, FALSE, TRUE);
		argv[0] = cmd;

		if (!copied) {
			report("Argument formatting failed");
		} else {
			/* When running random commands, initially show the
			 * command in the title. However, it maybe later be
			 * overwritten if a commit line is selected. */
			argv_to_string(next->argv, next->ref, sizeof(next->ref), " ");

			next->dir = NULL;
			open_view(view, REQ_VIEW_PAGER, OPEN_PREPARED);
		}

	} else if (!strcmp(cmd, "toggle")) {
		char action[SIZEOF_STR] = "";
		enum view_flag flags = prompt_toggle(view, argv, action);
		int i;

		if (flags == VIEW_FLAG_RESET_DISPLAY) {
			resize_display();
			redraw_display(TRUE);
		} else {
			foreach_displayed_view(view, i) {
				if (view_has_flags(view, flags) && !view->unrefreshable)
					reload_view(view);
				else
					redraw_view(view);
			}
		}

		if (*action)
			report("%s", action);

	} else {
		request = get_request(cmd);
		if (request != REQ_UNKNOWN)
			return request;

		if (set_option(argv[0], argv_size(argv + 1), &argv[1]) == SUCCESS) {
			request = !view->unrefreshable ? REQ_REFRESH : REQ_SCREEN_REDRAW;
			if (!strcmp(cmd, "color"))
				init_colors();
			resize_display();
			redraw_display(TRUE);
		}
		return request;
	}
	return REQ_NONE;
}

enum request
open_prompt(struct view *view)
{
	char *cmd = read_prompt(":");
	const char *argv[SIZEOF_ARG] = { NULL };
	int argc = 0;

	if (cmd && !argv_from_string(argv, &argc, cmd)) {
		report("Too many arguments");
		return REQ_NONE;
	}

	return run_prompt_command(view, argv);
}

/* vim: set ts=8 sw=8 noexpandtab: */
