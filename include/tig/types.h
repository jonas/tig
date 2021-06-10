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

#ifndef TIG_TYPES_H
#define TIG_TYPES_H

#include "tig/tig.h"

/*
 * Enumerations
 */

struct enum_map_entry {
	const char *name;
	int namelen;
	int value;
};

struct enum_map {
	const struct enum_map_entry *entries;
	const int size;
};

#define string_enum_sep(x) ((x) == '-' || (x) == '_')
int string_enum_compare(const char *str1, const char *str2, int len);

#define enum_equals(entry, str, len) \
	((entry).namelen == (len) && !string_enum_compare((entry).name, str, len))

#define enum_equals_static(str, name, namelen) \
	(namelen == STRING_SIZE(str) && !string_enum_compare(str, name, namelen))

#define enum_equals_prefix(entry, name_, namelen_) \
	((namelen_) > (entry).namelen && \
	 string_enum_sep((name_)[(entry).namelen]) && \
	 enum_equals(entry, name_, (entry).namelen))

const char *enum_name(const char *name);
bool enum_name_copy(char buf[], size_t bufsize, const char *name);
bool enum_name_prefixed(char buf[], size_t bufsize, const char *prefix, const char *name);

const struct enum_map *find_enum_map(const char *type);

bool map_enum_do(const struct enum_map_entry *map, size_t map_size, int *value, const char *name);

#define map_enum(attr, map, name) \
	map_enum_do(map, ARRAY_SIZE(map), attr, name)

#define ENUM_MAP_ENTRY(name, value) { name, STRING_SIZE(name), value }

#define ENUM_SYM_MACRO(prefix, name)	prefix##_##name
#define ENUM_MAP_MACRO(prefix, name)	ENUM_MAP_ENTRY(#name, ENUM_SYM_MACRO(prefix, name))

#define DEFINE_ENUM(name, info) \
	enum name { info(ENUM_SYM_MACRO) }; \
	extern const struct enum_map name##_map[];

#define DEFINE_ENUM_MAP(name, info) \
	const struct enum_map_entry name##_map_entries[] = { info(ENUM_MAP_MACRO) }; \
	const struct enum_map name##_map[] = { { name##_map_entries, ARRAY_SIZE(name##_map_entries) } }

#define VERTICAL_SPLIT_ENUM(_) \
	_(VERTICAL_SPLIT, HORIZONTAL), \
	_(VERTICAL_SPLIT, VERTICAL), \
	_(VERTICAL_SPLIT, AUTO)

#define GRAPHIC_ENUM(_) \
	_(GRAPHIC, ASCII), \
	_(GRAPHIC, DEFAULT), \
	_(GRAPHIC, UTF_8)

#define GRAPH_DISPLAY_ENUM(_) \
	_(GRAPH_DISPLAY, NO), \
	_(GRAPH_DISPLAY, V2), \
	_(GRAPH_DISPLAY, V1)

#define DATE_ENUM(_) \
	_(DATE, NO), \
	_(DATE, DEFAULT), \
	_(DATE, RELATIVE), \
	_(DATE, RELATIVE_COMPACT), \
	_(DATE, CUSTOM)

#define FILE_SIZE_ENUM(_) \
	_(FILE_SIZE, NO), \
	_(FILE_SIZE, DEFAULT), \
	_(FILE_SIZE, UNITS)

#define AUTHOR_ENUM(_) \
	_(AUTHOR, NO), \
	_(AUTHOR, FULL), \
	_(AUTHOR, ABBREVIATED), \
	_(AUTHOR, EMAIL), \
	_(AUTHOR, EMAIL_USER)

#define FILENAME_ENUM(_) \
	_(FILENAME, NO), \
	_(FILENAME, AUTO), \
	_(FILENAME, ALWAYS)

#define IGNORE_SPACE_ENUM(_) \
	_(IGNORE_SPACE, NO), \
	_(IGNORE_SPACE, ALL), \
	_(IGNORE_SPACE, SOME), \
	_(IGNORE_SPACE, AT_EOL)

#define IGNORE_CASE_ENUM(_) \
	_(IGNORE_CASE, NO), \
	_(IGNORE_CASE, YES), \
	_(IGNORE_CASE, SMART_CASE)

#define COMMIT_ORDER_ENUM(_) \
	_(COMMIT_ORDER, AUTO), \
	_(COMMIT_ORDER, DEFAULT), \
	_(COMMIT_ORDER, TOPO), \
	_(COMMIT_ORDER, DATE), \
	_(COMMIT_ORDER, AUTHOR_DATE), \
	_(COMMIT_ORDER, REVERSE)

#define VIEW_COLUMN_ENUM(_) \
	_(VIEW_COLUMN, AUTHOR), \
	_(VIEW_COLUMN, COMMIT_TITLE), \
	_(VIEW_COLUMN, DATE), \
	_(VIEW_COLUMN, FILE_NAME), \
	_(VIEW_COLUMN, FILE_SIZE), \
	_(VIEW_COLUMN, ID), \
	_(VIEW_COLUMN, LINE_NUMBER), \
	_(VIEW_COLUMN, MODE), \
	_(VIEW_COLUMN, REF), \
	_(VIEW_COLUMN, SECTION), \
	_(VIEW_COLUMN, STATUS), \
	_(VIEW_COLUMN, TEXT)

#define REFERENCE_ENUM(_) \
	_(REFERENCE, HEAD), \
	_(REFERENCE, BRANCH), \
	_(REFERENCE, TRACKED_REMOTE), \
	_(REFERENCE, REMOTE), \
	_(REFERENCE, TAG), \
	_(REFERENCE, LOCAL_TAG), \
	_(REFERENCE, REPLACE)

#define STATUS_LABEL_ENUM(_) \
	_(STATUS_LABEL, NO), \
	_(STATUS_LABEL, SHORT), \
	_(STATUS_LABEL, LONG)

#define REFRESH_MODE_ENUM(_) \
	_(REFRESH_MODE, MANUAL), \
	_(REFRESH_MODE, AUTO), \
	_(REFRESH_MODE, AFTER_COMMAND), \
	_(REFRESH_MODE, PERIODIC),

#define DIFF_COLUMN_HIGHLIGHT_ENUM(_) \
	_(DIFF_COLUMN_HIGHLIGHT, NO), \
	_(DIFF_COLUMN_HIGHLIGHT, ALL), \
	_(DIFF_COLUMN_HIGHLIGHT, ONLY_EMPTY)

#define ENUM_INFO(_) \
	_(author, AUTHOR_ENUM) \
	_(commit_order, COMMIT_ORDER_ENUM) \
	_(date, DATE_ENUM) \
	_(file_size, FILE_SIZE_ENUM) \
	_(filename, FILENAME_ENUM) \
	_(graphic, GRAPHIC_ENUM) \
	_(graph_display, GRAPH_DISPLAY_ENUM) \
	_(ignore_case, IGNORE_CASE_ENUM) \
	_(ignore_space, IGNORE_SPACE_ENUM) \
	_(vertical_split, VERTICAL_SPLIT_ENUM) \
	_(view_column_type, VIEW_COLUMN_ENUM) \
	_(reference_type, REFERENCE_ENUM) \
	_(refresh_mode, REFRESH_MODE_ENUM) \
	_(status_label, STATUS_LABEL_ENUM) \
	_(diff_column_highlight, DIFF_COLUMN_HIGHLIGHT_ENUM) \

#define DEFINE_ENUMS(name, macro) DEFINE_ENUM(name, macro)
ENUM_INFO(DEFINE_ENUMS)

#endif
/* vim: set ts=8 sw=8 noexpandtab: */
