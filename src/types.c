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

/*
 * Enumerations
 */

int
string_enum_compare(const char *str1, const char *str2, int len)
{
	size_t i;

#define string_enum_sep(x) ((x) == '-' || (x) == '_')

	/* Diff-Header == DIFF_HEADER */
	for (i = 0; i < len; i++) {
		if (ascii_toupper(str1[i]) == ascii_toupper(str2[i]))
			continue;

		if (string_enum_sep(str1[i]) &&
		    string_enum_sep(str2[i]))
			continue;

		return str1[i] - str2[i];
	}

	return 0;
}

bool
enum_name_ncopy(char *buf, size_t bufsize, const char *name, size_t namelen)
{
	int bufpos;

	for (bufpos = 0; bufpos <= namelen && bufpos < bufsize - 1; bufpos++) {
		buf[bufpos] = ascii_tolower(name[bufpos]);
		if (buf[bufpos] == '_')
			buf[bufpos] = '-';
	}

	buf[bufpos] = 0;
	return bufpos == namelen + 1;
}

const char *
enum_name_static(const char *name, size_t namelen)
{
	static char buf[SIZEOF_STR];

	return enum_name_copy(buf, name, namelen) ? buf : name;
}


bool
map_enum_do(const struct enum_map_entry *map, size_t map_size, int *value, const char *name)
{
	size_t namelen = strlen(name);
	int i;

	for (i = 0; i < map_size; i++)
		if (enum_equals(map[i], name, namelen)) {
			*value = map[i].value;
			return TRUE;
		}

	return FALSE;
}

#define DEFINE_ENUM_MAPS(name, macro) DEFINE_ENUM_MAP(name, macro);
ENUM_INFO(DEFINE_ENUM_MAPS);

/* vim: set ts=8 sw=8 noexpandtab: */
