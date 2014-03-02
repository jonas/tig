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
#include "tig/util.h"
#include "tig/parse.h"

size_t
parse_size(const char *text, int *max_digits)
{
	size_t size = 0;
	int digits = 0;

	while (*text == ' ')
		text++;

	while (isdigit(*text)) {
		size = (size * 10) + (*text++ - '0');
		digits++;
	}

	if (digits > *max_digits)
		*max_digits = digits;

	return size;
}

/*
 * Parsing of ident lines.
 */

static void
parse_timesec(struct time *time, const char *sec)
{
	time->sec = (time_t) atol(sec);
}

static void
parse_timezone(struct time *time, const char *zone)
{
	long tz;

	tz  = ('0' - zone[1]) * 60 * 60 * 10;
	tz += ('0' - zone[2]) * 60 * 60;
	tz += ('0' - zone[3]) * 60 * 10;
	tz += ('0' - zone[4]) * 60;

	if (zone[0] == '-')
		tz = -tz;

	time->tz = tz;
	time->sec -= tz;
}

void
parse_author_line(char *ident, const struct ident **author, struct time *time)
{
	char *nameend = strchr(ident, '<');
	char *emailend = strchr(ident, '>');
	const char *name, *email = "";

	if (nameend && emailend)
		*nameend = *emailend = 0;
	name = chomp_string(ident);
	if (nameend)
		email = chomp_string(nameend + 1);
	if (!*name)
		name = *email ? email : unknown_ident.name;
	if (!*email)
		email = *name ? name : unknown_ident.email;

	*author = get_author(name, email);

	/* Parse epoch and timezone */
	if (time && emailend && emailend[1] == ' ') {
		char *secs = emailend + 2;
		char *zone = strchr(secs, ' ');

		parse_timesec(time, secs);

		if (zone && strlen(zone) == STRING_SIZE(" +0700"))
			parse_timezone(time, zone + 1);
	}
}

/*
 * Blame.
 */

static bool
parse_number(const char **posref, size_t *number, size_t min, size_t max)
{
	const char *pos = *posref;

	*posref = NULL;
	pos = strchr(pos + 1, ' ');
	if (!pos || !isdigit(pos[1]))
		return FALSE;
	*number = atoi(pos + 1);
	if (*number < min || *number > max)
		return FALSE;

	*posref = pos;
	return TRUE;
}

bool
parse_blame_header(struct blame_header *header, const char *text, size_t max_lineno)
{
	const char *pos = text + SIZEOF_REV - 2;

	if (strlen(text) <= SIZEOF_REV || pos[1] != ' ')
		return FALSE;

	string_ncopy(header->id, text, SIZEOF_REV);

	if (!parse_number(&pos, &header->orig_lineno, 1, 9999999) ||
	    !parse_number(&pos, &header->lineno, 1, max_lineno) ||
	    !parse_number(&pos, &header->group, 1, max_lineno - header->lineno + 1))
		return FALSE;

	return TRUE;
}

static bool
match_blame_header(const char *name, char **line)
{
	size_t namelen = strlen(name);
	bool matched = !strncmp(name, *line, namelen);

	if (matched)
		*line += namelen;

	return matched;
}

bool
parse_blame_info(struct blame_commit *commit, char *line)
{
	if (match_blame_header("author ", &line)) {
		parse_author_line(line, &commit->author, NULL);

	} else if (match_blame_header("author-time ", &line)) {
		parse_timesec(&commit->time, line);

	} else if (match_blame_header("author-tz ", &line)) {
		parse_timezone(&commit->time, line);

	} else if (match_blame_header("summary ", &line)) {
		string_ncopy(commit->title, line, strlen(line));

	} else if (match_blame_header("previous ", &line)) {
		if (strlen(line) <= SIZEOF_REV)
			return FALSE;
		string_copy_rev(commit->parent_id, line);
		line += SIZEOF_REV;
		commit->parent_filename = get_path(line);
		if (!commit->parent_filename)
			return TRUE;

	} else if (match_blame_header("filename ", &line)) {
		commit->filename = get_path(line);
		return TRUE;
	}

	return FALSE;
}

