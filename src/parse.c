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
#include "tig/parse.h"
#include "tig/map.h"

size_t
parse_size(const char *text)
{
	size_t size = 0;

	while (*text == ' ')
		text++;

	while (isdigit(*text))
		size = (size * 10) + (*text++ - '0');

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
	name = string_trim(ident);
	if (nameend)
		email = string_trim(nameend + 1);
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
		return false;
	*number = atoi(pos + 1);
	if (*number < min || *number > max)
		return false;

	*posref = pos;
	return true;
}

bool
parse_blame_header(struct blame_header *header, const char *text, size_t max_lineno)
{
	const char *pos = text + SIZEOF_REV - 2;

	if (strlen(text) <= SIZEOF_REV || pos[1] != ' ')
		return false;

	string_ncopy(header->id, text, SIZEOF_REV);

	if (!parse_number(&pos, &header->orig_lineno, 1, 9999999) ||
	    !parse_number(&pos, &header->lineno, 1, max_lineno) ||
	    !parse_number(&pos, &header->group, 1, max_lineno - header->lineno + 1))
		return false;

	return true;
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
parse_blame_info(struct blame_commit *commit, char author[SIZEOF_STR], char *line)
{
	if (match_blame_header("author ", &line)) {
		string_ncopy_do(author, SIZEOF_STR, line, strlen(line));

	} else if (match_blame_header("author-mail ", &line)) {
		char *end = strchr(line, '>');

		if (end)
			*end = 0;
		if (*line == '<')
			line++;
		commit->author = get_author(author, line);
		author[0] = 0;

	} else if (match_blame_header("author-time ", &line)) {
		parse_timesec(&commit->time, line);

	} else if (match_blame_header("author-tz ", &line)) {
		parse_timezone(&commit->time, line);

	} else if (match_blame_header("summary ", &line)) {
		string_ncopy(commit->title, line, strlen(line));

	} else if (match_blame_header("previous ", &line)) {
		if (strlen(line) <= SIZEOF_REV)
			return false;
		string_copy_rev(commit->parent_id, line);
		line += SIZEOF_REV;
		commit->parent_filename = get_path(line);
		if (!commit->parent_filename)
			return true;

	} else if (match_blame_header("filename ", &line)) {
		commit->filename = get_path(line);
		return true;
	}

	return false;
}

/*
 * Diff.
 */

static bool
parse_ulong(const char **pos_ptr, unsigned long *value, char skip, bool optional)
{
	const char *start = *pos_ptr;
	char *end;

	if (*start != skip)
		return optional;

	start++;
	*value = strtoul(start, &end, 10);
	if (end == start)
		return false;

	while (isspace(*end))
		end++;
	*pos_ptr = end;
	return true;
}

bool
parse_chunk_header(struct chunk_header *header, const char *line)
{
	memset(header, 0, sizeof(*header));
	header->new.lines = header->old.lines = 1;

	if (!prefixcmp(line, "@@ -"))
		line += STRING_SIZE("@@ -") - 1;
	else if (!prefixcmp(line, "@@@") &&
		 (line = strstr(line, " @@@")))
		while (*line != '-')
			line--;
	else
		return false;

	return  parse_ulong(&line, &header->old.position, '-', false) &&
		parse_ulong(&line, &header->old.lines, ',', true) &&
		parse_ulong(&line, &header->new.position, '+', false) &&
		parse_ulong(&line, &header->new.lines, ',', false);
}

bool
parse_chunk_lineno(unsigned long *lineno, const char *chunk, int marker)
{
	struct chunk_header chunk_header;

	*lineno = 0;

	if (!parse_chunk_header(&chunk_header, chunk))
		return false;

	*lineno = marker == '-' ? chunk_header.old.position : chunk_header.new.position;
	return true;
}

/*
 * Caches.
 */

struct path_entry {
	char path[1];
};

DEFINE_STRING_MAP(path_cache, struct path_entry *, path, 32)

/* Small cache to reduce memory consumption. No entries are ever
 * freed. */
const char *
get_path(const char *path)
{
	struct path_entry *entry = string_map_get(&path_cache, path);

	if (!entry) {
		entry = calloc(1, sizeof(*entry) + strlen(path));
		if (!entry || !string_map_put(&path_cache, path, entry)) {
			free(entry);
			return NULL;
		}
		strcpy(entry->path, path);
	}

	return entry->path;
}

DEFINE_STRING_MAP(author_cache, const struct ident *, key, 32)

/* Small author cache to reduce memory consumption. No entries
 * are ever freed. */
struct ident *
get_author(const char *name, const char *email)
{
	char key[SIZEOF_STR + SIZEOF_STR];
	struct ident *ident;

	string_format(key, "%s%s", email, name);

	ident = string_map_get(&author_cache, key);
	if (ident)
		return ident;

	ident = calloc(1, sizeof(*ident));
	if (!ident)
		return NULL;
	ident->key = strdup(key);
	ident->name = strdup(name);
	ident->email = strdup(email);
	if (!ident->key || !ident->name || !ident->email ||
	    !string_map_put(&author_cache, key, ident)) {
		free((void *) ident->key);
		free((void *) ident->name);
		free((void *) ident->email);
		free(ident);
		return NULL;
	}

	return ident;
}

/* vim: set ts=8 sw=8 noexpandtab: */
