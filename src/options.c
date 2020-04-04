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
#include "tig/types.h"
#include "tig/argv.h"
#include "tig/io.h"
#include "tig/repo.h"
#include "tig/refdb.h"
#include "tig/options.h"
#include "tig/request.h"
#include "tig/line.h"
#include "tig/keys.h"
#include "tig/view.h"

/*
 * Option variables.
 */

#define DEFINE_OPTION_VARIABLES(name, type, flags) type opt_##name;
OPTION_INFO(DEFINE_OPTION_VARIABLES)

static struct option_info option_info[] = {
#define DEFINE_OPTION_INFO(name, type, flags) { #name, STRING_SIZE(#name), #type, &opt_##name },
	OPTION_INFO(DEFINE_OPTION_INFO)
};

struct option_info *
find_option_info(struct option_info *option, size_t options, const char *prefix, const char *name)
{
	size_t namelen = strlen(name);
	char prefixed[SIZEOF_STR];
	int i;

	if (*prefix && namelen == strlen(prefix) &&
	    !string_enum_compare(prefix, name, namelen)) {
		name = "display";
		namelen = strlen(name);
	}

	for (i = 0; i < options; i++) {
		if (!strcmp(option[i].type, "view_settings") &&
		    enum_equals_prefix(option[i], name, namelen))
			return &option[i];

		if (enum_equals(option[i], name, namelen))
			return &option[i];

		if (enum_name_prefixed(prefixed, sizeof(prefixed), prefix, option[i].name) &&
		    namelen == strlen(prefixed) &&
		    !string_enum_compare(prefixed, name, namelen))
			return &option[i];
	}

	return NULL;
}

static struct option_info *
find_option_info_by_value(void *value)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(option_info); i++)
		if (option_info[i].value == value)
			return &option_info[i];

	return NULL;
}

static void
mark_option_seen(void *value)
{
	struct option_info *option = find_option_info_by_value(value);

	if (option)
		option->seen = true;
}

