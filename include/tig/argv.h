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

#ifndef TIG_ARGV_H
#define TIG_ARGV_H

#include "tig/tig.h"

/*
 * Argument array helpers.
 */

#define SIZEOF_ARG	32	/* Default argument array size. */
#define DIFF_ARGS "%(diffargs)"

bool argv_to_string(const char *argv[], char *buf, size_t buflen, const char *sep);
char *argv_to_string_alloc(const char *argv[], const char *sep);
bool argv_to_string_quoted(const char *argv[SIZEOF_ARG], char *buf, size_t buflen, const char *sep);
bool argv_from_string_no_quotes(const char *argv[SIZEOF_ARG], int *argc, char *cmd);
bool argv_from_string(const char *argv[SIZEOF_ARG], int *argc, char *cmd);
void argv_free(const char *argv[]);
size_t argv_size(const char **argv);
bool argv_append(const char ***argv, const char *arg);
bool argv_appendn(const char ***argv, const char *arg, size_t arglen);
bool argv_append_array(const char ***dst_argv, const char *src_argv[]);
bool argv_copy(const char ***dst, const char *src[]);
bool argv_contains(const char **argv, const char *arg);

typedef char argv_string[SIZEOF_STR];
typedef unsigned long argv_number;

#define ARGV_ENV_INFO(_) \
	_(argv_string,	 commit,	"",		"HEAD") \
	_(argv_string,	 blob,		"",		"") \
	_(argv_string,	 branch,	"",		"") \
	_(argv_string,	 directory,	".",		"") \
	_(argv_string,	 file,		"",		"") \
	_(argv_string,	 head,		"",		"HEAD") \
	_(argv_number,	 lineno,	"",		0) \
	_(argv_number,	 lineno_old,	"",		0) \
	_(argv_string,	 ref,		"HEAD",		"") \
	_(argv_string,	 remote,	"origin",	"") \
	_(argv_string,	 stash,		"",		"") \
	_(argv_string,	 status,	"",		"") \
	_(argv_string,	 tag,		"",		"") \
	_(argv_string,	 text,		"",		"") \
	_(argv_string,	 refname,	"",		"") \

#define ARGV_ENV_FIELDS(type, name, ifempty, initval)	type name;

struct argv_env {
	ARGV_ENV_INFO(ARGV_ENV_FIELDS)
	unsigned long goto_lineno;
	char goto_id[SIZEOF_REV];
	char search[SIZEOF_STR];
	char none[1];
};

extern struct argv_env argv_env;

bool argv_format(struct argv_env *argv_env, const char ***dst_argv, const char *src_argv[], bool first, bool file_filter);
char *argv_format_arg(struct argv_env *argv_env, const char *src_arg);

struct rev_flags {
	size_t search_offset;
	bool with_graph;
	bool with_reflog;
};

bool argv_parse_rev_flag(const char *arg, struct rev_flags *flags);

#endif
/* vim: set ts=8 sw=8 noexpandtab: */
