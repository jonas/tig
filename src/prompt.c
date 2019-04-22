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
	int last_buf_length = promptlen ? -1 : promptlen;

	input->buf[0] = 0;
	input->context[0] = 0;

	if (strlen(prompt) > 0)
		curs_set(1);

	while (status == INPUT_OK || status == INPUT_SKIP) {
		int buf_length = strlen(input->buf) + promptlen;
		int offset = pos || buf_length != last_buf_length ? pos + promptlen : -1;

		last_buf_length = buf_length;
		if (offset >= 0)
			update_status_with_context(input->context, "%s%.*s", prompt, pos, input->buf);
		else
			wmove(status_win, 0, buf_length);

		if (get_input(offset, &key) == OK) {
			int len = strlen(key.data.bytes);

			if (pos + len >= sizeof(input->buf)) {
				report("Input string too long");
				curs_set(0);
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

	curs_set(0);
	report_clear();

	if (status == INPUT_CANCEL)
		return NULL;

	input->buf[pos++] = 0;

	return input->buf;
}

enum input_status
prompt_default_handler(struct input *input, struct key *key)
{
	switch (key_to_value(key)) {
	case KEY_RETURN:
	case KEY_ENTER:
	case '\n':
		return *input->buf || input->allow_empty ? INPUT_STOP : INPUT_CANCEL;

	case KEY_BACKSPACE:
		return *input->buf ? INPUT_DELETE : INPUT_CANCEL;

	case KEY_ESC:
		return INPUT_CANCEL;

	default:
		return INPUT_SKIP;
	}
}

static enum input_status
prompt_script_handler(struct input *input, struct key *key)
{
	switch (key_to_value(key)) {
	case KEY_RETURN:
	case KEY_ENTER:
	case '\n':
		return INPUT_STOP;

	default:
		return INPUT_OK;
	}
}

static enum input_status
prompt_yesno_handler(struct input *input, struct key *key)
{
	unsigned long c = key_to_unicode(key);

	if (c == 'y' || c == 'Y')
		return INPUT_STOP;
	if (c == 'n' || c == 'N' || key_to_control(key) == 'C')
		return INPUT_CANCEL;
	return prompt_default_handler(input, key);
}

bool
prompt_yesno(const char *prompt)
{
	char prompt2[SIZEOF_STR];
	struct input input = { prompt_yesno_handler, false, NULL };

	if (!string_format(prompt2, "%s [Yy/Nn]", prompt))
		return false;

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
read_prompt_incremental(const char *prompt, bool edit_mode, bool allow_empty, input_handler handler, void *data)
{
	static struct incremental_input incremental = { { read_prompt_handler } };

	incremental.input.allow_empty = allow_empty;
	incremental.input.data = data;
	incremental.input.buf[0] = 0;
	incremental.input.context[0] = 0;
	incremental.handler = handler;
	incremental.edit_mode = edit_mode;

	return prompt_input(prompt, (struct input *) &incremental);
}

#ifdef HAVE_READLINE
static volatile bool prompt_interrupted = false;

static void
readline_display(void)
{
	update_status("%s%s", rl_display_prompt, rl_line_buffer);
	wmove(status_win, 0, strlen(rl_display_prompt) + rl_point);
	wrefresh(status_win);
}

static char *
readline_variable_generator(const char *text, int state)
{
	static const char *vars[] = {
#define FORMAT_VAR(type, name, ifempty, initval) "%(" #name ")",
		ARGV_ENV_INFO(FORMAT_VAR)
#undef FORMAT_VAR
#define FORMAT_VAR(type, name) "%(repo:" #name ")",
		REPO_INFO(FORMAT_VAR)
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
		"goto",
		"save-display",
		"save-options",
		"exec",
		"echo",
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

	/* Let ncurses deal with the LINES and COLUMNS environment variables */
	rl_change_environment = 0;
	rl_catch_sigwinch = 0;
	rl_deprep_term_function = NULL;
	rl_prep_term_function = NULL;
}

static void sigint_absorb_handler(int sig) {
	signal(SIGINT, SIG_DFL);
	prompt_interrupted = true;
	rl_done = 1;
}

char *
read_prompt(const char *prompt)
{
	static char *line = NULL;
	HIST_ENTRY *last_entry;

	if (line) {
		free(line);
		line = NULL;
	}

	curs_set(1);
	if (signal(SIGINT, sigint_absorb_handler) == SIG_ERR)
		die("Failed to setup sigint handler");
	cbreak();
	line = readline(prompt);
	raw();
	if (signal(SIGINT, SIG_DFL) == SIG_ERR)
		die("Failed to remove sigint handler");
	curs_set(0);

	/* readline can leave the virtual cursor out-of-place */
	set_cursor_pos(0, 0);

	if (prompt_interrupted) {
		free(line);
		line = NULL;
		report_clear();
	}

	prompt_interrupted = false;

	last_entry = history_get(history_length);
	if (line && *line &&
	    (!last_entry || strcmp(line, last_entry->line)))
		add_history(line);

	return line;
}

static const char *
prompt_histfile(void)
{
	static char path[SIZEOF_STR] = "";
	const char *xdg_data_home = getenv("XDG_DATA_HOME");
	const char *home = getenv("HOME");
	int fd;

	if (!xdg_data_home || !*xdg_data_home) {
		if (!string_format(path, "%s/.local/share/tig/history", home))
			die("Failed to expand $HOME");
	} else if (!string_format(path, "%s/tig/history", xdg_data_home))
		die("Failed to expand $XDG_DATA_HOME");

	fd = open(path, O_RDWR | O_CREAT | O_APPEND, 0666);
	if (fd > 0)
		close(fd);
	else if (!string_format(path, "%s/.tig_history", home))
		die("Failed to expand $HOME");

	return path;
}

static int
history_go_line(int rel_line_num)
{
	/* history_set_pos confusingly takes an absolute index. Expose a
	 * "relative offset" version consistent with the rest of readline.
	 */
	return history_set_pos(rel_line_num - history_base);
}

static void
prompt_history_dedupe(void)
{
	HIST_ENTRY *uniq_entry, *earlier_entry;
	int uniq_lineno;

	using_history();
	uniq_lineno = history_length;

	while (uniq_lineno >= history_base) {
		history_go_line(uniq_lineno);
		uniq_entry = current_history();
		if (!uniq_entry)
			break;
		while ((earlier_entry = previous_history())) {
			if (!strcmp(earlier_entry->line, uniq_entry->line)
			    && ((earlier_entry = remove_history(where_history())))) {
				free_history_entry(earlier_entry);
				uniq_lineno--;
			}
		}
		uniq_lineno--;
	}

	/* defensive hard reset */
	using_history();
	history_go_line(history_length);
}

static void
prompt_teardown(void)
{
	if (opt_history_size <= 0)
		return;

	prompt_history_dedupe();
	write_history(prompt_histfile());
}

void
prompt_init(void)
{
	HIST_ENTRY *last_entry;

	readline_init();

	if (opt_history_size <= 0)
		return;

	using_history();
	stifle_history(opt_history_size);
	read_history(prompt_histfile());
	if (atexit(prompt_teardown))
		die("Failed to register prompt_teardown");

	last_entry = history_get(history_length);
	if (last_entry)
		string_ncopy(argv_env.search, last_entry->line, strlen(last_entry->line));
}
#else
char *
read_prompt(const char *prompt)
{
	return read_prompt_incremental(prompt, true, true, NULL, NULL);
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
	char buf[128];
	struct key key;
	int size = 0;

	while (items[size].text)
		size++;

	assert(size > 0);

	curs_set(1);
	while (status == INPUT_OK) {
		const struct menu_item *item = &items[*selected];
		const char hotkey[] = { ' ', '[', (char) item->hotkey, ']', 0 };
		int i;

		if (!string_format(buf, "(%d of %d)", *selected + 1, size))
			buf[0] = 0;

		update_status_with_context(buf, "%s %s%s", prompt, item->text,
			      item->hotkey ? hotkey : "");

		switch (get_input(COLS - 1, &key)) {
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
			if (key_to_control(&key) == 'C') {
				status = INPUT_CANCEL;
				break;
			}

			for (i = 0; items[i].text; i++)
				if (items[i].hotkey == key.data.bytes[0]) {
					*selected = i;
					status = INPUT_STOP;
					break;
				}
		}
	}
	curs_set(0);

	report_clear();

	return status != INPUT_CANCEL;
}

static struct option_info option_toggles[] = {
#define DEFINE_OPTION_TOGGLES(name, type, flags) { #name, STRING_SIZE(#name), #type, &opt_ ## name, flags },
	OPTION_INFO(DEFINE_OPTION_TOGGLES)
};

static bool
find_arg(const char *argv[], const char *arg)
{
	int i;

	for (i = 0; argv && argv[i]; i++)
		if (!strcmp(argv[i], arg))
			return true;
	return false;
}

static enum status_code
prompt_toggle_option(struct view *view, const char *argv[], const char *prefix,
		     struct option_info *toggle, enum view_flag *flags)
{
	char name[SIZEOF_STR];

	if (!enum_name_prefixed(name, sizeof(name), prefix, toggle->name))
		return error("Failed to toggle option %s", toggle->name);

	*flags = toggle->flags;

	if (!strcmp(toggle->type, "bool")) {
		bool *opt = toggle->value;

		*opt = !*opt;
		if (opt == &opt_mouse)
			enable_mouse(*opt);
		return success("set %s = %s", name, *opt ? "yes" : "no");

	} else if (!strncmp(toggle->type, "enum", 4)) {
		const char *type = toggle->type + STRING_SIZE("enum ");
		enum author *opt = toggle->value;
		const struct enum_map *map = find_enum_map(type);

		*opt = (*opt + 1) % map->size;
		return success("set %s = %s", name, enum_name(map->entries[*opt].name));

	} else if (!strcmp(toggle->type, "int")) {
		const char *arg = argv[2] ? argv[2] : "1";
		int diff = atoi(arg);
		int *opt = toggle->value;

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
		double *opt = toggle->value;
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
		const char ***opt = toggle->value;
		bool found = true;
		int i;

		if (argv_size(argv) <= 2) {
			argv_free(*opt);
			(*opt)[0] = NULL;
			return SUCCESS;
		}

		for (i = 2; argv[i]; i++) {
			if (!find_arg(*opt, argv[i])) {
				found = false;
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

static enum status_code
prompt_toggle(struct view *view, const char *argv[], enum view_flag *flags)
{
	const char *option = argv[1];
	size_t optionlen = option ? strlen(option) : 0;
	struct option_info template;
	struct option_info *toggle;
	struct view_column *column;
	const char *column_name;

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

	toggle = find_option_info(option_toggles, ARRAY_SIZE(option_toggles), "", option);
	if (toggle)
		return prompt_toggle_option(view, argv, "", toggle, flags);

	for (column = view->columns; column; column = column->next) {
		toggle = find_column_option_info(column->type, &column->opt, option, &template, &column_name);
		if (toggle)
			return prompt_toggle_option(view, argv, column_name, toggle, flags);
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
		redraw_display(true);
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

		if (parse_int(&lineno, cmd, 0, view->lines) == SUCCESS) {
			if (!lineno)
				lineno = 1;
			select_view_line(view, lineno - 1);
			report_clear();
		} else {
			report("Unable to parse '%s' as a line number", cmd);
		}
	} else if (iscommit(cmd)) {
		goto_id(view, cmd, true, true);
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
		return cmd[0] == '/' ? REQ_FIND_NEXT : REQ_FIND_PREV;

	} else if (cmdlen > 1 && cmd[0] == '!') {
		struct view *next = &pager_view;
		bool copied;

		/* Trim the leading '!'. */
		argv[0] = cmd + 1;
		copied = argv_format(view->env, &next->argv, argv, false, true);
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

	} else if (!strcmp(cmd, "goto")) {
		if (!argv[1] || !strlen(argv[1]))
			report("goto requires an argument");
		else
			goto_id(view, argv[1], true, true);
		return REQ_NONE;

	} else if (!strcmp(cmd, "echo")) {
		const char **fmt_argv = NULL;
		char text[SIZEOF_STR] = "";

		if (argv[1]
		    && strlen(argv[1]) > 0
		    && (!argv_format(view->env, &fmt_argv, &argv[1], false, true)
			|| !argv_to_string(fmt_argv, text, sizeof(text), " ")
			)) {
			report("Failed to format echo string");
			return REQ_NONE;
		}

		report("%s", text);
		return REQ_NONE;

	} else if (!strcmp(cmd, "save-display")) {
		const char *path = argv[1] ? argv[1] : "tig-display.txt";

		if (!save_display(path))
			report("Failed to save screen to %s", path);
		else
			report("Saved screen to %s", path);

	} else if (!strcmp(cmd, "save-view")) {
		const char *path = argv[1] ? argv[1] : "tig-view.txt";

		if (!save_view(view, path))
			report("Failed to save view to %s", path);
		else
			report("Saved view to %s", path);

	} else if (!strcmp(cmd, "save-options")) {
		const char *path = argv[1] ? argv[1] : "tig-options.txt";
		enum status_code code = save_options(path);

		if (code != SUCCESS)
			report("Failed to save options: %s", get_status_message(code));
		else
			report("Saved options to %s", path);

	} else if (!strcmp(cmd, "exec")) {
		// argv may be allocated and mutations below will cause
		// free() to error out so backup and restore. :(
		const char *cmd = argv[1];
		struct run_request req = { view->keymap, {0}, argv + 1 };
		enum status_code code = parse_run_request_flags(&req.flags, argv + 1);

		if (code != SUCCESS) {
			argv[1] = cmd;
			report("Failed to execute command: %s", get_status_message(code));
		} else {
			request = exec_run_request(view, &req);
			argv[1] = cmd;
			return request;
		}

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
		enum status_code code = open_script(argv[1]);

		if (code != SUCCESS)
			report("%s", get_status_message(code));
		return REQ_NONE;

	} else {
		struct key key = {{0}};
		enum status_code code;
		enum view_flag flags = VIEW_NO_FLAGS;

		/* Try :<key> */
		key.modifiers.multibytes = 1;
		string_ncopy(key.data.bytes, cmd, cmdlen);
		request = get_keybinding(view->keymap, &key, 1, NULL);
		if (request != REQ_UNKNOWN)
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
			struct option_info *toggle;

			toggle = find_option_info(option_toggles, ARRAY_SIZE(option_toggles),
						  "", argv[1]);

			if (toggle)
				flags = toggle->flags;
		}

		if (flags) {
			prompt_update_display(flags);

		} else {
			if (!strcmp(cmd, "color"))
				init_colors();
			resize_display();
			redraw_display(true);
		}

	}
	return REQ_NONE;
}

enum request
exec_run_request(struct view *view, struct run_request *req)
{
	const char **argv = NULL;
	bool confirmed = false;
	enum request request = REQ_NONE;
	char cmd[SIZEOF_MED_STR];
	const char *req_argv[SIZEOF_ARG];
	int req_argc = 0;

	if (!argv_to_string(req->argv, cmd, sizeof(cmd), " ")
	    || !argv_from_string_no_quotes(req_argv, &req_argc, cmd)
	    || !argv_format(view->env, &argv, req_argv, false, true)
	    || !argv) {
		report("Failed to format arguments");
		return REQ_NONE;
	}

	if (req->flags.internal) {
		request = run_prompt_command(view, argv);

	} else {
		confirmed = !req->flags.confirm;

		if (req->flags.confirm) {
			char cmd[SIZEOF_STR], prompt[SIZEOF_STR];
			const char *and_exit = req->flags.exit ? " and exit" : "";

			if (argv_to_string_quoted(argv, cmd, sizeof(cmd), " ") &&
			    string_format(prompt, "Run `%s`%s?", cmd, and_exit) &&
			    prompt_yesno(prompt)) {
				confirmed = true;
			}
		}

		if (confirmed)
			open_external_viewer(argv, repo.cdup, req->flags.silent,
					     !req->flags.exit, req->flags.echo, req->flags.quick, false, "");
	}

	if (argv)
		argv_free(argv);
	free(argv);

	if (request == REQ_NONE) {
		if (req->flags.confirm && !confirmed)
			request = REQ_NONE;

		else if (req->flags.exit)
			request = REQ_QUIT;

		else if (!req->flags.internal && watch_dirty(&view->watch))
			request = REQ_REFRESH;

	}

	return request;
}

enum request
open_prompt(struct view *view)
{
	char *cmd;
	const char *argv[SIZEOF_ARG] = { NULL };
	int argc = 0;

	if (is_script_executing())
		cmd = read_prompt_incremental(" ", false, true, prompt_script_handler, NULL);
	else
		cmd = read_prompt(":");

	if (cmd && *cmd && !argv_from_string(argv, &argc, cmd)) {
		report("Too many arguments");
		return REQ_NONE;
	}

	if (!cmd || !*cmd) {
		report_clear();
		return REQ_NONE;
	}

	return run_prompt_command(view, argv);
}

/* vim: set ts=8 sw=8 noexpandtab: */