struct option_info *
find_column_option_info(enum view_column_type type, union view_column_options *opts,
			const char *option, struct option_info *column_info,
			const char **column_name)
{
#define DEFINE_COLUMN_OPTION_INFO(name, type, flags) \
	{ #name, STRING_SIZE(#name), #type, &opt->name, flags },

#define DEFINE_COLUMN_OPTION_INFO_CHECK(name, id, options) \
	if (type == VIEW_COLUMN_##id) { \
		struct name##_options *opt = &opts->name; \
		struct option_info info[] = { \
			options(DEFINE_COLUMN_OPTION_INFO) \
		}; \
		struct option_info *match; \
		match = find_option_info(info, ARRAY_SIZE(info), #name, option); \
		if (match) { \
			*column_info = *match; \
			*column_name = #name; \
			return column_info; \
		} \
	}

	COLUMN_OPTIONS(DEFINE_COLUMN_OPTION_INFO_CHECK);

	*column_name = NULL;
	return NULL;
}

/*
 * State variables.
 */

iconv_t opt_iconv_out		= ICONV_NONE;
char opt_editor[SIZEOF_STR]	= "";
const char **opt_cmdline_args	= NULL;
bool opt_log_follow		= false;
bool opt_word_diff		= false;

/*
 * Mapping between options and command argument mapping.
 */

const char *
diff_context_arg()
{
	static char opt_diff_context_arg[9]	= "";

	if (opt_diff_context < 0 ||
	    !string_format(opt_diff_context_arg, "-U%u", opt_diff_context))
		return "";

	return opt_diff_context_arg;
}

const char *
use_mailmap_arg()
{
	return opt_mailmap ? "--use-mailmap" : "";
}

const char *
log_custom_pretty_arg(void)
{
	return opt_mailmap
		? "--pretty=format:commit %m %H %P%x00%aN <%aE> %ad%x00%s%x00%N"
		: "--pretty=format:commit %m %H %P%x00%an <%ae> %ad%x00%s%x00%N";
}

#define ENUM_ARG(enum_name, arg_string) ENUM_MAP_ENTRY(arg_string, enum_name)

static const struct enum_map_entry ignore_space_arg_map[] = {
	ENUM_ARG(IGNORE_SPACE_NO,	""),
	ENUM_ARG(IGNORE_SPACE_ALL,	"--ignore-all-space"),
	ENUM_ARG(IGNORE_SPACE_SOME,	"--ignore-space-change"),
	ENUM_ARG(IGNORE_SPACE_AT_EOL,	"--ignore-space-at-eol"),
};

const char *
ignore_space_arg()
{
	return ignore_space_arg_map[opt_ignore_space].name;
}

static const struct enum_map_entry commit_order_arg_map[] = {
	ENUM_ARG(COMMIT_ORDER_AUTO,		""),
	ENUM_ARG(COMMIT_ORDER_DEFAULT,		""),
	ENUM_ARG(COMMIT_ORDER_TOPO,		"--topo-order"),
	ENUM_ARG(COMMIT_ORDER_DATE,		"--date-order"),
	ENUM_ARG(COMMIT_ORDER_AUTHOR_DATE,	"--author-date-order"),
	ENUM_ARG(COMMIT_ORDER_REVERSE,		"--reverse"),
};

const char *
commit_order_arg()
{
	return commit_order_arg_map[opt_commit_order].name;
}

const char *
commit_order_arg_with_graph(enum graph_display graph_display)
{
	enum commit_order commit_order = opt_commit_order;

	if (commit_order == COMMIT_ORDER_AUTO &&
	    graph_display != GRAPH_DISPLAY_NO)
		commit_order = COMMIT_ORDER_TOPO;

	return commit_order_arg_map[commit_order].name;
}

/* Use --show-notes to support Git >= 1.7.6 */
#define NOTES_ARG	"--show-notes"
#define NOTES_EQ_ARG	NOTES_ARG "="

static char opt_notes_arg[SIZEOF_STR] = NOTES_ARG;

const char *
show_notes_arg()
{
	if (opt_show_notes)
		return opt_notes_arg;
	/* Notes are disabled by default when passing --pretty args. */
	return "";
}

void
update_options_from_argv(const char *argv[])
{
	int next, flags_pos;

	for (next = flags_pos = 0; argv[next]; next++) {
		const char *flag = argv[next];
		int value = -1;

		if (map_enum(&value, commit_order_arg_map, flag)) {
			opt_commit_order = value;
			mark_option_seen(&opt_commit_order);
			continue;
		}

		if (map_enum(&value, ignore_space_arg_map, flag)) {
			opt_ignore_space = value;
			mark_option_seen(&opt_ignore_space);
			continue;
		}

		if (!strcmp(flag, "--no-notes")) {
			opt_show_notes = false;
			mark_option_seen(&opt_show_notes);
			continue;
		}

		if (!prefixcmp(flag, "--show-notes") ||
		    !prefixcmp(flag, "--notes")) {
			opt_show_notes = true;
			string_ncopy(opt_notes_arg, flag, strlen(flag));
			mark_option_seen(&opt_show_notes);
			continue;
		}

		if (!prefixcmp(flag, "-U")
		    && parse_int(&value, flag + 2, 0, 999999) == SUCCESS) {
			opt_diff_context = value;
			mark_option_seen(&opt_diff_context);
			continue;
		}

		if (!strcmp(flag, "--word-diff") ||
		    !strcmp(flag, "--word-diff=plain")) {
			opt_word_diff = true;
		}

		argv[flags_pos++] = flag;
	}

	argv[flags_pos] = NULL;
}

/*
 * User config file handling.
 */

static const struct enum_map_entry color_map[] = {
#define COLOR_MAP(name) ENUM_MAP_ENTRY(#name, COLOR_##name)
	COLOR_MAP(DEFAULT),
	COLOR_MAP(BLACK),
	COLOR_MAP(BLUE),
	COLOR_MAP(CYAN),
	COLOR_MAP(GREEN),
	COLOR_MAP(MAGENTA),
	COLOR_MAP(RED),
	COLOR_MAP(WHITE),
	COLOR_MAP(YELLOW),
};

static const struct enum_map_entry attr_map[] = {
#define ATTR_MAP(name) ENUM_MAP_ENTRY(#name, A_##name)
	ATTR_MAP(NORMAL),
	ATTR_MAP(BLINK),
	ATTR_MAP(BOLD),
	ATTR_MAP(DIM),
	ATTR_MAP(REVERSE),
	ATTR_MAP(STANDOUT),
	ATTR_MAP(UNDERLINE),
};

#define set_attribute(attr, name)	map_enum(attr, attr_map, name)

enum status_code
parse_step(double *opt, const char *arg)
{
	int value = atoi(arg);

	if (!value && !isdigit(*arg))
		return error("Invalid double or percentage");

	*opt = value;
	if (!strchr(arg, '%'))
		return SUCCESS;

	/* "Shift down" so 100% and 1 does not conflict. */
	*opt /= 100;
	if (*opt >= 1.0) {
		*opt = 0.99;
		return error("Percentage is larger than 100%%");
	}
	if (*opt < 0.0) {
		*opt = 1;
		return error("Percentage is less than 0%%");
	}
	return SUCCESS;
}

enum status_code
parse_int(int *opt, const char *arg, int min, int max)
{
	int value = atoi(arg);

	if (min <= value && value <= max) {
		*opt = value;
		return SUCCESS;
	}

	return error("Value must be between %d and %d", min, max);
}

static bool
set_color(int *color, const char *name)
{
	if (map_enum(color, color_map, name))
		return true;
	/* Git expects a plain int w/o prefix, however, color<int> is
	 * the preferred Tig color notation.  */
	if (!prefixcmp(name, "color"))
		name += 5;
	return string_isnumber(name) &&
	       parse_int(color, name, 0, 255) == SUCCESS;
}

#define is_quoted(c)	((c) == '"' || (c) == '\'')

static enum status_code
parse_color_name(const char *color, struct line_rule *rule, const char **prefix_ptr)
{
	const char *prefixend = is_quoted(*color) ? NULL : strchr(color, '.');

	if (prefixend) {
		struct keymap *keymap = get_keymap(color, prefixend - color);

		if (!keymap)
			return error("Unknown key map: %.*s", (int) (prefixend - color), color);
		if (prefix_ptr)
			*prefix_ptr = keymap->name;
		color = prefixend + 1;
	}

	memset(rule, 0, sizeof(*rule));
	if (is_quoted(*color)) {
		rule->line = color + 1;
		rule->linelen = strlen(color) - 2;
	} else {
		rule->name = color;
		rule->namelen = strlen(color);
	}

	return SUCCESS;
}

static int
find_remapped(const char *remapped[][2], size_t remapped_size, const char *arg)
{
	size_t arglen = strlen(arg);
	int i;

	for (i = 0; i < remapped_size; i++) {
		const char *name = remapped[i][0];
		size_t namelen = strlen(name);

		if (arglen == namelen &&
		    !string_enum_compare(arg, name, namelen))
			return i;
	}

	return -1;
}

/* Wants: object fgcolor bgcolor [attribute] */
static enum status_code
option_color_command(int argc, const char *argv[])
{
	struct line_rule rule = {0};
	const char *prefix = NULL;
	struct line_info *info;
	enum status_code code;

	if (argc < 3)
		return error("Invalid color mapping: color area fgcolor bgcolor [attrs]");

	code = parse_color_name(argv[0], &rule, &prefix);
	if (code != SUCCESS)
		return code;

	info = add_line_rule(prefix, &rule);
	if (!info) {
		static const char *obsolete[][2] = {
			{ "acked",			"'    Acked-by'" },
			{ "diff-copy-from",		"'copy from '" },
			{ "diff-copy-to",		"'copy to '" },
			{ "diff-deleted-file-mode",	"'deleted file mode '" },
			{ "diff-dissimilarity",		"'dissimilarity '" },
			{ "diff-rename-from",		"'rename from '" },
			{ "diff-rename-to",		"'rename to '" },
			{ "diff-tree",			"'diff-tree '" },
			{ "filename",			"file" },
			{ "help-keymap",		"help.section" },
			{ "main-revgraph",		"" },
			{ "pp-adate",			"'AuthorDate: '" },
			{ "pp-author",			"'Author: '" },
			{ "pp-cdate",			"'CommitDate: '" },
			{ "pp-commit",			"'Commit: '" },
			{ "pp-date",			"'Date: '" },
			{ "reviewed",			"'    Reviewed-by'" },
			{ "signoff",			"'    Signed-off-by'" },
			{ "stat-head",			"status.header" },
			{ "stat-section",		"status.section" },
			{ "tested",			"'    Tested-by'" },
			{ "tree-dir",			"tree.directory" },
			{ "tree-file",			"tree.file" },
			{ "tree-head",			"tree.header" },
		};
		int index;

		index = find_remapped(obsolete, ARRAY_SIZE(obsolete), rule.name);
		if (index != -1) {
			if (!*obsolete[index][1])
				return error("%s is obsolete", argv[0]);
			/* Keep the initial prefix if defined. */
			code = parse_color_name(obsolete[index][1], &rule, prefix ? NULL : &prefix);
			if (code != SUCCESS)
				return code;
			info = add_line_rule(prefix, &rule);
		}

		if (!info)
			return error("Unknown color name: %s", argv[0]);

		code = error("%s has been replaced by %s",
			     obsolete[index][0], obsolete[index][1]);
	}

	if (!set_color(&info->fg, argv[1]))
		return error("Unknown color: %s", argv[1]);

	if (!set_color(&info->bg, argv[2]))
		return error("Unknown color: %s", argv[2]);

	info->attr = 0;
	while (argc-- > 3) {
		int attr;

		if (!set_attribute(&attr, argv[argc]))
			return error("Unknown color attribute: %s", argv[argc]);
		info->attr |= attr;
	}

	return code;
}

static enum status_code
parse_bool(bool *opt, const char *arg)
{
	*opt = (!strcmp(arg, "1") || !strcmp(arg, "true") || !strcmp(arg, "yes"))
		? true : false;
	if (*opt || !strcmp(arg, "0") || !strcmp(arg, "false") || !strcmp(arg, "no"))
		return SUCCESS;
	return error("Non-boolean value treated as false: %s", arg);
}

static enum status_code
parse_enum(const char *name, unsigned int *opt, const char *arg,
	   const struct enum_map *map)
{
	bool is_true;
	enum status_code code;

	assert(map->size > 1);

	if (map_enum_do(map->entries, map->size, (int *) opt, arg))
		return SUCCESS;

	code = parse_bool(&is_true, arg);
	*opt = is_true ? map->entries[1].value : map->entries[0].value;
	if (code == SUCCESS)
		return code;

	if (!strcmp(name, "date-display")) {
		const char *msg = "";

		if (!strcasecmp(arg, "local"))
			msg = ", use the 'date-local' column option";
		else if (!strcasecmp(arg, "short"))
			msg = ", use the 'custom' display mode and set 'date-format'";

		*opt = map->entries[1].value;
		return error("'%s' is no longer supported for %s%s", arg, name, msg);
	}

	return error("'%s' is not a valid value for %s; using %s",
		     arg, name, enum_name(map->entries[*opt].name));
}

static enum status_code
parse_string(char *opt, const char *arg, size_t optsize)
{
	int arglen = strlen(arg);

	switch (arg[0]) {
	case '\"':
	case '\'':
		if (arglen == 1 || arg[arglen - 1] != arg[0])
			return ERROR_UNMATCHED_QUOTATION;
		arg += 1; arglen -= 2;
		/* Fall-through */
	default:
		string_ncopy_do(opt, optsize, arg, arglen);
		return SUCCESS;
	}
}

static enum status_code
parse_encoding(struct encoding **encoding_ref, const char *arg, bool priority)
{
	char buf[SIZEOF_STR];
	enum status_code code = parse_string(buf, arg, sizeof(buf));

	if (code == SUCCESS) {
		struct encoding *encoding = *encoding_ref;

		if (encoding && !priority)
			return code;
		encoding = encoding_open(buf);
		if (encoding)
			*encoding_ref = encoding;
	}

	return code;
}

static enum status_code
parse_args(const char ***args, const char *argv[])
{
	if (!argv_copy(args, argv))
		return ERROR_OUT_OF_MEMORY;
	return SUCCESS;
}

enum status_code
parse_option(struct option_info *option, const char *prefix, const char *arg)
{
	char name[SIZEOF_STR];

	if (!enum_name_prefixed(name, sizeof(name), prefix, option->name))
		return error("Failed to parse option");

	if (!strcmp("show-notes", name)) {
		bool *value = option->value;
		enum status_code res;

		if (parse_bool(option->value, arg) == SUCCESS)
			return SUCCESS;

		*value = true;
		string_copy(opt_notes_arg, NOTES_EQ_ARG);
		res = parse_string(opt_notes_arg + STRING_SIZE(NOTES_EQ_ARG), arg,
				   sizeof(opt_notes_arg) - STRING_SIZE(NOTES_EQ_ARG));
		if (res == SUCCESS && !opt_notes_arg[STRING_SIZE(NOTES_EQ_ARG)])
			opt_notes_arg[STRING_SIZE(NOTES_ARG)] = 0;
		return res;
	}

	if (!strcmp(option->type, "bool"))
		return parse_bool(option->value, arg);

	if (!strcmp(option->type, "double"))
		return parse_step(option->value, arg);

	if (!strncmp(option->type, "enum", 4)) {
		const char *type = option->type + STRING_SIZE("enum ");
		const struct enum_map *map = find_enum_map(type);

		return parse_enum(name, option->value, arg, map);
	}

	if (!strcmp(option->type, "int")) {
		if (strstr(name, "title-overflow")) {
			bool enabled = false;
			int *value = option->value;

			/* We try to parse it as a boolean (and set the
			 * value to 0 if fale), otherwise we parse it as
			 * an integer and use the given value. */
			if (parse_bool(&enabled, arg) == SUCCESS) {
				if (!enabled) {
					*value = 0;
					return SUCCESS;
				}
				arg = "50";
			}
		}

		if (!strcmp(name, "line-number-interval") ||
		    !strcmp(name, "tab-size"))
			return parse_int(option->value, arg, 1, 1024);
		else if (!strcmp(name, "id-width"))
			return parse_int(option->value, arg, 0, SIZEOF_REV - 1);
		else
			return parse_int(option->value, arg, 0, 1024);
	}

	if (!strcmp(option->type, "const char *")) {
		const char **value = option->value;
		char *alloc = NULL;

		if (option->value == &opt_diff_highlight) {
			bool enabled = false;

			if (parse_bool(&enabled, arg) == SUCCESS) {
				if (!enabled) {
					*value = NULL;
					return SUCCESS;
				}
				arg = "diff-highlight";
			}
		}

		if (strlen(arg)) {
			if (arg[0] == '"' && arg[strlen(arg) - 1] == '"')
				alloc = strndup(arg + 1, strlen(arg + 1) - 1);
			else
				alloc = strdup(arg);
			if (!alloc)
				return ERROR_OUT_OF_MEMORY;
		}

		if (alloc && !strcmp(name, "truncation-delimiter")) {
			if (!strcmp(alloc, "utf-8") || !strcmp(alloc, "utf8")) {
				free(alloc);
				alloc = strdup("⋯");
				if (!alloc)
					return ERROR_OUT_OF_MEMORY;
			} else if (utf8_width_of(alloc, -1, -1) != 1) {
				free(alloc);
				alloc = strdup("~");
				if (!alloc)
					return ERROR_OUT_OF_MEMORY;
			}
		}

		free((void *) *value);
		*value = alloc;
		return SUCCESS;
	}

	return error("Unhandled option: %s", name);
}

static enum status_code
parse_view_settings(struct view_column **view_column, const char *name_, const char *argv[])
{
	char buf[SIZEOF_STR];
	const char *name = enum_name_copy(buf, sizeof(buf), name_) ? buf : name_;
	const char *prefixed;

	if ((prefixed = strstr(name, "-view-"))) {
		const char *column_name = prefixed + STRING_SIZE("-view-");
		size_t column_namelen = strlen(column_name);
		enum view_column_type type;

		for (type = 0; type < view_column_type_map->size; type++) {
			const struct enum_map_entry *column = &view_column_type_map->entries[type];

			if (enum_equals(*column, column_name, column_namelen))
				return parse_view_column_config(name, type, NULL, argv);

			if (enum_equals_prefix(*column, column_name, column_namelen))
				return parse_view_column_config(name, type,
								column_name + column->namelen + 1,
								argv);
		}
	}

	return parse_view_config(view_column, name, argv);
}

static enum status_code
option_update(struct option_info *option, int argc, const char *argv[])
{
	enum status_code code;

	if (option->seen)
		return SUCCESS;

	if (!strcmp(option->type, "const char **"))
		return parse_args(option->value, argv + 2);

	if (argc < 3)
		return error("Invalid set command: set option = value");

	if (!strcmp(option->type, "view_settings"))
		return parse_view_settings(option->value, argv[0], argv + 2);

	if (!strcmp(option->type, "struct ref_format **"))
		return parse_ref_formats(option->value, argv + 2);

	code = parse_option(option, "", argv[2]);
	if (code == SUCCESS && argc != 3)
		return error("Option %s only takes one value", argv[0]);

	return code;
}

/* Wants: name = value */
static enum status_code
option_set_command(int argc, const char *argv[])
{
	struct option_info *option;

	if (argc < 2)
		return error("Invalid set command: set option = value");

	if (strcmp(argv[1], "="))
		return error("No value assigned to %s", argv[0]);

	option = find_option_info(option_info, ARRAY_SIZE(option_info), "", argv[0]);
	if (option)
		return option_update(option, argc, argv);

	{
		const char *obsolete[][2] = {
			{ "status-untracked-dirs", "status-show-untracked-dirs" },
		};
		int index = find_remapped(obsolete, ARRAY_SIZE(obsolete), argv[0]);

		if (index != -1) {
			option = find_option_info(option_info, ARRAY_SIZE(option_info), "", obsolete[index][1]);
			if (option) {
				enum status_code code = option_update(option, argc, argv);

				if (code != SUCCESS)
					return code;
				return error("%s has been renamed to %s",
					     obsolete[index][0], obsolete[index][1]);
			}
		}
	}

	{
		const char *obsolete[][2] = {
			{ "author-width",		"author" },
			{ "filename-width",		"file-name" },
			{ "line-number-interval",	"line-number" },
			{ "show-author",		"author" },
			{ "show-date",			"date" },
			{ "show-file-size",		"file-size" },
			{ "show-filename",		"file-name" },
			{ "show-id",			"id" },
			{ "show-line-numbers",		"line-number" },
			{ "show-refs",			"commit-title" },
			{ "show-rev-graph",		"commit-title" },
			{ "title-overflow",		"commit-title and text" },
		};
		int index = find_remapped(obsolete, ARRAY_SIZE(obsolete), argv[0]);

		if (index != -1)
			return error("%s is obsolete; see tigrc(5) for how to set the %s column option",
				     obsolete[index][0], obsolete[index][1]);

		if (!strcmp(argv[0], "read-git-colors"))
			return error("read-git-colors has been obsoleted by the git-colors option");

		if (!strcmp(argv[0], "cmdline-args"))
			return error("cmdline-args is obsolete; use view-specific options instead, e.g. main-options");
	}

	return error("Unknown option name: %s", argv[0]);
}

/* Wants: mode request key */
static enum status_code
option_bind_command(int argc, const char *argv[])
{
	struct key key[16];
	size_t keys = 0;
	enum request request;
	struct keymap *keymap;
	const char *key_arg;

	if (argc < 3)
		return error("Invalid key binding: bind keymap key action");

	if (!(keymap = get_keymap(argv[0], strlen(argv[0])))) {
		if (!strcmp(argv[0], "branch"))
			keymap = get_keymap("refs", strlen("refs"));
		if (!keymap)
			return error("Unknown key map: %s", argv[0]);
	}

	for (keys = 0, key_arg = argv[1]; *key_arg && keys < ARRAY_SIZE(key); keys++) {
		enum status_code code = get_key_value(&key_arg, &key[keys]);

		if (code != SUCCESS)
			return code;
	}

	if (*key_arg && keys == ARRAY_SIZE(key))
		return error("Except for <Esc> combos only one key is allowed "
			     "in key combos: %s", argv[1]);

	request = get_request(argv[2]);
	if (request == REQ_UNKNOWN) {
		static const char *obsolete[][2] = {
			{ "view-branch",		"view-refs" },
		};
		static const char *toggles[][2] = {
			{ "diff-context-down",		"diff-context" },
			{ "diff-context-up",		"diff-context" },
			{ "stage-next",			":/^@@" },
			{ "status-untracked-dirs",	"status-show-untracked-dirs" },
			{ "toggle-author",		"author" },
			{ "toggle-changes",		"show-changes" },
			{ "toggle-commit-order",	"show-commit-order" },
			{ "toggle-date",		"date" },
			{ "toggle-files",		"file-filter" },
			{ "toggle-file-filter",		"file-filter" },
			{ "toggle-file-size",		"file-size" },
			{ "toggle-filename",		"filename" },
			{ "toggle-graphic",		"show-graphic" },
			{ "toggle-id",			"id" },
			{ "toggle-ignore-space",	"show-ignore-space" },
			{ "toggle-lineno",		"line-number" },
			{ "toggle-refs",		"commit-title-refs" },
			{ "toggle-rev-graph",		"commit-title-graph" },
			{ "toggle-show-changes",	"show-changes" },
			{ "toggle-sort-field",		"sort-field" },
			{ "toggle-sort-order",		"sort-order" },
			{ "toggle-title-overflow",	"commit-title-overflow" },
			{ "toggle-untracked-dirs",	"status-show-untracked-dirs" },
			{ "toggle-vertical-split",	"show-vertical-split" },
		};
		int alias;

		alias = find_remapped(obsolete, ARRAY_SIZE(obsolete), argv[2]);
		if (alias != -1) {
			const char *action = obsolete[alias][1];

			add_keybinding(keymap, get_request(action), key, keys);
			return error("%s has been renamed to %s",
				     obsolete[alias][0], action);
		}

		alias = find_remapped(toggles, ARRAY_SIZE(toggles), argv[2]);
		if (alias != -1) {
			const char *action = toggles[alias][0];
			const char *arg = prefixcmp(action, "diff-context-")
					? NULL : (strstr(action, "-down") ? "-1" : "+1");
			const char *mapped = toggles[alias][1];
			const char *toggle[] = { ":toggle", mapped, arg, NULL};
			const char *other[] = { mapped, NULL };
			const char **prompt = *mapped == ':' ? other : toggle;
			enum status_code code = add_run_request(keymap, key, keys, prompt);

			if (code == SUCCESS)
				code = error("%s has been replaced by `%s%s%s%s'",
					     action, prompt == other ? mapped : ":toggle ",
					     prompt == other ? "" : mapped,
					     arg ? " " : "", arg ? arg : "");
			return code;
		}
	}

	if (request == REQ_UNKNOWN)
		return add_run_request(keymap, key, keys, argv + 2);

	return add_keybinding(keymap, request, key, keys);
}


static enum status_code load_option_file(const char *path);

static enum status_code
option_source_command(int argc, const char *argv[])
{
	enum status_code code;
	bool quiet = false;

	if ((argc < 1) || (argc > 2))
		return error("Invalid source command: source [-q] <path>");

	if (argc == 2) {
		if (!strcmp(argv[0], "-q"))
			quiet = true;
		else
			return error("Invalid source option: %s", argv[0]);
	}

	code = load_option_file(argv[argc - 1]);

	if (quiet)
		return code == ERROR_FILE_DOES_NOT_EXIST ? 0 : code;

	return code == ERROR_FILE_DOES_NOT_EXIST
		? error("File does not exist: %s", argv[argc - 1]) : code;
}

enum status_code
set_option(const char *opt, int argc, const char *argv[])
{
	if (!strcmp(opt, "color"))
		return option_color_command(argc, argv);

	if (!strcmp(opt, "set"))
		return option_set_command(argc, argv);

	if (!strcmp(opt, "bind"))
		return option_bind_command(argc, argv);

	if (!strcmp(opt, "source"))
		return option_source_command(argc, argv);

	return error("Unknown option command: %s", opt);
}

struct config_state {
	const char *path;
	size_t lineno;
	bool errors;
};

static enum status_code
read_option(char *opt, size_t optlen, char *value, size_t valuelen, void *data)
{
	struct config_state *config = data;
	enum status_code status = ERROR_NO_OPTION_VALUE;

	/* Check for comment markers, since read_properties() will
	 * only ensure opt and value are split at first " \t". */
	optlen = strcspn(opt, "#");
	if (optlen == 0)
		return SUCCESS;

	if (opt[optlen] == 0) {
		/* Look for comment endings in the value. */
		size_t len = strcspn(value, "#");
		const char *argv[SIZEOF_ARG];
		int argc = 0;

		if (len < valuelen) {
			valuelen = len;
			value[valuelen] = 0;
		}

		if (!argv_from_string(argv, &argc, value))
			status = error("Too many option arguments for %s", opt);
		else
			status = set_option(opt, argc, argv);
	}

	if (status != SUCCESS) {
		warn("%s:%zu: %s", config->path, config->lineno,
		     get_status_message(status));
		config->errors = true;
	}

	/* Always keep going if errors are encountered. */
	return SUCCESS;
}

static enum status_code
load_option_file(const char *path)
{
	struct config_state config = { path, 0, false };
	struct io io;
	char buf[SIZEOF_STR];

	/* Do not read configuration from stdin if set to "" */
	if (!path || !strlen(path))
		return SUCCESS;

	if (!path_expand(buf, sizeof(buf), path))
		return error("Failed to expand path: %s", path);

	/* It's OK that the file doesn't exist. */
	if (!io_open(&io, "%s", buf)) {
		/* XXX: Must return ERROR_FILE_DOES_NOT_EXIST so missing
		 * system tigrc is detected properly. */
		if (io_error(&io) == ENOENT)
			return ERROR_FILE_DOES_NOT_EXIST;
		return error("Error loading file %s: %s", buf, io_strerror(&io));
	}

	if (io_load_span(&io, " \t", &config.lineno, read_option, &config) != SUCCESS ||
	    config.errors == true)
		warn("Errors while loading %s.", buf);
	return SUCCESS;
}

extern const char *builtin_config;

enum status_code
load_options(void)
{
	const char *tigrc_user = getenv("TIGRC_USER");
	const char *tigrc_system = getenv("TIGRC_SYSTEM");
	const char *tig_diff_opts = getenv("TIG_DIFF_OPTS");
	const bool diff_opts_from_args = !!opt_diff_options;
	bool custom_tigrc_system = !!tigrc_system;
	char buf[SIZEOF_STR];

	opt_file_filter = true;
	if (!find_option_info_by_value(&opt_diff_context)->seen)
		opt_diff_context = -3;

	if (!custom_tigrc_system)
		tigrc_system = SYSCONFDIR "/tigrc";

	if (!*tigrc_system ||
	    (load_option_file(tigrc_system) == ERROR_FILE_DOES_NOT_EXIST && !custom_tigrc_system)) {
		struct config_state config = { "<built-in>", 0, false };
		struct io io;

		if (!io_from_string(&io, builtin_config))
			return error("Failed to get built-in config");
		if (io_load_span(&io, " \t", &config.lineno, read_option, &config) != SUCCESS || config.errors == true)
			return error("Error in built-in config");
	}

	if (tigrc_user) {
		load_option_file(tigrc_user);
	} else {
		const char *xdg_config_home = getenv("XDG_CONFIG_HOME");

		if (!xdg_config_home || !*xdg_config_home)
			tigrc_user = "~/.config/tig/config";
		else if (!string_format(buf, "%s/tig/config", xdg_config_home))
			return error("Failed to expand $XDG_CONFIG_HOME");
		else
			tigrc_user = buf;

		if (load_option_file(tigrc_user) == ERROR_FILE_DOES_NOT_EXIST)
			load_option_file(TIG_USER_CONFIG);
	}

	if (!diff_opts_from_args && tig_diff_opts && *tig_diff_opts) {
		static const char *diff_opts[SIZEOF_ARG] = { NULL };
		char buf[SIZEOF_STR];
		int argc = 0;

		if (!string_format(buf, "%s", tig_diff_opts) ||
		    !argv_from_string(diff_opts, &argc, buf))
			return error("TIG_DIFF_OPTS contains too many arguments");
		else if (!argv_copy(&opt_diff_options, diff_opts))
			return error("Failed to format TIG_DIFF_OPTS arguments");
	}

	if (argv_contains(opt_diff_options, "--word-diff") ||
	    argv_contains(opt_diff_options, "--word-diff=plain")) {
		opt_word_diff = true;
	}

	return SUCCESS;
}

const char *
format_option_value(const struct option_info *option, char buf[], size_t bufsize)
{
	buf[0] = 0;

	if (!strcmp(option->type, "bool")) {
		bool *opt = option->value;

		if (string_nformat(buf, bufsize, NULL, "%s", *opt ? "yes" : "no"))
			return buf;

	} else if (!strncmp(option->type, "enum", 4)) {
		const char *type = option->type + STRING_SIZE("enum ");
		enum author *opt = option->value;
		const struct enum_map *map = find_enum_map(type);

		if (enum_name_copy(buf, bufsize, map->entries[*opt].name))
			return buf;

	} else if (!strcmp(option->type, "int")) {
		int *opt = option->value;

		if (opt == &opt_diff_context && *opt < 0)
			*opt = -*opt;

		if (string_nformat(buf, bufsize, NULL, "%d", *opt))
			return buf;

	} else if (!strcmp(option->type, "double")) {
		double *opt = option->value;

		if (*opt >= 1) {
			if (string_nformat(buf, bufsize, NULL, "%d", (int) *opt))
				return buf;

		} else if (string_nformat(buf, bufsize, NULL, "%.0f%%", (*opt) * 100)) {
			return buf;
		}

	} else if (!strcmp(option->type, "const char *")) {
		const char **opt = option->value;
		size_t bufpos = 0;

		if (!*opt)
			return "\"\"";
		if (!string_nformat(buf, bufsize, &bufpos, "\"%s\"", *opt))
			return NULL;
		return buf;

	} else if (!strcmp(option->type, "const char **")) {
		const char *sep = "";
		const char ***opt = option->value;
		size_t bufpos = 0;
		int i;

		for (i = 0; (*opt) && (*opt)[i]; i++) {
			const char *arg = (*opt)[i];

			if (!string_nformat(buf, bufsize, &bufpos, "%s%s", sep, arg))
				return NULL;

			sep = " ";
		}

		return buf;

	} else if (!strcmp(option->type, "struct ref_format **")) {
		struct ref_format ***opt = option->value;

		if (format_ref_formats(*opt, buf, bufsize) == SUCCESS)
			return buf;

	} else if (!strcmp(option->type, "view_settings")) {
		struct view_column **opt = option->value;

		if (format_view_config(*opt, buf, bufsize) == SUCCESS)
			return buf;

	} else {
		if (string_nformat(buf, bufsize, NULL, "<%s>", option->type))
			return buf;
	}

	return NULL;
}

static bool
save_option_settings(FILE *file)
{
	char buf[SIZEOF_STR];
	int i;

	if (!io_fprintf(file, "%s", "\n## Settings\n"))
		return false;

	for (i = 0; i < ARRAY_SIZE(option_info); i++) {
		struct option_info *option = &option_info[i];
		const char *name = enum_name(option->name);
		const char *value = format_option_value(option, buf, sizeof(buf));

		if (!value)
			return false;

		if (!suffixcmp(name, strlen(name), "-args"))
			continue;

		if (!io_fprintf(file, "\nset %-25s = %s", name, value))
			return false;
	}

	return true;
}

static bool
save_option_keybinding(void *data, const char *group, struct keymap *keymap,
		       enum request request, const char *key,
		       const struct request_info *req_info,
		       const struct run_request *run_req)
{
	FILE *file = data;

	if (group && !io_fprintf(file, "\n# %s", group))
		return false;

	if (!io_fprintf(file, "\nbind %-10s %-15s ", enum_name(keymap->name), key))
		return false;

	if (req_info) {
		return io_fprintf(file, "%s", enum_name(req_info->name));

	} else {
		const char *sep = format_run_request_flags(run_req);
		int i;

		for (i = 0; run_req->argv[i]; i++) {
			if (!io_fprintf(file, "%s%s", sep, run_req->argv[i]))
				return false;
			sep = " ";
		}

		return true;
	}
}

static bool
save_option_keybindings(FILE *file)
{
	if (!io_fprintf(file, "%s", "\n\n## Keybindings\n"))
		return false;

	return foreach_key(save_option_keybinding, file, false);
}

static bool
save_option_color_name(FILE *file, int color)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(color_map); i++)
		if (color_map[i].value == color)
			return io_fprintf(file, " %-8s", enum_name(color_map[i].name));

	return io_fprintf(file, " color%d", color);
}

