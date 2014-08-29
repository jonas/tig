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

static char *
prompt_input(const char *prompt, struct input *input)
{
	enum input_status status = INPUT_OK;
	unsigned char chars_length[SIZEOF_STR];
	struct key key;
	size_t promptlen = strlen(prompt);
	int pos = 0, chars = 0;

	input->buf[pos] = 0;

	while (status == INPUT_OK || status == INPUT_SKIP) {

		update_status("%s%.*s", prompt, pos, input->buf);

		if (get_input(pos + promptlen, &key, FALSE) == OK) {
			int len = strlen(key.data.bytes);

			if (pos + len >= sizeof(input->buf)) {
				report("Input string too long");
				return NULL;
			}

			string_ncopy_do(input->buf + pos, sizeof(input->buf) - pos, key.data.bytes, len);
			pos += len;
			chars_length[chars++] = len;
			status = input->handler(input, &key);
			if (status != INPUT_OK) {
				pos -= len;
				chars--;
			} else {
				int changed_pos = strlen(input->buf);

				if (changed_pos != pos) {
					pos = changed_pos;
					chars_length[chars - 1] = changed_pos - (pos - len);
				}
			}
		} else {
			status = input->handler(input, &key);
			if (status == INPUT_DELETE) {
				int len = chars_length[--chars];

				pos -= len;
				status = INPUT_OK;
			} else {
				int changed_pos = strlen(input->buf);

				if (changed_pos != pos) {
					pos = changed_pos;
					chars_length[chars++] = changed_pos - pos;
				}
			}
		}
		input->buf[pos] = 0;
	}

	report_clear();

	if (status == INPUT_CANCEL)
		return NULL;

	input->buf[pos++] = 0;

	return input->buf;
}

static enum input_status
prompt_default_handler(struct input *input, struct key *key)
{
	if (key->modifiers.multibytes)
		return INPUT_SKIP;

	switch (key->data.value) {
	case KEY_RETURN:
	case KEY_ENTER:
	case '\n':
		return *input->buf ? INPUT_STOP : INPUT_CANCEL;

	case KEY_BACKSPACE:
		return *input->buf ? INPUT_DELETE : INPUT_CANCEL;

	case KEY_ESC:
		return INPUT_CANCEL;

	default:
		return INPUT_SKIP;
	}
}

static enum input_status
prompt_yesno_handler(struct input *input, struct key *key)
{
	unsigned long c = key_to_unicode(key);

	if (c == 'y' || c == 'Y')
		return INPUT_STOP;
	if (c == 'n' || c == 'N')
		return INPUT_CANCEL;
	return prompt_default_handler(input, key);
}

bool
prompt_yesno(const char *prompt)
{
	char prompt2[SIZEOF_STR];
	struct input input = { prompt_yesno_handler, NULL };

	if (!string_format(prompt2, "%s [Yy/Nn]", prompt))
		return FALSE;

	return !!prompt_input(prompt2, &input);
}

struct incremental_input {
	struct input input;
	input_handler handler;
	bool edit_mode;
};

static enum input_status
read_prompt_handler(struct input *input, struct key *key)
{
	struct incremental_input *incremental = (struct incremental_input *) input;

	if (incremental->edit_mode && !key->modifiers.multibytes)
		return prompt_default_handler(input, key);

	if (!unicode_width(key_to_unicode(key), 8))
		return INPUT_SKIP;

	if (!incremental->handler)
		return INPUT_OK;

	return incremental->handler(input, key);
}

char *
read_prompt_incremental(const char *prompt, bool edit_mode, input_handler handler, void *data)
{
	static struct incremental_input incremental = { { read_prompt_handler } };

	incremental.input.data = data;
	incremental.handler = handler;
	incremental.edit_mode = edit_mode;

	return prompt_input(prompt, (struct input *) &incremental);
}

