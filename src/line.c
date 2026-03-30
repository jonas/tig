/* Copyright (c) 2006-2026 Jonas Fonseca <jonas.fonseca@gmail.com>
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
#include "tig/ansi.h"
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
	else if (ref->type == REFERENCE_STASH)
		return LINE_MAIN_STASH;
	else if (ref->type == REFERENCE_NOTE)
		return LINE_MAIN_NOTE;
	else if (ref->type == REFERENCE_PREFETCH)
		return LINE_MAIN_PREFETCH;
	else if (ref->type == REFERENCE_OTHER)
		return LINE_MAIN_OTHER;
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
	char *no_color = getenv("NO_COLOR");
	struct line_rule query = { "default", STRING_SIZE("default") };
	struct line_rule *rule = find_line_rule(&query);
	int default_bg = rule ? rule->info.bg : COLOR_BLACK;
	int default_fg = rule ? rule->info.fg : COLOR_WHITE;
	enum line_type type;

	/* XXX: Even if the terminal does not support colors (e.g.
	 * TERM=dumb) init_colors() must ensure that the built-in rules
	 * have been initialized. This is done by the above call to
	 * find_line_rule(). */
	if (!has_colors() || (no_color != NULL && no_color[0] != '\0'))
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

/*
 * Dynamic color pair allocation for syntax highlighting.
 *
 * Maps arbitrary (fg, bg) ncurses color indices to color pair IDs,
 * allocating new pairs on demand via init_pair().
 */

#define DYN_PAIR_BUCKETS	256

struct dyn_pair_entry {
	int fg;
	int bg;
	int pair_id;
	struct dyn_pair_entry *next;
};

static struct dyn_pair_entry *dyn_pair_table[DYN_PAIR_BUCKETS];
static int dyn_pair_next_id;
static bool dyn_pair_initialized;

static unsigned int
dyn_pair_hash(int fg, int bg)
{
	unsigned int h = (unsigned int)(fg * 257 + bg);
	return h % DYN_PAIR_BUCKETS;
}

/*
 * Convert an RGB color to the nearest xterm-256 color index.
 * Uses the 6x6x6 color cube (indices 16-231) and grayscale ramp (232-255).
 */
static int
rgb_to_256(unsigned char r, unsigned char g, unsigned char b)
{
	int ri, gi, bi, ci;
	int gray, gray_idx;
	int cube_r, cube_g, cube_b;
	int cube_dist, gray_dist;

	/* Map to 6x6x6 cube */
	ri = (r < 48) ? 0 : (r < 115) ? 1 : (r - 35) / 40;
	gi = (g < 48) ? 0 : (g < 115) ? 1 : (g - 35) / 40;
	bi = (b < 48) ? 0 : (b < 115) ? 1 : (b - 35) / 40;
	ci = 16 + 36 * ri + 6 * gi + bi;

	/* Cube color values for comparison */
	cube_r = ri ? 55 + ri * 40 : 0;
	cube_g = gi ? 55 + gi * 40 : 0;
	cube_b = bi ? 55 + bi * 40 : 0;
	cube_dist = (r - cube_r) * (r - cube_r)
		  + (g - cube_g) * (g - cube_g)
		  + (b - cube_b) * (b - cube_b);

	/* Check grayscale ramp (232-255, values 8,18,28,...,238) */
	gray = (r + g + b) / 3;
	gray_idx = (gray < 4) ? 0 : (gray > 243) ? 23 : (gray - 4) / 10;
	gray = 8 + gray_idx * 10;
	gray_dist = (r - gray) * (r - gray)
		  + (g - gray) * (g - gray)
		  + (b - gray) * (b - gray);

	return (gray_dist < cube_dist) ? 232 + gray_idx : ci;
}

int
ansi_color_to_ncurses(const struct ansi_color *color)
{
	switch (color->type) {
	case ANSI_COLOR_DEFAULT:
		return -1;  /* COLOR_DEFAULT in ncurses */
	case ANSI_COLOR_BASIC:
		return color->index;  /* 0-7 maps directly to ncurses COLOR_* */
	case ANSI_COLOR_256:
		return color->index;
	case ANSI_COLOR_RGB:
		return rgb_to_256(color->rgb.r, color->rgb.g, color->rgb.b);
	}
	return -1;
}

int
get_dynamic_color_pair(const struct ansi_color *fg, const struct ansi_color *bg)
{
	int ncurses_fg = ansi_color_to_ncurses(fg);
	int ncurses_bg = ansi_color_to_ncurses(bg);
	unsigned int bucket = dyn_pair_hash(ncurses_fg, ncurses_bg);
	struct dyn_pair_entry *entry;

	if (!dyn_pair_initialized) {
		/* Start dynamic IDs after the static ones */
		dyn_pair_next_id = color_pairs;
		dyn_pair_initialized = true;
	}

	/* Look up in hash table */
	for (entry = dyn_pair_table[bucket]; entry; entry = entry->next) {
		if (entry->fg == ncurses_fg && entry->bg == ncurses_bg)
			return entry->pair_id;
	}

	/* Allocate a new pair if we haven't exhausted them */
	if (COLOR_ID(dyn_pair_next_id) >= COLOR_PAIRS)
		return 0;  /* fallback to default pair */

	entry = calloc(1, sizeof(*entry));
	if (!entry)
		return 0;

	entry->fg = ncurses_fg;
	entry->bg = ncurses_bg;
	entry->pair_id = dyn_pair_next_id++;
	entry->next = dyn_pair_table[bucket];
	dyn_pair_table[bucket] = entry;

	init_pair(COLOR_ID(entry->pair_id), ncurses_fg, ncurses_bg);
	return entry->pair_id;
}

/* vim: set ts=8 sw=8 noexpandtab: */
