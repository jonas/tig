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
#include "tig/pager.h"
#include "tig/types.h"

#ifdef HAVE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif /* HAVE_READLINE */

static void
prompt_toggle_name(char buf[], size_t bufsize,
		   const char *prefix, const char *name);

typedef enum input_status (*input_handler)(void *data, char *buf, struct key *key);

static char *
prompt_input(const char *prompt, input_handler handler, void *data)
{
	enum input_status status = INPUT_OK;
	static char buf[SIZEOF_STR];
	unsigned char chars_length[SIZEOF_STR];
	struct key key;
	size_t promptlen = strlen(prompt);
	int pos = 0, chars = 0;

	buf[pos] = 0;

	while (status == INPUT_OK || status == INPUT_SKIP) {

		update_status("%s%.*s", prompt, pos, buf);

		if (get_input(pos + promptlen, &key, FALSE) == OK) {
			int len = strlen(key.data.bytes);

			if (pos + len >= sizeof(buf)) {
				report("Input string too long");
				return NULL;
			}

			string_ncopy_do(buf + pos, sizeof(buf) - pos, key.data.bytes, len);
			pos += len;
			chars_length[chars++] = len;
			status = handler(data, buf, &key);
			if (status != INPUT_OK) {
				pos -= len;
				chars--;
			} else {
				int changed_pos = strlen(buf);

				if (changed_pos != pos) {
					pos = changed_pos;
					chars_length[chars - 1] = changed_pos - (pos - len);
				}
			}
		} else {
			status = handler(data, buf, &key);
			if (status == INPUT_DELETE) {
				int len = chars_length[--chars];

				pos -= len;
			}
		}
		buf[pos] = 0;
	}

	report_clear();

	if (status == INPUT_CANCEL)
		return NULL;

	buf[pos++] = 0;

	return buf;
}

static enum input_status
prompt_default_handler(void *data, char *buf, struct key *key)
{
	if (key->modifiers.multibytes)
		return INPUT_SKIP;

	switch (key->data.value) {
	case KEY_RETURN:
	case KEY_ENTER:
	case '\n':
		return *buf ? INPUT_STOP : INPUT_CANCEL;

	case KEY_BACKSPACE:
		return *buf ? INPUT_DELETE : INPUT_CANCEL;

	case KEY_ESC:
		return INPUT_CANCEL;

	default:
		return INPUT_SKIP;
	}
}

static enum input_status
prompt_yesno_handler(void *data, char *buf, struct key *key)
{
	unsigned long c = key_input_to_unicode(key);

	if (c == 'y' || c == 'Y')
		return INPUT_STOP;
	if (c == 'n' || c == 'N')
		return INPUT_CANCEL;
	return prompt_default_handler(data, buf, key);
}

bool
prompt_yesno(const char *prompt)
{
	char prompt2[SIZEOF_STR];

	if (!string_format(prompt2, "%s [Yy/Nn]", prompt))
		return FALSE;

	return !!prompt_input(prompt2, prompt_yesno_handler, NULL);
}

#ifdef HAVE_READLINE
static void
readline_display(void)
{
	wmove(status_win, 0, 0);
	waddstr(status_win, rl_display_prompt);
	waddstr(status_win, rl_line_buffer);
	wclrtoeol(status_win);
	wmove(status_win, 0, strlen(rl_display_prompt) + rl_point);
	wrefresh(status_win);
}

static char *
readline_variable_generator(const char *text, int state)
{
	static const char *vars[] = {
#define FORMAT_VAR(name, ifempty, initval) "%(" #name ")"
		ARGV_ENV_INFO(FORMAT_VAR),
#undef FORMAT_VAR
		NULL
	};

	static int index, len;
	const char *name;
	char *variable = NULL; /* No match */

	/* If it is a new word to complete, initialize */
	if (!state) {
		index = 0;
		len = strlen(text);
	}

	/* Return the next name which partially matches */
	while ((name = vars[index])) {
		index++;

		/* Complete or format a variable */
		if (strncmp(name, text, len) == 0) {
			if (strlen(name) > len)
				variable = strdup(name);
			else
				variable = argv_format_arg(&argv_env, text);
			break;
		}
	}

	return variable;
}

