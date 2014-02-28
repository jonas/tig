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

#include "tig.h"
#include "argv.h"

bool
argv_to_string(const char *argv[SIZEOF_ARG], char *buf, size_t buflen, const char *sep)
{
	size_t bufpos, argc;

	for (bufpos = 0, argc = 0; argv[argc]; argc++)
		if (!string_nformat(buf, buflen, &bufpos, "%s%s",
				argc ? sep : "", argv[argc]))
			return FALSE;

	return TRUE;
}

static inline int
get_arg_valuelen(const char *arg, char *quoted)
{
	if (*arg == '"' || *arg == '\'') {
		const char *end = *arg == '"' ? "\"" : "'";
		int valuelen = strcspn(arg + 1, end);

		if (quoted)
			*quoted = *arg;
		return valuelen > 0 ? valuelen + 2 : strlen(arg);
	} else {
		if (quoted)
			*quoted = 0;
		return strcspn(arg, " \t");
	}
}

static bool
split_argv_string(const char *argv[SIZEOF_ARG], int *argc, char *cmd, bool remove_quotes)
{
	while (*cmd && *argc < SIZEOF_ARG) {
		char quoted = 0;
		int valuelen = get_arg_valuelen(cmd, &quoted);
		bool advance = cmd[valuelen] != 0;
		int quote_offset = !!(quoted && remove_quotes);

		cmd[valuelen - quote_offset] = 0;
		argv[(*argc)++] = chomp_string(cmd + quote_offset);
		cmd = chomp_string(cmd + valuelen + advance);
	}

	if (*argc < SIZEOF_ARG)
		argv[*argc] = NULL;
	return *argc < SIZEOF_ARG;
}

bool
argv_from_string_no_quotes(const char *argv[SIZEOF_ARG], int *argc, char *cmd)
{
	return split_argv_string(argv, argc, cmd, TRUE);
}

bool
argv_from_string(const char *argv[SIZEOF_ARG], int *argc, char *cmd)
{
	return split_argv_string(argv, argc, cmd, FALSE);
}

bool
argv_from_env(const char **argv, const char *name)
{
	char *env = argv ? getenv(name) : NULL;
	int argc = 0;

	if (env && *env)
		env = strdup(env);
	return !env || argv_from_string(argv, &argc, env);
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
			return TRUE;
	return FALSE;
}

DEFINE_ALLOCATOR(argv_realloc, const char *, SIZEOF_ARG)

bool
argv_append(const char ***argv, const char *arg)
{
	size_t argc = argv_size(*argv);
	char *alloc;

	if (!*arg && argc > 0)
		return TRUE;

	if (!argv_realloc(argv, argc, 2))
		return FALSE;

	alloc = strdup(arg);

	(*argv)[argc++] = alloc;
	(*argv)[argc] = NULL;

	return alloc != NULL;
}

bool
argv_append_array(const char ***dst_argv, const char *src_argv[])
{
	int i;

	for (i = 0; src_argv && src_argv[i]; i++)
		if (!argv_append(dst_argv, src_argv[i]))
			return FALSE;
	return TRUE;
}

bool
argv_remove_quotes(const char *argv[])
{
	int argc;

	for (argc = 0; argv[argc]; argc++) {
		char quoted = 0;
		const char *arg = argv[argc];
		int arglen = get_arg_valuelen(arg, &quoted);
		int unquotedlen = arglen - 1 - (arg[arglen - 1] == quoted);
		char *unquoted;

		if (!quoted)
			continue;

		unquoted = malloc(unquotedlen + 1);
		if (!unquoted)
			return FALSE;
		strncpy(unquoted, arg + 1, unquotedlen);
		unquoted[unquotedlen] = 0;
		free((void *) arg);
		argv[argc] = unquoted;
	}

	return TRUE;
}

bool
argv_copy(const char ***dst, const char *src[])
{
	int argc;

	argv_free(*dst);
	for (argc = 0; src[argc]; argc++)
		if (!argv_append(dst, src[argc]))
			return FALSE;
	return TRUE;
}

/* vim: set ts=8 sw=8 noexpandtab: */
