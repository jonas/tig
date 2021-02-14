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
#include "tig/argv.h"
#include "tig/repo.h"
#include "tig/options.h"
#include "tig/prompt.h"

static bool
concat_argv(const char *argv[], char *buf, size_t buflen, const char *sep, bool quoted)
{
	size_t bufpos, argc;

	for (bufpos = 0, argc = 0; argv[argc]; argc++) {
		const char *arg_sep = argc ? sep : "";
		const char *arg = argv[argc];

		if (quoted && arg[strcspn(arg, " \t\"")]) {
			if (!string_nformat(buf, buflen, &bufpos, "%s\"", arg_sep))
				return false;

			while (*arg) {
				int pos = strcspn(arg, "\"");
				const char *qesc = arg[pos] == '"' ? "\\\"" : "";

				if (!string_nformat(buf, buflen, &bufpos, "%.*s%s", pos, arg, qesc))
					return false;
				if (!arg[pos])
					break;
				else
					arg += pos + 1;
			}

			if (!string_nformat(buf, buflen, &bufpos, "\""))
				return false;

			continue;
		}

		if (!string_nformat(buf, buflen, &bufpos, "%s%s", arg_sep, arg))
			return false;
	}

	return true;
}

char *
argv_to_string_alloc(const char *argv[], const char *sep)
{
	size_t i, size = 0;
	char *buf;

	for (i = 0; argv[i]; i++)
		size += strlen(argv[i]);

	buf = malloc(size + 1);
	if (buf && argv_to_string(argv, buf, size + 1, sep))
		return buf;
	free(buf);
	return NULL;
}

bool
argv_to_string_quoted(const char *argv[SIZEOF_ARG], char *buf, size_t buflen, const char *sep)
{
	return concat_argv(argv, buf, buflen, sep, true);
}

bool
argv_to_string(const char *argv[SIZEOF_ARG], char *buf, size_t buflen, const char *sep)
{
	return concat_argv(argv, buf, buflen, sep, false);
}

static char *
parse_arg(char **cmd, bool remove_quotes)
{
	int quote = 0;
	char *arg = *cmd;
	char *next, *pos;

	for (pos = next = arg; *pos; pos++) {
		int c = *pos;

		if (c == '"' || c == '\'') {
			if (quote == c) {
				quote = 0;
				if (remove_quotes) {
					if (pos == arg) {
						arg++;
						next++;
					}
					continue;
				}

			} else if (!quote) {
				quote = c;
				if (remove_quotes) {
					if (pos == arg) {
						arg++;
						next++;
					}
					continue;
				}
			}

		} else if (quote && c == '\\') {
			if (remove_quotes) {
				if (pos == arg) {
					arg++;
					next++;
				}
			} else {
				*next++ = *pos;
			}
			pos++;
			if (!*pos)
				break;
		}

		if (!quote && isspace(c))
			break;

		*next++ = *pos;
	}

	if (*pos)
		*cmd = pos + 1;
	else
		*cmd = pos;
	*next = 0;
	return (!remove_quotes || !quote) ? arg : NULL;
}

static bool
split_argv_string(const char *argv[SIZEOF_ARG], int *argc, char *cmd, bool remove_quotes)
{
	while (*cmd && *argc < SIZEOF_ARG) {
		char *arg = parse_arg(&cmd, remove_quotes);

		if (!arg)
			break;
		argv[(*argc)++] = arg;
		cmd = string_trim(cmd);
	}

	if (*argc < SIZEOF_ARG)
		argv[*argc] = NULL;
	return *argc < SIZEOF_ARG;
}

bool
argv_from_string_no_quotes(const char *argv[SIZEOF_ARG], int *argc, char *cmd)
{
	return split_argv_string(argv, argc, cmd, true);
}

bool
argv_from_string(const char *argv[SIZEOF_ARG], int *argc, char *cmd)
{
	return split_argv_string(argv, argc, cmd, false);
}

void
argv_free(const char *argv[])
{
	int argc;

	if (!argv)
		return;
	for (argc = 0; argv[argc]; argc++)
		free((void *) argv[argc]);
	argv[0] = NULL;
}

size_t
argv_size(const char **argv)
{
	int argc = 0;

	while (argv && argv[argc])
		argc++;

	return argc;
}

bool
argv_contains(const char **argv, const char *arg)
{
	int i;

	for (i = 0; argv && argv[i]; i++)
		if (!strcmp(argv[i], arg))
			return true;
	return false;
}

DEFINE_ALLOCATOR(argv_realloc, const char *, SIZEOF_ARG)

bool
argv_appendn(const char ***argv, const char *arg, size_t arglen)
{
	size_t argc = argv_size(*argv);
	char *alloc;

	if (!*arg && argc > 0)
		return true;

	if (!argv_realloc(argv, argc, 2))
		return false;

	alloc = strndup(arg, arglen);

	(*argv)[argc++] = alloc;
	(*argv)[argc] = NULL;

	return alloc != NULL;
}


bool
argv_append(const char ***argv, const char *arg)
{
	return argv_appendn(argv, arg, strlen(arg));
}

