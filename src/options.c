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
#include "tig/types.h"
#include "tig/argv.h"
#include "tig/io.h"
#include "tig/repo.h"
#include "tig/refdb.h"
#include "tig/options.h"
#include "tig/request.h"
#include "tig/line.h"
#include "tig/keys.h"

/*
 * Option variables.
 */

#define DEFINE_OPTION_VARIABLES(name, type, flags) type opt_##name;
OPTION_INFO(DEFINE_OPTION_VARIABLES);

/*
 * State variables.
 */

iconv_t opt_iconv_out		= ICONV_NONE;
char opt_editor[SIZEOF_STR]	= "";
const char **opt_cmdline_argv	= NULL;
const char **opt_rev_argv	= NULL;
const char **opt_file_argv	= NULL;
char opt_env_lines[64]		= "";
char opt_env_columns[64]	= "";
char *opt_env[]			= { opt_env_lines, opt_env_columns, NULL };

/*
 * Mapping between options and command argument mapping.
 */

const char *
diff_context_arg()
{
	static char opt_diff_context_arg[9]	= "";

	if (!string_format(opt_diff_context_arg, "-U%u", opt_diff_context))
		string_ncopy(opt_diff_context_arg, "-U3", 3);

	return opt_diff_context_arg;
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
	ENUM_ARG(COMMIT_ORDER_DEFAULT,	""),
	ENUM_ARG(COMMIT_ORDER_TOPO,	"--topo-order"),
	ENUM_ARG(COMMIT_ORDER_DATE,	"--date-order"),
	ENUM_ARG(COMMIT_ORDER_REVERSE,	"--reverse"),
};

const char *
commit_order_arg()
{
	return commit_order_arg_map[opt_commit_order].name;
}

static char opt_notes_arg[SIZEOF_STR] = "--show-notes";

const char *
show_notes_arg()
{
	if (opt_show_notes)
		return opt_notes_arg;
	/* Notes are disabled by default when passing --pretty args. */
	return "";
}

static bool seen_commit_order_arg;
static bool seen_ignore_space_arg;
static bool seen_diff_context_arg;

