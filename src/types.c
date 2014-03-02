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

char *
enum_map_name(const char *name, size_t namelen)
{
	static char buf[SIZEOF_STR];
	int bufpos;

	for (bufpos = 0; bufpos <= namelen; bufpos++) {
		buf[bufpos] = ascii_tolower(name[bufpos]);
		if (buf[bufpos] == '_')
			buf[bufpos] = '-';
	}

	buf[bufpos] = 0;
	return buf;
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

DEFINE_ENUM_MAP(author, AUTHOR_ENUM);
DEFINE_ENUM_MAP(commit_order, COMMIT_ORDER_ENUM);
DEFINE_ENUM_MAP(date, DATE_ENUM);
DEFINE_ENUM_MAP(file_size, FILE_SIZE_ENUM);
DEFINE_ENUM_MAP(filename, FILENAME_ENUM);
DEFINE_ENUM_MAP(graphic, GRAPHIC_ENUM);
DEFINE_ENUM_MAP(ignore_space, IGNORE_SPACE_ENUM);
DEFINE_ENUM_MAP(vertical_split, VERTICAL_SPLIT_ENUM);

/* vim: set ts=8 sw=8 noexpandtab: */
