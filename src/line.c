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
#include "tig/refdb.h"
#include "tig/line.h"
#include "tig/util.h"

static struct line_rule *line_rule;
static size_t line_rules;

static struct line_info **color_pair;
static size_t color_pairs;

DEFINE_ALLOCATOR(realloc_line_rule, struct line_rule, 8)
DEFINE_ALLOCATOR(realloc_color_pair, struct line_info *, 8)

enum line_type
get_line_type(const char *line)
{
	int linelen = strlen(line);
	enum line_type type;

	for (type = 0; type < line_rules; type++) {
		struct line_rule *rule = &line_rule[type];

		if (rule->regex && !regexec(rule->regex, line, 0, NULL, 0))
			return type;

		/* Case insensitive search matches Signed-off-by lines better. */
		if (rule->linelen && linelen >= rule->linelen &&
		    !strncasecmp(rule->line, line, rule->linelen))
			return type;
	}

	return LINE_DEFAULT;
}

enum line_type
get_line_type_from_ref(const struct ref *ref)
{
	if (ref->type == REFERENCE_HEAD)
		return LINE_MAIN_HEAD;
	else if (ref->type == REFERENCE_LOCAL_TAG)
		return LINE_MAIN_LOCAL_TAG;
	else if (ref->type == REFERENCE_TAG)
		return LINE_MAIN_TAG;
	else if (ref->type == REFERENCE_TRACKED_REMOTE)
		return LINE_MAIN_TRACKED;
	else if (ref->type == REFERENCE_REMOTE)
		return LINE_MAIN_REMOTE;
	else if (ref->type == REFERENCE_REPLACE)
		return LINE_MAIN_REPLACE;

	return LINE_MAIN_REF;
}

const char *
get_line_type_name(enum line_type type)
{
	assert(0 <= type && type < line_rules);
	return line_rule[type].name;
}

struct line_info *
get_line_info(const char *prefix, enum line_type type)
{
	struct line_info *info;
	struct line_rule *rule;

	assert(0 <= type && type < line_rules);
	rule = &line_rule[type];
	for (info = &rule->info; info; info = info->next) {
		if (prefix && info->prefix == prefix)
			return info;
		if (!prefix && !info->prefix)
			return info;
	}

	return &rule->info;
}

static struct line_info *
init_line_info(const char *prefix, const char *name, size_t namelen, const char *line, size_t linelen, regex_t *regex)
{
	struct line_rule *rule;

	if (!realloc_line_rule(&line_rule, line_rules, 1))
		die("Failed to allocate line info");

	rule = &line_rule[line_rules++];
	rule->name = name;
	rule->namelen = namelen;
	rule->line = line;
	rule->linelen = linelen;
	rule->regex = regex;

	rule->info.prefix = prefix;
	rule->info.fg = COLOR_DEFAULT;
	rule->info.bg = COLOR_DEFAULT;

	return &rule->info;
}

#define INIT_BUILTIN_LINE_INFO(type, line) \
	init_line_info(NULL, #type, STRING_SIZE(#type), (line), STRING_SIZE(line), NULL)

static struct line_rule *
find_line_rule(struct line_rule *query)
{
	enum line_type type;

	if (!line_rules) {
		LINE_INFO(INIT_BUILTIN_LINE_INFO);
	}

	for (type = 0; type < line_rules; type++) {
		struct line_rule *rule = &line_rule[type];

		if (query->namelen && enum_equals(*rule, query->name, query->namelen))
			return rule;

		if (query->linelen && query->linelen == rule->linelen &&
		    !strncasecmp(rule->line, query->line, rule->linelen))
			return rule;
	}

	return NULL;
}

struct line_info *
add_line_rule(const char *prefix, struct line_rule *query)
{
	struct line_rule *rule = find_line_rule(query);
	struct line_info *info, *last;

	if (!rule) {
		if (query->name)
			return NULL;

		return init_line_info(prefix, "", 0, query->line, query->linelen, query->regex);
	}

	/* When a rule already exists and we are just adding view-specific
	 * colors, query->line and query->regex can be freed. */
	free((void *) query->line);
	if (query->regex) {
		regfree(query->regex);
		free(query->regex);
	}

	for (info = &rule->info; info; last = info, info = info->next)
		if (info->prefix == prefix)
			return info;

	info = calloc(1, sizeof(*info));
	if (info)
		info->prefix = prefix;
	last->next = info;
	return info;
}

bool
foreach_line_rule(line_rule_visitor_fn visitor, void *data)
{
	enum line_type type;

	for (type = 0; type < line_rules; type++) {
		struct line_rule *rule = &line_rule[type];

		if (!visitor(data, rule))
			return false;
	}

	return true;
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
		die("Failed to allocate color pair");

	color_pair[color_pairs] = info;
	info->color_pair = color_pairs++;
	init_pair(COLOR_ID(info->color_pair), fg, bg);
}

void
init_colors(void)
{
	struct line_rule query = { "default", STRING_SIZE("default") };
	struct line_rule *rule = find_line_rule(&query);
	int default_bg = rule ? rule->info.bg : COLOR_BLACK;
	int default_fg = rule ? rule->info.fg : COLOR_WHITE;
	enum line_type type;

	/* XXX: Even if the terminal does not support colors (e.g.
	 * TERM=dumb) init_colors() must ensure that the built-in rules
	 * have been initialized. This is done by the above call to
	 * find_line_rule(). */
	if (!has_colors())
		return;

	start_color();

	if (assume_default_colors(default_fg, default_bg) == ERR) {
		default_bg = COLOR_BLACK;
		default_fg = COLOR_WHITE;
	}

	for (type = 0; type < line_rules; type++) {
		struct line_rule *rule = &line_rule[type];
		struct line_info *info;

		for (info = &rule->info; info; info = info->next) {
			init_line_info_color_pair(info, type, default_bg, default_fg);
		}
	}
}

/* vim: set ts=8 sw=8 noexpandtab: */