/*
 * Diff.
 */

static bool
parse_ulong(const char **pos_ptr, unsigned long *value, const char *skip)
{
	const char *start = *pos_ptr;
	char *end;

	if (!isdigit(*start))
		return 0;

	*value = strtoul(start, &end, 10);
	if (end == start)
		return FALSE;

	start = end;
	while (skip && *start && strchr(skip, *start))
		start++;
	*pos_ptr = start;
	return TRUE;
}

bool
parse_chunk_header(struct chunk_header *header, const char *line)
{
	memset(header, 0, sizeof(*header));

	if (!prefixcmp(line, "@@ -"))
		line += STRING_SIZE("@@ -");
	else if (!prefixcmp(line, "@@@ -") &&
		 (line = strchr(line + STRING_SIZE("@@@ -"), '-')))
		line += 1;
	else
		return FALSE;


	return  parse_ulong(&line, &header->old.position, ",") &&
		parse_ulong(&line, &header->old.lines, " +") &&
		parse_ulong(&line, &header->new.position, ",") &&
		parse_ulong(&line, &header->new.lines, NULL);
}

bool
parse_chunk_lineno(unsigned long *lineno, const char *chunk, int marker)
{
	struct chunk_header chunk_header;

	*lineno = 0;

	if (!parse_chunk_header(&chunk_header, chunk))
		return FALSE;

	*lineno = marker == '-' ? chunk_header.old.position : chunk_header.new.position;
	return TRUE;
}

/*
 * Caches.
 */

DEFINE_ALLOCATOR(realloc_paths, const char *, 256)

/* Small cache to reduce memory consumption. It uses binary search to
 * lookup or find place to position new entries. No entries are ever
 * freed. */
const char *
get_path(const char *path)
{
	static const char **paths;
	static size_t paths_size;
	int from = 0, to = paths_size - 1;
	char *entry;

	while (from <= to) {
		size_t pos = (to + from) / 2;
		int cmp = strcmp(path, paths[pos]);

		if (!cmp)
			return paths[pos];

		if (cmp < 0)
			to = pos - 1;
		else
			from = pos + 1;
	}

	if (!realloc_paths(&paths, paths_size, 1))
		return NULL;
	entry = strdup(path);
	if (!entry)
		return NULL;

	memmove(paths + from + 1, paths + from, (paths_size - from) * sizeof(*paths));
	paths[from] = entry;
	paths_size++;

	return entry;
}

DEFINE_ALLOCATOR(realloc_authors, struct ident *, 256)

/* Small author cache to reduce memory consumption. It uses binary
 * search to lookup or find place to position new entries. No entries
 * are ever freed. */
struct ident *
get_author(const char *name, const char *email)
{
	static struct ident **authors;
	static size_t authors_size;
	int from = 0, to = authors_size - 1;
	struct ident *ident;

	while (from <= to) {
		size_t pos = (to + from) / 2;
		int cmp = strcmp(name, authors[pos]->name);

		if (!cmp)
			return authors[pos];

		if (cmp < 0)
			to = pos - 1;
		else
			from = pos + 1;
	}

	if (!realloc_authors(&authors, authors_size, 1))
		return NULL;
	ident = calloc(1, sizeof(*ident));
	if (!ident)
		return NULL;
	ident->name = strdup(name);
	ident->email = strdup(email);
	if (!ident->name || !ident->email) {
		free((void *) ident->name);
		free(ident);
		return NULL;
	}

	memmove(authors + from + 1, authors + from, (authors_size - from) * sizeof(*authors));
	authors[from] = ident;
	authors_size++;

	return ident;
}

/* vim: set ts=8 sw=8 noexpandtab: */
