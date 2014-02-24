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
#include "types.h"
#include "refs.h"
#include "line.h"
#include "util.h"

static struct line_info *line_info;
static size_t line_infos;

static struct line_info **color_pair;
static size_t color_pairs;

DEFINE_ALLOCATOR(realloc_line_info, struct line_info, 8)
DEFINE_ALLOCATOR(realloc_color_pair, struct line_info *, 8)

enum line_type
get_line_type(const char *line)
{
	int linelen = strlen(line);
	enum line_type type;

	for (type = 0; type < line_infos; type++)
		/* Case insensitive search matches Signed-off-by lines better. */
		if (line_info[type].linelen && linelen >= line_info[type].linelen &&
		    !strncasecmp(line_info[type].line, line, line_info[type].linelen))
			return type;

	return LINE_DEFAULT;
}

enum line_type
get_line_type_from_ref(const struct ref *ref)
{
	if (ref->head)
		return LINE_MAIN_HEAD;
	else if (ref->ltag)
		return LINE_MAIN_LOCAL_TAG;
	else if (ref->tag)
		return LINE_MAIN_TAG;
	else if (ref->tracked)
		return LINE_MAIN_TRACKED;
	else if (ref->remote)
		return LINE_MAIN_REMOTE;
	else if (ref->replace)
		return LINE_MAIN_REPLACE;

	return LINE_MAIN_REF;
}

struct line_info *
get_line_info(enum line_type type)
{
	assert(type < line_infos);
	return &line_info[type];
}

static struct line_info *
add_line_info(const char *name, size_t namelen, const char *line, size_t linelen)
{
	struct line_info *info = NULL;

	if (!realloc_line_info(&line_info, line_infos, 1))
		die("Failed to allocate line info");

	info = &line_info[line_infos++];
	info->name = name;
	info->namelen = namelen;
	info->line = line;
	info->linelen = linelen;

	return info;
}

#define ADD_LINE_INFO(type, line) \
	add_line_info(#type, STRING_SIZE(#type), (line), STRING_SIZE(line))

struct line_info *
find_line_info(const char *name, size_t namelen, bool line_only)
{
	enum line_type type;

	if (!line_infos) {
		LINE_INFO(ADD_LINE_INFO);
	}

	for (type = 0; type < line_infos; type++) {
		struct line_info *info = &line_info[type];

		if (!line_only && enum_equals(*info, name, namelen))
			return info;
		if (info->linelen && namelen >= info->linelen &&
		    !strncasecmp(info->line, name, info->linelen))
			return info;
	}

	return NULL;
}

struct line_info *
add_custom_color(const char *quoted_line)
{
	size_t linelen = strlen(quoted_line) - 2;
	struct line_info *info = find_line_info(quoted_line + 1, linelen, TRUE);
	char *line;

	if (info)
		return info;

	line = strndup(quoted_line + 1, linelen);
	if (!line)
		return NULL;

	info = add_line_info(line, linelen, line, linelen);
	if (!info)
		free(line);
	return info;
}

static void
init_line_info_color_pair(struct line_info *info, enum line_type type,
	int default_bg, int default_fg)
{
	int bg = info->bg == COLOR_DEFAULT ? default_bg : info->bg;
	int fg = info->fg == COLOR_DEFAULT ? default_fg : info->fg;
	int i;

	for (i = 0; i < color_pairs; i++) {
		if (color_pair[i]->fg == info->fg && color_pair[i]->bg == info->bg) {
			info->color_pair = i;
			return;
		}
	}

	if (!realloc_color_pair(&color_pair, color_pairs, 1))
		die("Failed to alloc color pair");

	color_pair[color_pairs] = info;
	info->color_pair = color_pairs++;
	init_pair(COLOR_ID(info->color_pair), fg, bg);
}

void
init_colors(void)
{
	int default_bg = line_info[LINE_DEFAULT].bg;
	int default_fg = line_info[LINE_DEFAULT].fg;
	enum line_type type;

	start_color();

	if (assume_default_colors(default_fg, default_bg) == ERR) {
		default_bg = COLOR_BLACK;
		default_fg = COLOR_WHITE;
	}

	for (type = 0; type < line_infos; type++) {
		struct line_info *info = &line_info[type];

		init_line_info_color_pair(info, type, default_bg, default_fg);
	}
}

/* vim: set ts=8 sw=8 noexpandtab: */