static bool
save_option_color_attr(FILE *file, int attr)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(attr_map); i++)
		if ((attr & attr_map[i].value) &&
		    !io_fprintf(file, " %s", enum_name(attr_map[i].name)))
			return false;

	return true;
}

static bool
save_option_color(void *data, const struct line_rule *rule)
{
	FILE *file = data;
	const struct line_info *info;

	for (info = &rule->info; info; info = info->next) {
		const char *prefix = info->prefix ? info->prefix : "";
		const char *prefix_sep = info->prefix ? "." : "";
		const char *quote = *rule->line ? "\"" : "";
		const char *name = *rule->line ? rule->line : enum_name(rule->name);
		int name_width = strlen(prefix) + strlen(prefix_sep) + 2 * strlen(quote) + strlen(name);
		int padding = name_width > 30 ? 0 : 30 - name_width;

		if (!io_fprintf(file, "\ncolor %s%s%s%s%s%-*s",
				      prefix, prefix_sep, quote, name, quote, padding, "")
		    || !save_option_color_name(file, info->fg)
		    || !save_option_color_name(file, info->bg)
		    || !save_option_color_attr(file, info->attr))
			return false;
	}

	return true;
}

static bool
save_option_colors(FILE *file)
{
	if (!io_fprintf(file, "%s", "\n\n## Colors\n"))
		return false;

	return foreach_line_rule(save_option_color, file);
}

