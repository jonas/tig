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

#ifndef TIG_LINE_H
#define TIG_LINE_H

#include "tig.h"
struct ref;

/*
 * Line-oriented content detection.
 */

#define LINE_INFO(_) \
	_(DIFF_HEADER,  	"diff --"), \
	_(DIFF_CHUNK,   	"@@"), \
	_(DIFF_ADD,		"+"), \
	_(DIFF_ADD2,		" +"), \
	_(DIFF_DEL,		"-"), \
	_(DIFF_DEL2,		" -"), \
	_(DIFF_INDEX,		"index "), \
	_(DIFF_OLDMODE,		"old file mode "), \
	_(DIFF_NEWMODE,		"new file mode "), \
	_(DIFF_SIMILARITY,	"similarity "), \
	_(PP_MERGE,		"Merge: "), \
	_(PP_REFS,		"Refs: "), \
	_(PP_REFLOG,		"Reflog: "), \
	_(PP_REFLOGMSG,		"Reflog message: "), \
	_(COMMIT,		"commit "), \
	_(PARENT,		"parent "), \
	_(TREE,			"tree "), \
	_(AUTHOR,		"author "), \
	_(COMMITTER,		"committer "), \
	_(DEFAULT,		""), \
	_(CURSOR,		""), \
	_(STATUS,		""), \
	_(DELIMITER,		""), \
	_(DATE,      		""), \
	_(MODE,      		""), \
	_(ID,			""), \
	_(OVERFLOW,		""), \
	_(FILENAME,  		""), \
	_(FILE_SIZE, 		""), \
	_(LINE_NUMBER,		""), \
	_(TITLE_BLUR,		""), \
	_(TITLE_FOCUS,		""), \
	_(MAIN_COMMIT,		""), \
	_(MAIN_TAG,		""), \
	_(MAIN_LOCAL_TAG,	""), \
	_(MAIN_REMOTE,		""), \
	_(MAIN_REPLACE,		""), \
	_(MAIN_TRACKED,		""), \
	_(MAIN_REF,		""), \
	_(MAIN_HEAD,		""), \
	_(MAIN_REVGRAPH,	""), \
	_(TREE_HEAD,		""), \
	_(TREE_DIR,		""), \
	_(TREE_FILE,		""), \
	_(STAT_HEAD,		""), \
	_(STAT_SECTION,		""), \
	_(STAT_NONE,		""), \
	_(STAT_STAGED,		""), \
	_(STAT_UNSTAGED,	""), \
	_(STAT_UNTRACKED,	""), \
	_(HELP_KEYMAP,		""), \
	_(HELP_GROUP,		""), \
	_(DIFF_STAT,		""), \
	_(PALETTE_0,		""), \
	_(PALETTE_1,		""), \
	_(PALETTE_2,		""), \
	_(PALETTE_3,		""), \
	_(PALETTE_4,		""), \
	_(PALETTE_5,		""), \
	_(PALETTE_6,		""), \
	_(GRAPH_COMMIT,		"")

enum line_type {
#define DEFINE_LINE_ENUM(type, line) LINE_##type
	LINE_INFO(DEFINE_LINE_ENUM),
	LINE_NONE
};

struct line_info {
	const char *name;	/* Option name. */
	int namelen;		/* Size of option name. */
	const char *line;	/* The start of line to match. */
	int linelen;		/* Size of string to match. */
	int fg, bg, attr;	/* Color and text attributes for the lines. */
	int color_pair;
};

enum line_type get_line_type(const char *line);
enum line_type get_line_type_from_ref(const struct ref *ref);

struct line_info *get_line_info(enum line_type type);
struct line_info *find_line_info(const char *name, size_t namelen, bool line_only);
struct line_info *add_custom_color(const char *quoted_line);
void init_colors(void);

/* Color IDs must be 1 or higher. [GH #15] */
#define COLOR_ID(line_type)		((line_type) + 1)

static inline int
get_line_color(enum line_type type)
{
	return COLOR_ID(get_line_info(type)->color_pair);
}

static inline int
get_line_attr(enum line_type type)
{
	struct line_info *info = get_line_info(type);

	return COLOR_PAIR(COLOR_ID(info->color_pair)) | info->attr;
}

#endif
/* vim: set ts=8 sw=8 noexpandtab: */
