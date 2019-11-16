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

#ifndef TIG_PARSE_H
#define TIG_PARSE_H

#include "tig/tig.h"
#include "tig/util.h"

struct chunk_header_position {
	unsigned long position;
	unsigned long lines;
};

struct chunk_header {
	struct chunk_header_position old;
	struct chunk_header_position new;
};

bool parse_chunk_header(struct chunk_header *header, const char *line);
bool parse_chunk_lineno(unsigned long *lineno, const char *chunk, int marker);

struct blame_commit {
	char id[SIZEOF_REV];		/* SHA1 ID. */
	char title[128];		/* First line of the commit message. */
	const struct ident *author;	/* Author of the commit. */
	struct time time;		/* Date from the author ident. */
	const char *filename;		/* Name of file. */
	char parent_id[SIZEOF_REV];	/* Parent/previous SHA1 ID. */
	const char *parent_filename;	/* Parent/previous name of file. */
};

struct blame_header {
	char id[SIZEOF_REV];		/* SHA1 ID. */
	size_t orig_lineno;
	size_t lineno;
	size_t group;
};

bool parse_blame_header(struct blame_header *header, const char *text, size_t max_lineno);
bool parse_blame_info(struct blame_commit *commit, char author[SIZEOF_STR], char *line);

/* Parse author lines where the name may be empty:
 *	author  <email@address.tld> 1138474660 +0100
 */
void parse_author_line(char *ident, const struct ident **author, struct time *time);

size_t parse_size(const char *text);

/*
 * Caches.
 */
const char *get_path(const char *path);
struct ident *get_author(const char *name, const char *email);

static inline int
chunk_header_marker_length(const char *data)
{
	int len = 0;
	for (; *data == '@'; data++)
		len++;
	return len;
}

#endif
/* vim: set ts=8 sw=8 noexpandtab: */