enum status_code
save_options(const char *path)
{
	int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0666);
	FILE *file = fd != -1 ? fdopen(fd, "w") : NULL;
	enum status_code code = SUCCESS;

	if (!file)
		return error("%s", strerror(errno));

	if (!io_fprintf(file, "%s", "# Saved by Tig\n")
	    || !save_option_settings(file)
	    || !save_option_keybindings(file)
	    || !save_option_colors(file))
		code = error("Write returned an error");

	fclose(file);
	return code;
}

/*
 * Repository properties
 */

static void
set_remote_branch(const char *name, const char *value, size_t valuelen)
{
	if (!strcmp(name, ".remote")) {
		string_ncopy(repo.remote, value, valuelen);

	} else if (*repo.remote && !strcmp(name, ".merge")) {
		size_t from = strlen(repo.remote);

		if (!prefixcmp(value, "refs/heads/"))
			value += STRING_SIZE("refs/heads/");

		if (!string_format_from(repo.remote, &from, "/%s", value))
			repo.remote[0] = 0;
	}
}

static void
set_repo_config_option(char *name, char *value, enum status_code (*cmd)(int, const char **))
{
	const char *argv[SIZEOF_ARG] = { name, "=" };
	int argc = 1 + (cmd == option_set_command);
	enum status_code code;

	if (!argv_from_string(argv, &argc, value))
		code = error("Too many arguments");
	else
		code = cmd(argc, argv);

	if (code != SUCCESS)
		warn("Option 'tig.%s': %s", name, get_status_message(code));
}

