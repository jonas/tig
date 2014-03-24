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

#ifndef TIG_VIEW_H
#define TIG_VIEW_H

#include "tig/tig.h"
#include "tig/argv.h"
#include "tig/io.h"
#include "tig/line.h"
#include "tig/keys.h"

struct view_ops;

struct line {
	enum line_type type;
	unsigned int lineno:24;

	/* State flags */
	unsigned int selected:1;
	unsigned int dirty:1;
	unsigned int cleareol:1;
	unsigned int wrapped:1;

	unsigned int user_flags:6;
	void *data;		/* User data */
};

enum view_flag {
	VIEW_NO_FLAGS = 0,
	VIEW_ALWAYS_LINENO	= 1 << 0,
	VIEW_CUSTOM_STATUS	= 1 << 1,
	VIEW_ADD_DESCRIBE_REF	= 1 << 2,
	VIEW_ADD_PAGER_REFS	= 1 << 3,
	VIEW_OPEN_DIFF		= 1 << 4,
	VIEW_NO_REF		= 1 << 5,
	VIEW_NO_GIT_DIR		= 1 << 6,
	VIEW_DIFF_LIKE		= 1 << 7,
	VIEW_BLAME_LIKE		= 1 << 8,
	VIEW_SEND_CHILD_ENTER	= 1 << 9,
	VIEW_FILE_FILTER	= 1 << 10,
	VIEW_LOG_LIKE		= 1 << 11,
	VIEW_STATUS_LIKE	= 1 << 12,
	VIEW_REFRESH		= 1 << 13,
	VIEW_CUSTOM_DIGITS	= 1 << 14,

	VIEW_RESET_DISPLAY	= 1 << 31,
};

#define view_has_flags(view, flag)	((view)->ops->flags & (flag))

struct position {
	unsigned long offset;	/* Offset of the window top */
	unsigned long col;	/* Offset from the window side. */
	unsigned long lineno;	/* Current line number */
};

struct sort_state {
	size_t current;
	bool reverse;
};

struct column_info {
	int width;
	unsigned long option;
};

struct view {
	const char *name;	/* View name */

	struct view_ops *ops;	/* View operations */
	struct argv_env *env;	/* View variables. */

	char ref[SIZEOF_REF];	/* Hovered commit reference */
	char vid[SIZEOF_REF];	/* View ID. Set to id member when updating. */

	int height, width;	/* The width and height of the main window */
	WINDOW *win;		/* The main window */
	WINDOW *title;		/* The title window */

	struct keymap *keymap;	/* What keymap does this view have */
	struct sort_state sort;	/* Sorting information. */

	/* Navigation */
	struct position pos;	/* Current position. */
	struct position prev_pos; /* Previous position. */

	/* View columns rendering state */
	struct column_info *columns_info;

	/* Searching */
	char grep[SIZEOF_STR];	/* Search string */
	regex_t *regex;		/* Pre-compiled regexp */

	/* If non-NULL, points to the view that opened this view. If this view
	 * is closed tig will switch back to the parent view. */
	struct view *parent;
	struct view *prev;

	/* Buffering */
	size_t lines;		/* Total number of lines */
	struct line *line;	/* Line index */
	unsigned int digits;	/* Number of digits in the lines member. */

	/* Number of lines with custom status, not to be counted in the
	 * view title. */
	unsigned int custom_lines;

	/* Drawing */
	struct line *curline;	/* Line currently being drawn. */
	enum line_type curtype;	/* Attribute currently used for drawing. */
	unsigned long col;	/* Column when drawing. */
	bool has_scrolled;	/* View was scrolled. */
	bool force_redraw;	/* Whether to force a redraw after reading. */

	/* Loading */
	const char **argv;	/* Shell command arguments. */
	const char *dir;	/* Directory from which to execute. */
	struct io io;
	struct io *pipe;
	time_t start_time;
	time_t update_secs;
	struct encoding *encoding;
	bool unrefreshable;

	/* Private data */
	void *private;
};

#define DEFINE_VIEW(name) struct view name ##_view = { #name, &name##_ops, &argv_env }

enum open_flags {
	OPEN_DEFAULT = 0,	/* Use default view switching. */
	OPEN_STDIN = 1,		/* Open in pager mode. */
	OPEN_FORWARD_STDIN = 2,	/* Forward stdin to I/O process. */
	OPEN_SPLIT = 4,		/* Split current view. */
	OPEN_RELOAD = 8,	/* Reload view even if it is the current. */
	OPEN_REFRESH = 16,	/* Refresh view using previous command. */
	OPEN_PREPARED = 32,	/* Open already prepared command. */
	OPEN_EXTRA = 64,	/* Open extra data from command. */

	OPEN_PAGER_MODE = OPEN_STDIN | OPEN_FORWARD_STDIN,
};

#define open_in_pager_mode(flags) ((flags) & OPEN_PAGER_MODE)
#define open_from_stdin(flags) ((flags) & OPEN_STDIN)

struct view_columns {
	const struct ident *author;
	const struct time *date;
	const char *file_name;
	const unsigned long *file_size;
	const char *id;
	const mode_t *mode;
	const struct ref *ref;
	const char *title;
};