static char *
readline_action_generator(const char *text, int state)
{
	static const char *actions[] = {
		"!",
		"source",
		"color",
		"bind",
		"set",
		"toggle",
#define REQ_GROUP(help)
#define REQ_(req, help)	#req
		REQ_INFO,
#undef	REQ_GROUP
#undef	REQ_
		NULL
	};

	static int index, len;
	const char *name;
	char *match = NULL; /* No match */

	/* If it is a new word to complete, initialize */
	if (!state) {
		index = 0;
		len = strlen(text);
	}

	/* Return the next name which partially matches */
	while ((name = actions[index])) {
		name = enum_name_static(name, strlen(name));
		index++;

		if (strncmp(name, text, len) == 0) {
			/* Ignore exact completion */
			if (strlen(name) > len)
				match = strdup(name);
			break;
		}
	}

	return match;
}

static char *
readline_set_generator(const char *text, int state)
{
	static const char *words[] = {
#define DEFINE_OPTION_NAME(name, type, flags) #name " = ",
		OPTION_INFO(DEFINE_OPTION_NAME)
		VIEW_COLUMN_OPTION_INFO(DEFINE_OPTION_NAME)
#undef DEFINE_OPTION_NAME
		NULL
	};

	static int index, len;
	const char *name;
	char *match = NULL; /* No match */

	/* If it is a new word to complete, initialize */
	if (!state) {
		index = 0;
		len = strlen(text);
	}

	/* Return the next name which partially matches */
	while ((name = words[index])) {
		name = enum_name_static(name, strlen(name));
		index++;

		if (strncmp(name, text, len) == 0) {
			/* Ignore exact completion */
			if (strlen(name) > len)
				match = strdup(name);
			break;
		}
	}

	return match;
}