static struct line_info *
parse_git_color_option(struct line_info *info, char *value)
{
	const char *argv[SIZEOF_ARG];
	int argc = 0;
	bool first_color = true;
	int i;

	if (!argv_from_string(argv, &argc, value))
		return NULL;

	info->fg = COLOR_DEFAULT;
	info->bg = COLOR_DEFAULT;
	info->attr = 0;

	for (i = 0; i < argc; i++) {
		int attr = 0;

		if (!strncmp(argv[i], "ul", 2)) {
			argv[i] = "underline";
		}
		if (set_attribute(&attr, argv[i])) {
			info->attr |= attr;

		} else if (set_color(&attr, argv[i])) {
			if (first_color)
				info->fg = attr;
			else
				info->bg = attr;
			first_color = false;
		}
	}
	return info;
}

static void
set_git_color_option(const char *name, char *value)
{
	struct line_info parsed = {0};
	struct line_info *color = NULL;
	size_t namelen = strlen(name);
	int i;

	if (!opt_git_colors)
		return;

	for (i = 0; opt_git_colors[i]; i++) {
		struct line_rule rule = {0};
		const char *prefix = NULL;
		struct line_info *info;
		const char *alias = opt_git_colors[i];
		const char *sep = strchr(alias, '=');

		if (!sep || namelen != sep - alias ||
		    string_enum_compare(name, alias, namelen))
			continue;

		if (!color) {
			color = parse_git_color_option(&parsed, value);
			if (!color)
				return;
		}

		if (parse_color_name(sep + 1, &rule, &prefix) == SUCCESS &&
		    (info = add_line_rule(prefix, &rule))) {
			info->fg = parsed.fg;
			info->bg = parsed.bg;
			info->attr = parsed.attr;
		}
	}
}

