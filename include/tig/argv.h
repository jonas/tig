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

#ifndef TIG_ARGV_H
#define TIG_ARGV_H

#include "tig/tig.h"

/*
 * Argument array helpers.
 */

#define SIZEOF_ARG	32	/* Default argument array size. */

bool argv_to_string(const char *argv[SIZEOF_ARG], char *buf, size_t buflen, const char *sep);
bool argv_from_string_no_quotes(const char *argv[SIZEOF_ARG], int *argc, char *cmd);
bool argv_from_string(const char *argv[SIZEOF_ARG], int *argc, char *cmd);
bool argv_from_env(const char **argv, const char *name);
void argv_free(const char *argv[]);
size_t argv_size(const char **argv);
bool argv_append(const char ***argv, const char *arg);
bool argv_append_array(const char ***dst_argv, const char *src_argv[]);
bool argv_copy(const char ***dst, const char *src[]);
bool argv_remove_quotes(const char *argv[]);
bool argv_contains(const char **argv, const char *arg);

#define ARGV_ENV_INFO(_) \
	_(commit,	"",	"HEAD"), \
	_(blob,	 	"",	""), \
	_(branch,	"",	""), \
	_(directory,	".",	""), \
	_(file,	 	"",	""), \
	_(head,	 	"",	"HEAD"), \
	_(ref,	 	"HEAD",	""), \
	_(stash,	"",	""), \
	_(status,	"",	"")

#define ARGV_ENV_FIELDS(name, ifempty, initval)	name[SIZEOF_STR]

struct argv_env {
	char ARGV_ENV_INFO(ARGV_ENV_FIELDS);
	unsigned long lineno;
	char search[SIZEOF_STR];
	char none[1];
};

extern struct argv_env argv_env;

bool argv_format(struct argv_env *argv_env, const char ***dst_argv, const char *src_argv[], bool first, bool file_filter);

struct rev_flags {
	size_t search_offset;
	bool with_graph;
};

bool argv_parse_rev_flag(const char *arg, struct rev_flags *flags);

#endif
/* vim: set ts=8 sw=8 noexpandtab: */
