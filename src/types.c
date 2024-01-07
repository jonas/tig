/* Copyright (c) 2006-2024 Jonas Fonseca <jonas.fonseca@gmail.com>
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
enum_name_copy(char buf[], size_t bufsize, const char *name)
{
	int bufpos;

	for (bufpos = 0; name[bufpos] && bufpos < bufsize - 1; bufpos++) {
		buf[bufpos] = ascii_tolower(name[bufpos]);
		if (string_enum_sep(buf[bufpos]))
			buf[bufpos] = '-';
	}

	buf[bufpos] = 0;
	return bufpos < bufsize;
}

const char *
enum_name(const char *name)
{
	static char buf[SIZEOF_STR];

	return enum_name_copy(buf, sizeof(buf), name) ? buf : name;
}

bool
enum_name_prefixed(char buf[], size_t bufsize, const char *prefix, const char *name)
{
	char prefixed[SIZEOF_STR];

	if (*prefix) {
		if (!string_format(prefixed, "%s-%s", prefix, name))
			return false;
		name = prefixed;
	}

	return enum_name_copy(buf, bufsize, name);
}

const struct enum_map *
find_enum_map(const char *type)
{
	static struct {
		const char *type;
		const struct enum_map *map;
	} mappings[] = {
#define DEFINE_ENUM_MAPPING(name, macro) { #name, name##_map },
		ENUM_INFO(DEFINE_ENUM_MAPPING)
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(mappings); i++)
		if (!strcmp(type, mappings[i].type))
			return mappings[i].map;
	return NULL;
}

bool
map_enum_do(const struct enum_map_entry *map, size_t map_size, int *value, const char *name)
{
	size_t namelen = strlen(name);
	int i;

	for (i = 0; i < map_size; i++)
		if (enum_equals(map[i], name, namelen)) {
			*value = map[i].value;
			return true;
		}

	return false;
}

#define DEFINE_ENUM_MAPS(name, macro) DEFINE_ENUM_MAP(name, macro);
ENUM_INFO(DEFINE_ENUM_MAPS)

/* vim: set ts=8 sw=8 noexpandtab: */