bool
argv_append_array(const char ***dst_argv, const char *src_argv[])
{
	int i;

	for (i = 0; src_argv && src_argv[i]; i++)
		if (!argv_append(dst_argv, src_argv[i]))
			return false;
	return true;
}

bool
argv_copy(const char ***dst, const char *src[])
{
	int argc;

	argv_free(*dst);
	for (argc = 0; src[argc]; argc++)
		if (!argv_append(dst, src[argc]))
			return false;
	return true;
}

/*
 * Argument formatting.
 */

struct format_context;

struct format_var {
	const char *name;
	size_t namelen;
	bool (*formatter)(struct format_context *, struct format_var *);
	void *value_ref;
	const char *value_if_empty;
};

struct format_context {
	struct format_var *vars;
	size_t vars_size;
	char buf[SIZEOF_MED_STR];
	size_t bufpos;
	bool file_filter;
};

#define ARGV_ENV_INIT(type, name, ifempty, initval)	initval,

struct argv_env argv_env = {
	ARGV_ENV_INFO(ARGV_ENV_INIT)
};

static bool
format_expand_arg(struct format_context *format, const char *name, const char *end)
{
	struct format_var *vars = format->vars;
	int i;

	if (!prefixcmp(name, "%(prompt")) {
		const char *prompt = "Command argument: ";
		char msgbuf[SIZEOF_STR];
		const char *value;
		const char *msgstart = name + STRING_SIZE("%(prompt");
		const int msglen = end - msgstart - 1;

		if (end && msglen > 0 && string_format(msgbuf, "%.*s", msglen, msgstart)) {
			const char *msg = msgbuf;

			while (isspace(*msg))
				msg++;
			if (*msg)
				prompt = msg;
		}

		value = read_prompt(prompt);
		if (value == NULL)
			return false;
		return string_format_from(format->buf, &format->bufpos, "%s", value);
	}

	for (i = 0; i < format->vars_size; i++) {
		if (string_enum_compare(name, vars[i].name, vars[i].namelen))
			continue;

		if (vars[i].value_ref == &argv_env.file && !format->file_filter)
			return true;

		return vars[i].formatter(format, &vars[i]);
	}

	return false;
}

static bool
format_append_arg(struct format_context *format, const char ***dst_argv, const char *arg)
{
	memset(format->buf, 0, sizeof(format->buf));
	format->bufpos = 0;

	while (arg) {
		const char *var = strstr(arg, "%(");
		const char *closing = var ? strchr(var, ')') : NULL;
		const char *next = closing ? closing + 1 : NULL;
		const int len = var ? var - arg : strlen(arg);

		if (var && !closing)
			return false;

		if (len && !string_format_from(format->buf, &format->bufpos, "%.*s", len, arg))
			return false;

		if (var && !format_expand_arg(format, var, next))
			return false;

		arg = next;
	}

	return argv_append(dst_argv, format->buf);
}

static bool
format_append_argv(struct format_context *format, const char ***dst_argv, const char *src_argv[])
{
	int argc;

	if (!src_argv)
		return true;

	for (argc = 0; src_argv[argc]; argc++)
		if (!format_append_arg(format, dst_argv, src_argv[argc]))
			return false;

	return src_argv[argc] == NULL;
}

static bool
argv_string_formatter(struct format_context *format, struct format_var *var)
{
	argv_string *value_ref = var->value_ref;
	const char *value = *value_ref;

	if (!*value)
		value = var->value_if_empty;

	if (!*value)
		return true;

	return string_format_from(format->buf, &format->bufpos, "%s", value);
}

static bool
argv_number_formatter(struct format_context *format, struct format_var *var)
{
	unsigned long value = *(unsigned long *) var->value_ref;

	return string_format_from(format->buf, &format->bufpos, "%lu", value);
}

static bool
bool_formatter(struct format_context *format, struct format_var *var)
{
	bool value = *(bool *)var->value_ref;

	return string_format_from(format->buf, &format->bufpos, "%s", value ? "true" : "false");
}

static bool
repo_str_formatter(struct format_context *format, struct format_var *var)
{
	return argv_string_formatter(format, var);
}

static bool
repo_ref_formatter(struct format_context *format, struct format_var *var)
{
	return argv_string_formatter(format, var);
}

static bool
repo_rev_formatter(struct format_context *format, struct format_var *var)
{
	return argv_string_formatter(format, var);
}