static char *
readline_toggle_generator(const char *text, int state)
{
	static const char **words;
	static int index, len;
	const char *name;
	char *match = NULL; /* No match */

	if (!words) {
		/* TODO: Only complete column options that are defined
		 * for the view. */

#define DEFINE_OPTION_WORD(name, type, flags) argv_append(&words, #name);
#define DEFINE_COLUMN_OPTIONS_WORD(name, type, flags) #name,
#define DEFINE_COLUMN_OPTIONS_WORDS(name, id, options) \
	{ \
		const char *vars[] = { \
			options(DEFINE_COLUMN_OPTIONS_WORD) \
		}; \
		char buf[SIZEOF_STR]; \
		int i; \
		for (i = 0; i < ARRAY_SIZE(vars); i++) { \
			prompt_toggle_name(buf, sizeof(buf), #name, vars[i]); \
			argv_append(&words, buf); \
		} \
	}

		OPTION_INFO(DEFINE_OPTION_WORD)
		COLUMN_OPTIONS(DEFINE_COLUMN_OPTIONS_WORDS);
	}

	/* If it is a new word to complete, initialize */
	if (!state) {
		index = 0;
		len = strlen(text);
	}

	/* Return the next name which partially matches */
	while ((name = words[index])) {
		name = enum_name_static(name, strlen(name));
		index++;

		if (strncmp(name, text, len) == 0) {
			/* Ignore exact completion */
			if (strlen(name) > len)
				match = strdup(name);
			break;
		}
	}

	return match;
}

static int
readline_getc(FILE *stream)
{
	return getc(opt_tty);
}

static char **
readline_completion(const char *text, int start, int end)
{
	/* Do not append a space after a completion */
	rl_completion_suppress_append = 1;

	/*
	 * If the word is at the start of the line,
	 * then it is a tig action to complete.
	 */
	if (start == 0)
		return rl_completion_matches(text, readline_action_generator);

	/*
	 * If the line begins with "toggle", then we complete toggle options.
	 */
	if (start >= 7 && strncmp(rl_line_buffer, "toggle ", 7) == 0)
		return rl_completion_matches(text, readline_toggle_generator);

	/*
	 * If the line begins with "set", then we complete set options.
	 * (unless it is already completed)
	 */
	if (start >= 4 && strncmp(rl_line_buffer, "set ", 4) == 0 &&
			!strchr(rl_line_buffer, '='))
		return rl_completion_matches(text, readline_set_generator);

	/*
	 * Otherwise it might be a variable name...
	 */
	if (strncmp(text, "%(", 2) == 0)
		return rl_completion_matches(text, readline_variable_generator);

	/*
	 * ... or finally fall back to filename completion.
	 */
	return NULL;
}

static void
readline_display_matches(char **matches, int num_matches, int max_length)
{
	unsigned int i;

	wmove(status_win, 0, 0);
	waddstr(status_win, "matches: ");

	/* matches[0] is the incomplete word */
	for (i = 1; i < num_matches + 1; ++i) {
		waddstr(status_win, matches[i]);
		waddch(status_win, ' ');
	}

	wgetch(status_win);
	wrefresh(status_win);
}

static void
readline_init(void)
{
	/* Allow conditional parsing of the ~/.inputrc file. */
	rl_readline_name = "tig";

	/* Word break caracters (we removed '(' to match variables) */
	rl_basic_word_break_characters = " \t\n\"\\'`@$><=;|&{";

	/* Custom display function */
	rl_redisplay_function = readline_display;
	rl_getc_function = readline_getc;

	/* Completion support */
	rl_attempted_completion_function = readline_completion;

	rl_completion_display_matches_hook = readline_display_matches;
}

char *
read_prompt(const char *prompt)
{
	static char *line = NULL;

	if (line) {
		free(line);
		line = NULL;
	}

	line = readline(prompt);
	if (line && *line)
		add_history(line);

	return line;
}

void
prompt_init(void)
{
	readline_init();
}
#else
static enum input_status
read_prompt_handler(void *data, char *buf, struct key *key)
{
	unsigned long c = key_input_to_unicode(key);

	if (!key->modifiers.multibytes)
		return prompt_default_handler(data, buf, key);
	return unicode_width(c, 8) ? INPUT_OK : INPUT_SKIP;
}

char *
read_prompt(const char *prompt)
{
	return prompt_input(prompt, read_prompt_handler, NULL);
}

void
prompt_init(void)
{
}
#endif /* HAVE_READLINE */

bool
prompt_menu(const char *prompt, const struct menu_item *items, int *selected)
{
	enum input_status status = INPUT_OK;
	struct key key;
	int size = 0;

	while (items[size].text)
		size++;

	assert(size > 0);

	while (status == INPUT_OK) {
		const struct menu_item *item = &items[*selected];
		char hotkey[] = { '[', (char) item->hotkey, ']', ' ', 0 };
		int i;

		update_status("%s (%d of %d) %s%s", prompt, *selected + 1, size,
			      item->hotkey ? hotkey : "", item->text);

		switch (get_input(COLS - 1, &key, FALSE)) {
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
				if (items[i].hotkey == key.data.bytes[0]) {
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

static bool
find_arg(const char *argv[], const char *arg)
{
	int i;

	for (i = 0; argv[i]; i++)
		if (!strcmp(argv[i], arg))
			return TRUE;
	return FALSE;
}

static void
prompt_toggle_name(char buf[], size_t bufsize,
		   const char *prefix, const char *name)
{
	if (*prefix) {
		char tmp[SIZEOF_STR];

		if (!strcasecmp("show", name)) {
			string_format(tmp, "%s-%s", name, prefix);
		} else {
			string_format(tmp, "%s-%s", prefix, name);
		}

		enum_name_ncopy(buf, bufsize, tmp, strlen(tmp));
	} else {
		enum_name_ncopy(buf, bufsize, name, strlen(name));
	}
}

static enum view_flag
prompt_toggle_option(struct view *view, const char *argv[], const char *prefix,
		     struct prompt_toggle *toggle, char msg[SIZEOF_STR])
{
	char name[SIZEOF_STR];

	prompt_toggle_name(name, sizeof(name), prefix, toggle->name);

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
		const char *arg = argv[2] ? argv[2] : "1";
		int diff = atoi(arg);
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

		if (!strcmp(name, "commit-title-overflow")) {
			*opt = *opt ? -*opt : 50;
			if (*opt < 0) {
				string_format_size(msg, SIZEOF_STR, "set %s = no", name);
				return toggle->flags;
			}
		}

		*opt += diff;
		string_format_size(msg, SIZEOF_STR, "set %s = %d", name, *opt);

	} else if (!strcmp(toggle->type, "double")) {
		const char *arg = argv[2] ? argv[2] : "1.0";
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

static struct prompt_toggle *
find_prompt_toggle(struct prompt_toggle toggles[], size_t toggles_size,
		   const char *prefix, const char *name, size_t namelen)
{
	char toggle_name[SIZEOF_STR];
	int i;

	for (i = 0; i < toggles_size; i++) {
		struct prompt_toggle *toggle = &toggles[i];

		prompt_toggle_name(toggle_name, sizeof(toggle_name), prefix, toggle->name);

		if (namelen == strlen(toggle_name) &&
		    !string_enum_compare(toggle_name, name, namelen))
			return toggle;
	}

	return NULL;
}

static enum view_flag
prompt_toggle(struct view *view, const char *argv[], char msg[SIZEOF_STR])
{
	struct prompt_toggle option_toggles[] = {
#define DEFINE_OPTION_TOGGLES(name, type, flags) { #name, #type, flags, &opt_ ## name },
		OPTION_INFO(DEFINE_OPTION_TOGGLES)
	};
	const char *option = argv[1];
	size_t optionlen = option ? strlen(option) : 0;
	struct prompt_toggle *toggle;
	struct view_column *column;

	if (!option) {
		string_format_size(msg, SIZEOF_STR, "%s", "No option name given to :toggle");
		return VIEW_NO_FLAGS;
	}

	if (enum_equals_static("sort-field", option, optionlen) ||
	    enum_equals_static("sort-order", option, optionlen)) {
		if (!view_has_flags(view, VIEW_SORTABLE)) {
			report("Sorting is not yet supported for the %s view", view->name);
		} else {
			bool sort_field = enum_equals_static("sort-field", option, optionlen);
			struct sort_state *sort = &view->sort;

			sort_view(view, sort_field);
			string_format_size(msg, SIZEOF_STR, "set %s = %s", option,
				sort_field ? enum_name(view_column_type_map->entries[get_sort_field(view)])
					   : sort->reverse ? "descending" : "ascending");
		}
		return VIEW_NO_FLAGS;
	}

	toggle = find_prompt_toggle(option_toggles, ARRAY_SIZE(option_toggles),
				    "", option, optionlen);
	if (toggle)
		return prompt_toggle_option(view, argv, "", toggle, msg);

#define DEFINE_COLUMN_OPTIONS_TOGGLE(name, type, flags) \
	{ #name, #type, flags, &opt->name },

#define DEFINE_COLUMN_OPTIONS_CHECK(name, id, options) \
	if (column->type == VIEW_COLUMN_##id) { \
		struct name##_options *opt = &column->opt.name; \
		struct prompt_toggle toggles[] = { \
			options(DEFINE_COLUMN_OPTIONS_TOGGLE) \
		}; \
		toggle = find_prompt_toggle(toggles, ARRAY_SIZE(toggles), #name, option, optionlen); \
		if (toggle) \
			return prompt_toggle_option(view, argv, #name, toggle, msg); \
	}

	for (column = view->columns; column; column = column->next) {
		COLUMN_OPTIONS(DEFINE_COLUMN_OPTIONS_CHECK);
	}

	string_format_size(msg, SIZEOF_STR, "`:toggle %s` not supported", option);
	return VIEW_NO_FLAGS;
}

enum request
run_prompt_command(struct view *view, const char *argv[])
{
	enum request request;
	const char *cmd = argv[0];
	size_t cmdlen = cmd ? strlen(cmd) : 0;

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
		string_ncopy(view->env->search, cmd, cmdlen);
		return REQ_JUMP_COMMIT;

	} else if (cmdlen > 1 && (cmd[0] == '/' || cmd[0] == '?')) {
		char search[SIZEOF_STR];

		if (!argv_to_string(argv, search, sizeof(search), " ")) {
			report("Failed to copy search string");
			return REQ_NONE;
		}

		if (!strcmp(search + 1, view->env->search))
			return cmd[0] == '/' ? REQ_FIND_NEXT : REQ_FIND_PREV;

		string_ncopy(view->env->search, search + 1, strlen(search + 1));
		return cmd[0] == '/' ? REQ_SEARCH : REQ_SEARCH_BACK;

	} else if (cmdlen > 1 && cmd[0] == '!') {
		struct view *next = &pager_view;
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
			open_pager_view(view, OPEN_PREPARED | OPEN_WITH_STDERR);
		}

	} else if (!strcmp(cmd, "toggle")) {
		char action[SIZEOF_STR] = "";
		enum view_flag flags = prompt_toggle(view, argv, action);
		int i;

		if (flags & VIEW_RESET_DISPLAY) {
			resize_display();
			redraw_display(TRUE);
		}

		foreach_displayed_view(view, i) {
			if (view_has_flags(view, flags) && !view->unrefreshable)
				reload_view(view);
			else
				redraw_view(view);
		}

		if (*action)
			report("%s", action);

	} else {
		struct key key = {};

		/* Try :<key> */
		key.modifiers.multibytes = 1;
		string_ncopy(key.data.bytes, cmd, cmdlen);
		request = get_keybinding(view->keymap, &key, 1);
		if (request != REQ_NONE)
			return request;

		/* Try :<command> */
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