struct view_ops {
	/* What type of content being displayed. Used in the title bar. */
	const char *type;
	/* Points to either of ref_{head,commit,blob} */
	const char *id;
	/* Flags to control the view behavior. */
	enum view_flag flags;
	/* Size of private data. */
	size_t private_size;
	/* Open and reads in all view content. */
	bool (*open)(struct view *view, enum open_flags flags);
	/* Read one line; updates view->line. */
	bool (*read)(struct view *view, char *data);
	/* Draw one line; @lineno must be < view->height. */
	bool (*draw)(struct view *view, struct line *line, unsigned int lineno);
	/* Depending on view handle a special requests. */
	enum request (*request)(struct view *view, enum request request, struct line *line);
	/* Search for regexp in a line. */
	bool (*grep)(struct view *view, struct line *line);
	/* Select line */
	void (*select)(struct view *view, struct line *line);
	/* Release resources when reloading the view */
	void (*done)(struct view *view);
	/* Extract line information. */
	bool (*get_columns)(struct view *view, const struct line *line, struct view_columns *columns);
	const enum view_column *columns;
	size_t columns_size;
};

/*
 * Global view state.
 */

struct view *get_view(int index);

#define foreach_view(view, i) \
	for (i = 0; (view = get_view(i)); i++)

#define view_has_line(view, line_) \
	((view)->line <= (line_) && (line_) < (view)->line + (view)->lines)

/*
 * Navigation
 */

bool goto_view_line(struct view *view, unsigned long offset, unsigned long lineno);
void select_view_line(struct view *view, unsigned long lineno);
void do_scroll_view(struct view *view, int lines);
void scroll_view(struct view *view, enum request request);
void move_view(struct view *view, enum request request);

/*
 * Searching
 */

void search_view(struct view *view, enum request request);
void find_next(struct view *view, enum request request);
bool grep_text(struct view *view, const char *text[]);

/*
 * View history
 */

struct view_state {
	struct view_state *prev;	/* Entry below this in the stack */
	struct position position;	/* View position to restore */
	void *data;			/* View specific state */
};

struct view_history {
	size_t state_alloc;
	struct view_state *stack;
	struct position position;
};

struct view_state *push_view_history_state(struct view_history *history, struct position *position, void *data);
bool pop_view_history_state(struct view_history *history, struct position *position, void *data);
void reset_view_history(struct view_history *history);

/*
 * View opening
 */

void split_view(struct view *prev, struct view *view);
void maximize_view(struct view *view, bool redraw);
void load_view(struct view *view, struct view *prev, enum open_flags flags);

#define refresh_view(view) load_view(view, NULL, OPEN_REFRESH)
#define reload_view(view) load_view(view, NULL, OPEN_RELOAD)

void open_view(struct view *prev, struct view *view, enum open_flags flags);
void open_argv(struct view *prev, struct view *view, const char *argv[], const char *dir, enum open_flags flags);

/*
 * Various utilities.
 */

#define get_sort_field(view) ((view)->ops->columns[(view)->sort.current])
void sort_view(struct view *view, bool change_field);

bool view_columns_grep(struct view *view, struct line *line);
bool view_columns_draw(struct view *view, struct line *line, unsigned int lineno);
bool view_columns_info_init(struct view *view);
bool view_columns_info_update(struct view *view, struct line *line);

struct line *
find_line_by_type(struct view *view, struct line *line, enum line_type type, int direction);

#define find_prev_line_by_type(view, line, type) \
	find_line_by_type(view, line, type, -1)

#define find_next_line_by_type(view, line, type) \
	find_line_by_type(view, line, type, 1)

#define is_initial_view(view) (!(view)->prev && !(view)->argv)
#define failed_to_load_initial_view(view) (!(view)->prev && !(view)->lines)

#define get_view_color(view, type)	get_line_color((view)->keymap->name, type)
#define get_view_attr(view, type)	get_line_attr((view)->keymap->name, type)

/*
 * Incremental updating
 */

static inline bool
check_position(struct position *pos)
{
	return pos->lineno || pos->col || pos->offset;
}

static inline void
clear_position(struct position *pos)
{
	memset(pos, 0, sizeof(*pos));
}

void reset_view(struct view *view);
bool begin_update(struct view *view, const char *dir, const char **argv, enum open_flags flags);
void end_update(struct view *view, bool force);
bool update_view(struct view *view);
void update_view_title(struct view *view);

/*
 * Line utilities.
 */

struct line *add_line_at(struct view *view, unsigned long pos, const void *data, enum line_type type, size_t data_size, bool custom);
struct line *add_line(struct view *view, const void *data, enum line_type type, size_t data_size, bool custom);
struct line *add_line_alloc_(struct view *view, void **ptr, enum line_type type, size_t data_size, bool custom);

#define add_line_alloc(view, data_ptr, type, extra_size, custom) \
	add_line_alloc_(view, (void **) data_ptr, type, sizeof(**data_ptr) + extra_size, custom)

struct line *add_line_nodata(struct view *view, enum line_type type);
struct line *add_line_text(struct view *view, const char *text, enum line_type type);
struct line * PRINTF_LIKE(3, 4) add_line_format(struct view *view, enum line_type type, const char *fmt, ...);

#endif
/* vim: set ts=8 sw=8 noexpandtab: */