void
update_options_from_argv(const char *argv[])
{
	int next, flags_pos;

	for (next = flags_pos = 0; argv[next]; next++) {
		const char *flag = argv[next];
		int value = -1;

		if (map_enum(&value, commit_order_arg_map, flag)) {
			opt_commit_order = value;
			seen_commit_order_arg = TRUE;
			continue;
		}

		if (map_enum(&value, ignore_space_arg_map, flag)) {
			opt_ignore_space = value;
			seen_ignore_space_arg = TRUE;
			continue;
		}

		if (!prefixcmp(flag, "-U")
		    && parse_int(&value, flag + 2, 0, 999999) == SUCCESS) {
			opt_diff_context = value;
			seen_diff_context_arg = TRUE;
			continue;
		}

		if (!strcmp(flag, "--graph")) {
			opt_show_rev_graph = TRUE;
			continue;
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
	*opt = atoi(arg);
	if (!strchr(arg, '%'))
		return SUCCESS;

	/* "Shift down" so 100% and 1 does not conflict. */
	*opt = (*opt - 1) / 100;
	if (*opt >= 1.0) {
		*opt = 0.99;
		return ERROR_INVALID_STEP_VALUE;
	}
	if (*opt < 0.0) {
		*opt = 1;
		return ERROR_INVALID_STEP_VALUE;
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

	return ERROR_INTEGER_VALUE_OUT_OF_BOUND;
}

#define parse_id(opt, arg) \
	parse_int(opt, arg, 4, SIZEOF_REV - 1)

static bool
set_color(int *color, const char *name)
{
	if (map_enum(color, color_map, name))
		return TRUE;
	if (!prefixcmp(name, "color"))
		return parse_int(color, name + 5, 0, 255) == SUCCESS;
	/* Used when reading git colors. Git expects a plain int w/o prefix.  */
	return parse_int(color, name, 0, 255) == SUCCESS;
}

#define is_quoted(c)	((c) == '"' || (c) == '\'')

static enum status_code
parse_color_name(const char *color, struct line_rule *rule, const char **prefix_ptr)
{
	const char *prefixend = is_quoted(*color) ? NULL : strchr(color, '.');

	if (prefixend) {
		struct keymap *keymap = get_keymap(color, prefixend - color);

		if (!keymap)
			return ERROR_UNKNOWN_KEY_MAP;
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
	struct line_rule rule = {};
	const char *prefix = NULL;
	struct line_info *info;
	enum status_code code;

	if (argc < 3)
		return ERROR_WRONG_NUMBER_OF_ARGUMENTS;

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
			{ "pp-adate",			"'AuthorDate: '" },
			{ "pp-author",			"'Author: '" },
			{ "pp-cdate",			"'CommitDate: '" },
			{ "pp-commit",			"'Commit: '" },
			{ "pp-date",			"'Date: '" },
			{ "reviewed",			"'    Reviewed-by'" },
			{ "signoff",			"'    Signed-off-by'" },
			{ "tested",			"'    Tested-by'" },
			{ "tree-dir",			"tree.directory" },
			{ "tree-file",			"tree.file" },
		};
		int index;

		index = find_remapped(obsolete, ARRAY_SIZE(obsolete), rule.name);
		if (index != -1) {
			/* Keep the initial prefix if defined. */
			code = parse_color_name(obsolete[index][1], &rule, prefix ? NULL : &prefix);
			if (code != SUCCESS)
				return code;
			info = add_line_rule(prefix, &rule);
		}

		if (!info)
			return ERROR_UNKNOWN_COLOR_NAME;

		code = ERROR_OBSOLETE_REQUEST_NAME;
	}

	if (!set_color(&info->fg, argv[1]) ||
	    !set_color(&info->bg, argv[2]))
		return ERROR_UNKNOWN_COLOR;

	info->attr = 0;
	while (argc-- > 3) {
		int attr;

		if (!set_attribute(&attr, argv[argc]))
			return ERROR_UNKNOWN_ATTRIBUTE;
		info->attr |= attr;
	}

	return code;
}

static enum status_code
parse_bool_matched(bool *opt, const char *arg, bool *matched)
{
	*opt = (!strcmp(arg, "1") || !strcmp(arg, "true") || !strcmp(arg, "yes"))
		? TRUE : FALSE;
	if (matched)
		*matched = *opt || (!strcmp(arg, "0") || !strcmp(arg, "false") || !strcmp(arg, "no"));
	return SUCCESS;
}

#define parse_bool(opt, arg) parse_bool_matched(opt, arg, NULL)

static enum status_code
parse_enum(unsigned int *opt, const char *arg, const struct enum_map *map)
{
	bool is_true;

	assert(map->size > 1);

	if (map_enum_do(map->entries, map->size, (int *) opt, arg))
		return SUCCESS;

	parse_bool(&is_true, arg);
	*opt = is_true ? map->entries[1].value : map->entries[0].value;
	return SUCCESS;
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

/* Wants: name = value */
static enum status_code
option_set_command(int argc, const char *argv[])
{
	if (argc < 3)
		return ERROR_WRONG_NUMBER_OF_ARGUMENTS;

	if (strcmp(argv[1], "="))
		return ERROR_NO_VALUE_ASSIGNED;

	if (!strcmp(argv[0], "blame-options"))
		return parse_args(&opt_blame_options, argv + 2);

	if (!strcmp(argv[0], "diff-options"))
		return parse_args(&opt_diff_options, argv + 2);

	if (!strcmp(argv[0], "reference-format"))
		return parse_ref_formats(argv + 2);

	if (argc != 3)
		return ERROR_WRONG_NUMBER_OF_ARGUMENTS;

	if (!strcmp(argv[0], "show-author"))
		return parse_enum(&opt_show_author, argv[2], author_map);

	if (!strcmp(argv[0], "show-date"))
		return parse_enum(&opt_show_date, argv[2], date_map);

	if (!strcmp(argv[0], "show-rev-graph"))
		return parse_bool(&opt_show_rev_graph, argv[2]);

	if (!strcmp(argv[0], "show-refs"))
		return parse_bool(&opt_show_refs, argv[2]);

	if (!strcmp(argv[0], "show-changes"))
		return parse_bool(&opt_show_changes, argv[2]);

	if (!strcmp(argv[0], "show-notes")) {
		bool matched = FALSE;
		enum status_code res = parse_bool_matched(&opt_show_notes, argv[2], &matched);

		if (res == SUCCESS && matched)
			return res;

		opt_show_notes = TRUE;
		strcpy(opt_notes_arg, "--show-notes=");
		res = parse_string(opt_notes_arg + 8, argv[2],
				   sizeof(opt_notes_arg) - 8);
		if (res == SUCCESS && opt_notes_arg[8] == '\0')
			opt_notes_arg[7] = '\0';
		return res;
	}

	if (!strcmp(argv[0], "show-line-numbers"))
		return parse_bool(&opt_show_line_numbers, argv[2]);

	if (!strcmp(argv[0], "line-graphics"))
		return parse_enum(&opt_line_graphics, argv[2], graphic_map);

	if (!strcmp(argv[0], "line-number-interval"))
		return parse_int(&opt_line_number_interval, argv[2], 1, 1024);

	if (!strcmp(argv[0], "author-width"))
		return parse_int(&opt_author_width, argv[2], 0, 1024);

	if (!strcmp(argv[0], "filename-width"))
		return parse_int(&opt_show_filename_width, argv[2], 0, 1024);

	if (!strcmp(argv[0], "show-filename"))
		return parse_enum(&opt_show_filename, argv[2], filename_map);

	if (!strcmp(argv[0], "show-file-size"))
		return parse_enum(&opt_show_file_size, argv[2], file_size_map);

	if (!strcmp(argv[0], "horizontal-scroll"))
		return parse_step(&opt_horizontal_scroll, argv[2]);

	if (!strcmp(argv[0], "split-view-height"))
		return parse_step(&opt_split_view_height, argv[2]);

	if (!strcmp(argv[0], "vertical-split"))
		return parse_enum(&opt_vertical_split, argv[2], vertical_split_map);

	if (!strcmp(argv[0], "tab-size"))
		return parse_int(&opt_tab_size, argv[2], 1, 1024);

	if (!strcmp(argv[0], "diff-context"))
		return seen_diff_context_arg ? SUCCESS
			: parse_int(&opt_diff_context, argv[2], 0, 999999);

	if (!strcmp(argv[0], "ignore-space"))
		return seen_ignore_space_arg ? SUCCESS
			: parse_enum(&opt_ignore_space, argv[2], ignore_space_map);

	if (!strcmp(argv[0], "commit-order"))
		return seen_commit_order_arg ? SUCCESS
			: parse_enum(&opt_commit_order, argv[2], commit_order_map);

	if (!strcmp(argv[0], "status-untracked-dirs"))
		return parse_bool(&opt_status_untracked_dirs, argv[2]);

	if (!strcmp(argv[0], "read-git-colors"))
		return parse_bool(&opt_read_git_colors, argv[2]);

	if (!strcmp(argv[0], "ignore-case"))
		return parse_bool(&opt_ignore_case, argv[2]);

	if (!strcmp(argv[0], "focus-child"))
		return parse_bool(&opt_focus_child, argv[2]);

	if (!strcmp(argv[0], "wrap-lines"))
		return parse_bool(&opt_wrap_lines, argv[2]);

	if (!strcmp(argv[0], "show-id"))
		return parse_bool(&opt_show_id, argv[2]);

	if (!strcmp(argv[0], "id-width"))
		return parse_id(&opt_id_width, argv[2]);

	if (!strcmp(argv[0], "title-overflow")) {
		bool enabled = FALSE;
		bool matched;
		enum status_code code;

		/*
		 * "title-overflow" is considered a boolint.
		 * We try to parse it as a boolean (and set the value to 50 if true),
		 * otherwise we parse it as an integer and use the given value.
		 */
		code = parse_bool_matched(&enabled, argv[2], &matched);
		if (code == SUCCESS && matched) {
			if (enabled)
				opt_title_overflow = 50;
		} else {
			code = parse_int(&opt_title_overflow, argv[2], 2, 1024);
			if (code != SUCCESS)
				opt_title_overflow = 50;
		}

		return code;
	}

	if (!strcmp(argv[0], "editor-line-number"))
		return parse_bool(&opt_editor_line_number, argv[2]);

	if (!strcmp(argv[0], "mouse"))
		return parse_bool(&opt_mouse, argv[2]);

	if (!strcmp(argv[0], "mouse-scroll"))
		return parse_int(&opt_mouse_scroll, argv[2], 0, 1024);

	return ERROR_UNKNOWN_VARIABLE_NAME;
}

/* Wants: mode request key */
static enum status_code
option_bind_command(int argc, const char *argv[])
{
	struct key_input input;
	enum request request;
	struct keymap *keymap;

	if (argc < 3)
		return ERROR_WRONG_NUMBER_OF_ARGUMENTS;

	if (!(keymap = get_keymap(argv[0], strlen(argv[0])))) {
		if (!strcmp(argv[0], "branch"))
			keymap = get_keymap("refs", strlen("refs"));
		if (!keymap)
			return ERROR_UNKNOWN_KEY_MAP;
	}

	if (get_key_value(argv[1], &input) == ERR)
		return ERROR_UNKNOWN_KEY;

	request = get_request(argv[2]);
	if (request == REQ_UNKNOWN) {
		static const struct enum_map_entry obsolete[] = {
			ENUM_MAP_ENTRY("cherry-pick",	REQ_NONE),
			ENUM_MAP_ENTRY("screen-resize",	REQ_NONE),
			ENUM_MAP_ENTRY("tree-parent",	REQ_PARENT),
			ENUM_MAP_ENTRY("view-branch",	REQ_VIEW_REFS),
		};
		static const char *toggles[][2] = {
			{ "diff-context-down",		"diff-context" },
			{ "diff-context-up",		"diff-context" },
			{ "toggle-author",		"show-author" },
			{ "toggle-changes",		"show-changes" },
			{ "toggle-commit-order",	"show-commit-order" },
			{ "toggle-date",		"show-date" },
			{ "toggle-file-filter",		"file-filter" },
			{ "toggle-file-size",		"show-file-size" },
			{ "toggle-filename",		"show-filename" },
			{ "toggle-graphic",		"show-graphic" },
			{ "toggle-id",			"show-id" },
			{ "toggle-ignore-space",	"show-ignore-space" },
			{ "toggle-lineno",		"show-line-numbers" },
			{ "toggle-refs",		"show-refs" },
			{ "toggle-rev-graph",		"show-rev-graph" },
			{ "toggle-sort-field",		"sort-field" },
			{ "toggle-sort-order",		"sort-order" },
			{ "toggle-title-overflow",	"show-title-overflow" },
			{ "toggle-untracked-dirs",	"status-untracked-dirs" },
			{ "toggle-vertical-split",	"show-vertical-split" },
		};
		int alias;

		if (map_enum(&alias, obsolete, argv[2])) {
			if (alias != REQ_NONE)
				add_keybinding(keymap, alias, &input);
			return ERROR_OBSOLETE_REQUEST_NAME;
		}

		alias = find_remapped(toggles, ARRAY_SIZE(toggles), argv[2]);
		if (alias != -1) {
			const char *action = toggles[alias][0];
			const char *arg = prefixcmp(action, "diff-context-")
					? NULL : (strstr(action, "-down") ? "-1" : "+1");
			const char *toggle[] = { ":toggle", toggles[alias][1], arg, NULL};
			enum status_code code = add_run_request(keymap, &input, toggle);

			return code == SUCCESS ? ERROR_OBSOLETE_REQUEST_NAME : code;
		}
	}

	if (request == REQ_UNKNOWN)
		return add_run_request(keymap, &input, argv + 2);

	return add_keybinding(keymap, request, &input);
}


static enum status_code load_option_file(const char *path);

static enum status_code
option_source_command(int argc, const char *argv[])
{
	if (argc < 1)
		return ERROR_WRONG_NUMBER_OF_ARGUMENTS;

	return load_option_file(argv[0]);
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

	return ERROR_UNKNOWN_OPTION_COMMAND;
}

struct config_state {
	const char *path;
	int lineno;
	bool errors;
};

static int
read_option(char *opt, size_t optlen, char *value, size_t valuelen, void *data)
{
	struct config_state *config = data;
	enum status_code status = ERROR_NO_OPTION_VALUE;

	config->lineno++;

	/* Check for comment markers, since read_properties() will
	 * only ensure opt and value are split at first " \t". */
	optlen = strcspn(opt, "#");
	if (optlen == 0)
		return OK;

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
			status = ERROR_TOO_MANY_OPTION_ARGUMENTS;
		else
			status = set_option(opt, argc, argv);
	}

	if (status != SUCCESS) {
		warn("%s line %d: %s near '%.*s'", config->path, config->lineno,
		     get_status_message(status), (int) optlen, opt);
		config->errors = TRUE;
	}

	/* Always keep going if errors are encountered. */
	return OK;
}

static enum status_code
load_option_file(const char *path)
{
	struct config_state config = { path, 0, FALSE };
	struct io io;
	char buf[SIZEOF_STR];

	/* Do not read configuration from stdin if set to "" */
	if (!path || !strlen(path))
		return SUCCESS;

	if (!prefixcmp(path, "~/")) {
		const char *home = getenv("HOME");

		if (!home || !string_format(buf, "%s/%s", home, path + 2))
			return ERROR_HOME_UNRESOLVABLE;
		path = buf;
	}

	/* It's OK that the file doesn't exist. */
	if (!io_open(&io, "%s", path))
		return ERROR_FILE_DOES_NOT_EXIST;

	if (io_load(&io, " \t", read_option, &config) == ERR ||
	    config.errors == TRUE)
		warn("Errors while loading %s.", path);
	return SUCCESS;
}

extern const char *builtin_config;

int
load_options(void)
{
	const char *tigrc_user = getenv("TIGRC_USER");
	const char *tigrc_system = getenv("TIGRC_SYSTEM");
	const char *tig_diff_opts = getenv("TIG_DIFF_OPTS");
	const bool diff_opts_from_args = !!opt_diff_options;
	bool custom_tigrc_system = !!tigrc_system;

	opt_file_filter = TRUE;

	if (!custom_tigrc_system)
		tigrc_system = SYSCONFDIR "/tigrc";

	if (load_option_file(tigrc_system) == ERROR_FILE_DOES_NOT_EXIST && !custom_tigrc_system) {
		struct config_state config = { "<built-in>", 0, FALSE };
		struct io io;

		if (!io_from_string(&io, builtin_config) ||
		    !io_load(&io, " \t", read_option, &config) == ERR ||
		    config.errors == TRUE)
			die("Error in built-in config");
	}

	if (!tigrc_user)
		tigrc_user = "~/.tigrc";
	load_option_file(tigrc_user);

	if (!diff_opts_from_args && tig_diff_opts && *tig_diff_opts) {
		static const char *diff_opts[SIZEOF_ARG] = { NULL };
		char buf[SIZEOF_STR];
		int argc = 0;

		if (!string_format(buf, "%s", tig_diff_opts) ||
		    !argv_from_string(diff_opts, &argc, buf))
			die("TIG_DIFF_OPTS contains too many arguments");
		else if (!argv_copy(&opt_diff_options, diff_opts))
			die("Failed to format TIG_DIFF_OPTS arguments");
	}

	return OK;
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
	enum status_code error;

	if (!argv_from_string(argv, &argc, value))
		error = ERROR_TOO_MANY_OPTION_ARGUMENTS;
	else
		error = cmd(argc, argv);

	if (error != SUCCESS)
		warn("Option 'tig.%s': %s", name, get_status_message(error));
}

static void
set_work_tree(const char *value)
{
	char cwd[SIZEOF_STR];

	if (!getcwd(cwd, sizeof(cwd)))
		die("Failed to get cwd path: %s", strerror(errno));
	if (chdir(cwd) < 0)
		die("Failed to chdir(%s): %s", cwd, strerror(errno));
	if (chdir(repo.git_dir) < 0)
		die("Failed to chdir(%s): %s", repo.git_dir, strerror(errno));
	if (!getcwd(repo.git_dir, sizeof(repo.git_dir)))
		die("Failed to get git path: %s", strerror(errno));
	if (chdir(value) < 0)
		die("Failed to chdir(%s): %s", value, strerror(errno));
	if (!getcwd(cwd, sizeof(cwd)))
		die("Failed to get cwd path: %s", strerror(errno));
	if (setenv("GIT_WORK_TREE", cwd, TRUE))
		die("Failed to set GIT_WORK_TREE to '%s'", cwd);
	if (setenv("GIT_DIR", repo.git_dir, TRUE))
		die("Failed to set GIT_DIR to '%s'", repo.git_dir);
	repo.is_inside_work_tree = TRUE;
}

static void
parse_git_color_option(enum line_type type, char *value)
{
	struct line_info *info = get_line_info(NULL, type);
	const char *argv[SIZEOF_ARG];
	int argc = 0;
	bool first_color = TRUE;
	int i;

	if (!argv_from_string(argv, &argc, value))
		return;

	info->fg = COLOR_DEFAULT;
	info->bg = COLOR_DEFAULT;
	info->attr = 0;

	for (i = 0; i < argc; i++) {
		int attr = 0;

		if (set_attribute(&attr, argv[i])) {
			info->attr |= attr;

		} else if (set_color(&attr, argv[i])) {
			if (first_color)
				info->fg = attr;
			else
				info->bg = attr;
			first_color = FALSE;
		}
	}
}

static void
set_git_color_option(const char *name, char *value)
{
	static const struct enum_map_entry color_option_map[] = {
		ENUM_MAP_ENTRY("branch.current", LINE_MAIN_HEAD),
		ENUM_MAP_ENTRY("branch.local", LINE_MAIN_REF),
		ENUM_MAP_ENTRY("branch.plain", LINE_MAIN_REF),
		ENUM_MAP_ENTRY("branch.remote", LINE_MAIN_REMOTE),

		ENUM_MAP_ENTRY("diff.meta", LINE_DIFF_HEADER),
		ENUM_MAP_ENTRY("diff.meta", LINE_DIFF_INDEX),
		ENUM_MAP_ENTRY("diff.meta", LINE_DIFF_OLDMODE),
		ENUM_MAP_ENTRY("diff.meta", LINE_DIFF_NEWMODE),
		ENUM_MAP_ENTRY("diff.frag", LINE_DIFF_CHUNK),
		ENUM_MAP_ENTRY("diff.old", LINE_DIFF_DEL),
		ENUM_MAP_ENTRY("diff.new", LINE_DIFF_ADD),

		//ENUM_MAP_ENTRY("diff.commit", LINE_DIFF_ADD),

		ENUM_MAP_ENTRY("status.branch", LINE_STAT_HEAD),
		//ENUM_MAP_ENTRY("status.nobranch", LINE_STAT_HEAD),
		ENUM_MAP_ENTRY("status.added", LINE_STAT_STAGED),
		ENUM_MAP_ENTRY("status.updated", LINE_STAT_STAGED),
		ENUM_MAP_ENTRY("status.changed", LINE_STAT_UNSTAGED),
		ENUM_MAP_ENTRY("status.untracked", LINE_STAT_UNTRACKED),
	};
	int type = LINE_NONE;

	if (opt_read_git_colors && map_enum(&type, color_option_map, name)) {
		parse_git_color_option(type, value);
	}
}

static void
set_encoding(struct encoding **encoding_ref, const char *arg, bool priority)
{
	if (parse_encoding(encoding_ref, arg, priority) == SUCCESS)
		encoding_arg[0] = 0;
}

static int
read_repo_config_option(char *name, size_t namelen, char *value, size_t valuelen, void *data)
{
	if (!strcmp(name, "i18n.commitencoding"))
		set_encoding(&default_encoding, value, FALSE);

	else if (!strcmp(name, "gui.encoding"))
		set_encoding(&default_encoding, value, TRUE);

	else if (!strcmp(name, "core.editor"))
		string_ncopy(opt_editor, value, valuelen);

	else if (!strcmp(name, "core.worktree"))
		set_work_tree(value);

	else if (!strcmp(name, "core.abbrev"))
		parse_id(&opt_id_width, value);

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

	return OK;
}

int
load_git_config(void)
{
	const char *config_list_argv[] = { "git", "config", "--list", NULL };

	return io_run_load(config_list_argv, "=", read_repo_config_option, NULL);
}

/* vim: set ts=8 sw=8 noexpandtab: */