bool
argv_format(struct argv_env *argv_env, const char ***dst_argv, const char *src_argv[], bool first, bool file_filter)
{
	struct format_var vars[] = {
#define FORMAT_VAR(type, name, ifempty, initval) \
	{ "%(" #name ")", STRING_SIZE("%(" #name ")"), type ## _formatter, &argv_env->name, ifempty },
		ARGV_ENV_INFO(FORMAT_VAR)
#define FORMAT_REPO_VAR(type, name) \
	{ "%(repo:" #name ")", STRING_SIZE("%(repo:" #name ")"), type ## _formatter, &repo.name, "" },
		REPO_INFO(FORMAT_REPO_VAR)
	};
	struct format_context format = { vars, ARRAY_SIZE(vars), "", 0, file_filter };
	int argc;

	argv_free(*dst_argv);

	for (argc = 0; src_argv[argc]; argc++) {
		const char *arg = src_argv[argc];

		if (!strcmp(arg, "%(fileargs)")) {
			if (file_filter && !argv_append_array(dst_argv, opt_file_args))
				break;

		} else if (!strcmp(arg, DIFF_ARGS)) {
			if (!format_append_argv(&format, dst_argv, opt_diff_options))
				break;

		} else if (!strcmp(arg, "%(blameargs)")) {
			if (!format_append_argv(&format, dst_argv, opt_blame_options))
				break;

		} else if (!strcmp(arg, "%(logargs)")) {
			if (!format_append_argv(&format, dst_argv, opt_log_options))
				break;

		} else if (!strcmp(arg, "%(mainargs)")) {
			if (!format_append_argv(&format, dst_argv, opt_main_options))
				break;

		} else if (!strcmp(arg, "%(cmdlineargs)")) {
			if (!format_append_argv(&format, dst_argv, opt_cmdline_args))
				break;

		} else if (!strcmp(arg, "%(revargs)") ||
			   (first && !strcmp(arg, "%(commit)"))) {
			if (!argv_append_array(dst_argv, opt_rev_args))
				break;

		} else if (!format_append_arg(&format, dst_argv, arg)) {
			break;
		}
	}

	return src_argv[argc] == NULL;
}

static inline bool
argv_find_rev_flag(const char *argv[], size_t argc, const char *arg, size_t arglen,
		   size_t *search_offset, bool *with_graph, bool *with_reflog)
{
	int i;

	for (i = 0; i < argc; i++) {
		const char *flag = argv[i];
		size_t flaglen = strlen(flag);

		if (flaglen > arglen || strncmp(arg, flag, flaglen))
			continue;

		if (search_offset)
			*search_offset = flaglen;
		else if (flaglen != arglen && flag[flaglen - 1] != '=')
			continue;

		if (with_graph)
			*with_graph = false;
		if (with_reflog)
			*with_reflog = true;

		return true;
	}

	return false;
}

bool
argv_parse_rev_flag(const char *arg, struct rev_flags *rev_flags)
{
	static const char *with_graph[] = {
		"--after=",
		"--all",
		"--all-match",
		"--ancestry-path",
		"--author-date-order",
		"--basic-regexp",
		"--before=",
		"--boundary",
		"--branches",
		"--branches=",
		"--cherry",
		"--cherry-mark",
		"--cherry-pick",
		"--committer=",
		"--date-order",
		"--dense",
		"--exclude=",
		"--extended-regexp",
		"--first-parent",
		"--fixed-strings",
		"--full-history",
		"--graph",
		"--glob=",
		"--left-only",
		"--max-parents=",
		"--max-age=",
		"--merge",
		"--merges",
		"--min-parents=",
		"--no-max-parents",
		"--no-min-parents",
		"--no-walk",
		"--perl-regexp",
		"--pickaxe-all",
		"--pickaxe-regex",
		"--regexp-ignore-case",
		"--remotes",
		"--remotes=",
		"--remove-empty",
		"--reverse",
		"--right-only",
		"--simplify-by-decoration",
		"--simplify-merges",
		"--since=",
		"--skip=",
		"--sparse",
		"--stdin",
		"--tags",
		"--tags=",
		"--topo-order",
		"--until=",
		"-E",
		"-F",
		"-i",
	};
	static const char *no_graph[] = {
		"--no-merges",
		"--follow",
		"--author=",
	};
	static const char *with_reflog[] = {
		"--walk-reflogs",
		"-g",
	};
	static const char *search_no_graph[] = {
		"--grep-reflog=",
		"--grep=",
		"-G",
		"-S",
	};
	size_t arglen = strlen(arg);
	bool graph = true;
	bool reflog = false;
	size_t search = 0;

	if (argv_find_rev_flag(with_graph, ARRAY_SIZE(with_graph), arg, arglen, NULL, NULL, NULL) ||
	    argv_find_rev_flag(no_graph, ARRAY_SIZE(no_graph), arg, arglen, NULL, &graph, NULL) ||
	    argv_find_rev_flag(with_reflog, ARRAY_SIZE(with_reflog), arg, arglen, NULL, NULL, &reflog) ||
	    argv_find_rev_flag(search_no_graph, ARRAY_SIZE(search_no_graph), arg, arglen, &search, &graph, NULL)) {
		if (rev_flags) {
			rev_flags->search_offset = search ? search : arglen;
			rev_flags->with_graph = graph;
			rev_flags->with_reflog = reflog;
		}
		return true;
	}

	return false;
}

char *
argv_format_arg(struct argv_env *argv_env, const char *src_arg)
{
	const char *src_argv[] = { src_arg, NULL };
	const char **dst_argv = NULL;
	char *dst_arg = NULL;

	if (argv_format(argv_env, &dst_argv, src_argv, false, true))
		dst_arg = (char *) dst_argv[0];

	free(dst_argv);
	return dst_arg;
}

/* vim: set ts=8 sw=8 noexpandtab: */