static void
set_encoding(struct encoding **encoding_ref, const char *arg, bool priority)
{
	if (!strcasecmp(arg, "utf-8") || !strcasecmp(arg, "utf8"))
		return;
	if (parse_encoding(encoding_ref, arg, priority) == SUCCESS)
		encoding_arg[0] = 0;
}

static enum status_code
read_repo_config_option(char *name, size_t namelen, char *value, size_t valuelen, void *data)
{
	if (!strcmp(name, "i18n.commitencoding"))
		set_encoding(&default_encoding, value, false);

	else if (!strcmp(name, "gui.encoding"))
		set_encoding(&default_encoding, value, true);

	else if (!strcmp(name, "core.editor"))
		string_ncopy(opt_editor, value, valuelen);

	else if (!strcmp(name, "core.worktree"))
		string_ncopy(repo.worktree, value, valuelen);

	else if (!strcmp(name, "core.abbrev"))
		parse_int(&opt_id_width, value, 0, SIZEOF_REV - 1);

	else if (!strcmp(name, "diff.noprefix"))
		parse_bool(&opt_diff_noprefix, value);

	else if (!strcmp(name, "status.showUntrackedFiles"))
		parse_bool(&opt_status_show_untracked_files, value);

	else if (!prefixcmp(name, "tig.color."))
		set_repo_config_option(name + 10, value, option_color_command);

	else if (!prefixcmp(name, "tig.bind."))
		set_repo_config_option(name + 9, value, option_bind_command);

	else if (!prefixcmp(name, "tig."))
		set_repo_config_option(name + 4, value, option_set_command);

	else if (!prefixcmp(name, "color."))
		set_git_color_option(name + STRING_SIZE("color."), value);

	else if (*repo.head && !prefixcmp(name, "branch.") &&
		 !strncmp(name + 7, repo.head, strlen(repo.head)))
		set_remote_branch(name + 7 + strlen(repo.head), value, valuelen);

	else if (!strcmp(name, "diff.context")) {
		if (!find_option_info_by_value(&opt_diff_context)->seen)
			opt_diff_context = -atoi(value);

	} else if (!strcmp(name, "format.pretty")) {
		if (!prefixcmp(value, "format:") && strstr(value, "%C("))
			argv_append(&opt_log_options, "--pretty=medium");

	} else if (!strcmp(name, "log.follow") && opt_file_args && !opt_file_args[1])
		parse_bool(&opt_log_follow, value);

	return SUCCESS;
}

enum status_code
load_git_config(void)
{
	enum status_code code;
	struct io io;
	const char *config_list_argv[] = { "git", "config", "--list", NULL };
	const char *git_worktree = getenv("GIT_WORK_TREE");

	code = io_run_load(&io, config_list_argv, "=", read_repo_config_option, NULL);

	if (git_worktree && *git_worktree)
		string_ncopy(repo.worktree, git_worktree, strlen(git_worktree));

	return code;
}

/* vim: set ts=8 sw=8 noexpandtab: */