#ifdef HAVE_READLINE
static void
readline_display(void)
{
	update_status("%s%s", rl_display_prompt, rl_line_buffer);
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
		"save-display",
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
		name = enum_name(name);
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
		name = enum_name(name);
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
	if (VIEW_COLUMN_##id != VIEW_COLUMN_SECTION) { \
		const char *vars[] = { \
			options(DEFINE_COLUMN_OPTIONS_WORD) \
		}; \
		char buf[SIZEOF_STR]; \
		int i; \
		for (i = 0; i < ARRAY_SIZE(vars); i++) { \
			if (enum_name_prefixed(buf, sizeof(buf), #name, vars[i])) \
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
		name = enum_name(name);
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
	return get_input_char();
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

	if (line && !*line) {
		free(line);
		line = NULL;
	}

	if (line)
		add_history(line);

	return line;
}

void
prompt_init(void)
{
	readline_init();
}
#else
char *
read_prompt(const char *prompt)
{
	return read_prompt_incremental(prompt, TRUE, NULL, NULL);
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

static struct prompt_toggle option_toggles[] = {
#define DEFINE_OPTION_TOGGLES(name, type, flags) { #name, #type, flags, &opt_ ## name },
	OPTION_INFO(DEFINE_OPTION_TOGGLES)
};

static bool
find_arg(const char *argv[], const char *arg)
{
	int i;

	for (i = 0; argv && argv[i]; i++)
		if (!strcmp(argv[i], arg))
			return TRUE;
	return FALSE;
}

static enum status_code
prompt_toggle_option(struct view *view, const char *argv[], const char *prefix,
		     struct prompt_toggle *toggle, enum view_flag *flags)
{
	char name[SIZEOF_STR];

	if (!enum_name_prefixed(name, sizeof(name), prefix, toggle->name))
		return error("Failed to toggle option %s", toggle->name);

	*flags = toggle->flags;

	if (!strcmp(toggle->type, "bool")) {
		bool *opt = toggle->opt;

		*opt = !*opt;
		if (opt == &opt_mouse)
			enable_mouse(*opt);
		return success("set %s = %s", name, *opt ? "yes" : "no");

	} else if (!strncmp(toggle->type, "enum", 4)) {
		const char *type = toggle->type + STRING_SIZE("enum ");
		enum author *opt = toggle->opt;
		const struct enum_map *map = find_enum_map(type);

		*opt = (*opt + 1) % map->size;
		return success("set %s = %s", name, enum_name(map->entries[*opt].name));

	} else if (!strcmp(toggle->type, "int")) {
		const char *arg = argv[2] ? argv[2] : "1";
		int diff = atoi(arg);
		int *opt = toggle->opt;

		if (!diff)
			diff = *arg == '-' ? -1 : 1;

		if (opt == &opt_diff_context && *opt < 0)
			*opt = -*opt;
		if (opt == &opt_diff_context && diff < 0) {
			if (!*opt)
				return error("Diff context cannot be less than zero");
			if (*opt < -diff)
				diff = -*opt;
		}

		if (strstr(name, "commit-title-overflow")) {
			*opt = *opt ? -*opt : 50;
			if (*opt < 0)
				return success("set %s = no", name);
			diff = 0;
		}

		*opt += diff;
		return success("set %s = %d", name, *opt);

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
		return success("set %s = %.2f", name, *opt);

	} else if (!strcmp(toggle->type, "const char **")) {
		const char ***opt = toggle->opt;
		bool found = TRUE;
		int i;

		if (argv_size(argv) <= 2) {
			argv_free(*opt);
			(*opt)[0] = NULL;
			return SUCCESS;
		}

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
			return ERROR_OUT_OF_MEMORY;
		}
		return SUCCESS;

	} else {
		return error("Unsupported `:toggle %s` (%s)", name, toggle->type);
	}
}

static struct prompt_toggle *
find_prompt_toggle(struct prompt_toggle toggles[], size_t toggles_size,
		   const char *prefix, const char *name, size_t namelen)
{
	char prefixed[SIZEOF_STR];
	int i;

	if (*prefix && namelen == strlen(prefix) &&
	    !string_enum_compare(prefix, name, namelen)) {
		name = "display";
		namelen = strlen(name);
	}

	for (i = 0; i < toggles_size; i++) {
		struct prompt_toggle *toggle = &toggles[i];

		if (namelen == strlen(toggle->name) &&
		    !string_enum_compare(toggle->name, name, namelen))
			return toggle;

		if (enum_name_prefixed(prefixed, sizeof(prefixed), prefix, toggle->name) &&
		    namelen == strlen(prefixed) &&
		    !string_enum_compare(prefixed, name, namelen))
			return toggle;
	}

	return NULL;
}

static enum status_code
prompt_toggle(struct view *view, const char *argv[], enum view_flag *flags)
{
	const char *option = argv[1];
	size_t optionlen = option ? strlen(option) : 0;
	struct prompt_toggle *toggle;
	struct view_column *column;

	if (!option)
		return error("%s", "No option name given to :toggle");

	if (enum_equals_static("sort-field", option, optionlen) ||
	    enum_equals_static("sort-order", option, optionlen)) {
		if (!view_has_flags(view, VIEW_SORTABLE)) {
			return error("Sorting is not yet supported for the %s view", view->name);
		} else {
			bool sort_field = enum_equals_static("sort-field", option, optionlen);
			struct sort_state *sort = &view->sort;

			sort_view(view, sort_field);
			return success("set %s = %s", option,
				sort_field ? view_column_name(get_sort_field(view))
					   : sort->reverse ? "descending" : "ascending");
		}
	}

	toggle = find_prompt_toggle(option_toggles, ARRAY_SIZE(option_toggles),
				    "", option, optionlen);
	if (toggle)
		return prompt_toggle_option(view, argv, "", toggle, flags);

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
			return prompt_toggle_option(view, argv, #name, toggle, flags); \
	}

	for (column = view->columns; column; column = column->next) {
		COLUMN_OPTIONS(DEFINE_COLUMN_OPTIONS_CHECK);
	}

	return error("`:toggle %s` not supported", option);
}

static void
prompt_update_display(enum view_flag flags)
{
	struct view *view;
	int i;

	if (flags & VIEW_RESET_DISPLAY) {
		resize_display();
		redraw_display(TRUE);
	}

	foreach_displayed_view(view, i) {
		if (view_has_flags(view, flags) && view_can_refresh(view))
			reload_view(view);
		else
			redraw_view(view);
	}
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

		if (parse_int(&lineno, cmd, 0, view->lines + 1) == SUCCESS) {
			if (!lineno)
				lineno = 1;
			select_view_line(view, lineno - 1);
			report_clear();
		} else {
			report("Unable to parse '%s' as a line number", cmd);
		}
	} else if (iscommit(cmd)) {
		int lineno;

		if (!(view->ops->column_bits & view_column_bit(ID))) {
			report("Jumping to commits is not supported by the %s view", view->name);
			return REQ_NONE;
		}

		for (lineno = 0; lineno < view->lines; lineno++) {
			struct view_column_data column_data = {};
			struct line *line = &view->line[lineno];

			if (view->ops->get_column_data(view, line, &column_data) &&
			    column_data.id &&
			    !strncasecmp(column_data.id, cmd, cmdlen)) {
				string_ncopy(view->env->search, cmd, cmdlen);
				select_view_line(view, lineno);
				report_clear();
				return REQ_NONE;
			}
		}

		report("Unable to find commit '%s'", view->env->search);
		return REQ_NONE;

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

	} else if (!strcmp(cmd, "save-display")) {
		const char *path = argv[1] ? argv[1] : "tig-display.txt";

		if (!save_display(path))
			report("Failed to save screen to %s", path);
		else
			report("Saved screen to %s", path);

	} else if (!strcmp(cmd, "toggle")) {
		enum view_flag flags = VIEW_NO_FLAGS;
		enum status_code code = prompt_toggle(view, argv, &flags);
		const char *action = get_status_message(code);

		if (code != SUCCESS) {
			report("%s", action);
			return REQ_NONE;
		}

		prompt_update_display(flags);

		if (*action)
			report("%s", action);

	} else if (!strcmp(cmd, "script")) {
		if (is_script_executing()) {
			report("Scripts cannot be run from scripts");
		} else if (!open_script(argv[1])) {
			report("Failed to open %s", argv[1]);
		}

	} else {
		struct key key = {};
		enum status_code code;
		enum view_flag flags = VIEW_NO_FLAGS;

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

		code = set_option(argv[0], argv_size(argv + 1), &argv[1]);
		if (code != SUCCESS) {
			report("%s", get_status_message(code));
			return REQ_NONE;
		}

		if (!strcmp(cmd, "set")) {
			struct prompt_toggle *toggle;

			toggle = find_prompt_toggle(option_toggles, ARRAY_SIZE(option_toggles),
						    "", argv[1], strlen(argv[1]));

			if (toggle)
				flags = toggle->flags;
		}

		if (flags) {
			prompt_update_display(flags);

		} else {
			request = view_can_refresh(view) ? REQ_REFRESH : REQ_SCREEN_REDRAW;
			if (!strcmp(cmd, "color"))
				init_colors();
			resize_display();
			redraw_display(TRUE);
		}

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
