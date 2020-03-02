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

#ifndef TIG_LINE_H
#define TIG_LINE_H

#include "tig/tig.h"
struct ref;

/*
 * Line-oriented content detection.
 */

#define LINE_INFO(_) \
	_(DIFF_HEADER,		"diff --"), \
	_(DIFF_DEL_FILE,	"--- "), \
	_(DIFF_ADD_FILE,	"+++ "), \
	_(DIFF_START,		"---"), \
	_(DIFF_CHUNK,		"@@"), \
	_(DIFF_ADD,		"+"), \
	_(DIFF_ADD2,		" +"), \
	_(DIFF_DEL,		"-"), \
	_(DIFF_DEL2,		" -"), \
	_(DIFF_INDEX,		"index "), \
	_(DIFF_OLDMODE,		"old file mode "), \
	_(DIFF_NEWMODE,		"new file mode "), \
	_(DIFF_DELMODE,		"deleted file mode "), \
	_(DIFF_SIMILARITY,	"similarity "), \
	_(DIFF_NO_NEWLINE,	"\\ No newline at end of file"), \
	_(DIFF_ADD_HIGHLIGHT,	""), \
	_(DIFF_DEL_HIGHLIGHT,	""), \
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
	_(DATE,			""), \
	_(MODE,			""), \
	_(ID,			""), \
	_(OVERFLOW,		""), \
	_(DIRECTORY,		""), \
	_(FILE,			""), \
	_(FILE_SIZE,		""), \
	_(LINE_NUMBER,		""), \
	_(TITLE_BLUR,		""), \
	_(TITLE_FOCUS,		""), \
	_(HEADER,		""), \
	_(SECTION,		""), \
	_(MAIN_COMMIT,		""), \
	_(MAIN_ANNOTATED,	""), \
	_(MAIN_TAG,		""), \
	_(MAIN_LOCAL_TAG,	""), \
	_(MAIN_REMOTE,		""), \
	_(MAIN_REPLACE,		""), \
	_(MAIN_TRACKED,		""), \
	_(MAIN_REF,		""), \
	_(MAIN_HEAD,		""), \
	_(STAT_NONE,		""), \
	_(STAT_STAGED,		""), \
	_(STAT_UNSTAGED,	""), \
	_(STAT_UNTRACKED,	""), \
	_(HELP_GROUP,		""), \
	_(HELP_ACTION,		""), \
	_(DIFF_STAT,		""), \
	_(PALETTE_0,		""), \
	_(PALETTE_1,		""), \
	_(PALETTE_2,		""), \
	_(PALETTE_3,		""), \
	_(PALETTE_4,		""), \
	_(PALETTE_5,		""), \
	_(PALETTE_6,		""), \
	_(PALETTE_7,		""), \
	_(PALETTE_8,		""), \
	_(PALETTE_9,		""), \
	_(PALETTE_10,		""), \
	_(PALETTE_11,		""), \
	_(PALETTE_12,		""), \
	_(PALETTE_13,		""), \
	_(GRAPH_COMMIT,		""), \
	_(SEARCH_RESULT,	"")

enum line_type {
#define DEFINE_LINE_ENUM(type, line) LINE_##type
	LINE_INFO(DEFINE_LINE_ENUM),
	LINE_NONE
};

struct line_info {
	struct line_info *next;	/* List of line info matching this line type. */
	const char *prefix;	/* View (or keymap) name. */
	int fg, bg, attr;	/* Color and text attributes for the lines. */
	int color_pair;
};

struct line_rule {
	const char *name;	/* Option name. */
	int namelen;		/* Size of option name. */
	const char *line;	/* The start of line to match. */
	int linelen;		/* Size of string to match. */
	struct line_info info;	/* List of line info matching this rule. */
};

enum line_type get_line_type(const char *line);
enum line_type get_line_type_from_ref(const struct ref *ref);

const char *get_line_type_name(enum line_type type);

struct line_info *get_line_info(const char *prefix, enum line_type type);
struct line_info *add_line_rule(const char *prefix, struct line_rule *rule);
void init_colors(void);

typedef bool (*line_rule_visitor_fn)(void *data, const struct line_rule *rule);
bool foreach_line_rule(line_rule_visitor_fn fn, void *data);

/* Color IDs must be 1 or higher. [GH #15] */
#define COLOR_ID(line_type)		((line_type) + 1)

static inline int
get_line_color(const char *prefix, enum line_type type)
{
	return COLOR_ID(get_line_info(prefix, type)->color_pair);
}

static inline int
get_line_attr(const char *prefix, enum line_type type)
{
	struct line_info *info = get_line_info(prefix, type);
	return COLOR_PAIR(COLOR_ID(info->color_pair)) | info->attr;
}

#endif
/* vim: set ts=8 sw=8 noexpandtab: */
