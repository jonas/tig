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

#ifndef TIG_DIFF_H
#define TIG_DIFF_H

#include "tig/view.h"

/*
 * Deferred syntax highlighting.
 *
 * Running the highlighter while the diff is read costs a fork+exec per file,
 * which dominates the load time of a diff touching many files.  Instead each
 * content line is recorded and rendered plain (keeping the diff background so
 * only foregrounds change later), and the highlighter runs when a line is
 * about to be drawn.  Files nobody scrolls to never spawn a process.
 *
 * Each file keeps its own highlighter and is fed strictly in order, so the
 * lexer state always matches the real file.  `fed` counts lines written to the
 * current process and resets when it is recycled; `applied` counts lines whose
 * colors have been installed and only ever grows.
 */
struct diff_lazy_line {
	char *data;			/* original diff line, prefix included */
	unsigned long lineno;		/* first view line for this diff line */
	unsigned int segs;		/* view lines it wrapped into */
	enum line_type type;
	bool indicator_stripped;	/* caller already removed the +/- */
};

struct diff_lazy_file {
	char path[SIZEOF_STR];
	pid_t pid;
	int write_fd;
	FILE *read_fp;
	bool failed;			/* highlighter gone; leave rest plain */
	unsigned int used;		/* LRU stamp for pipe recycling */
	unsigned int fed;		/* lines written to the current pipe */
	unsigned int applied;		/* lines whose colors are installed */
	struct diff_lazy_line *lines;
	size_t nlines;
};

/* Reverse map from a view line back to the diff line that produced it */
struct diff_lazy_map {
	int file;			/* index into syntax_files, or -1 */
	unsigned int index;		/* line index within that file */
};

struct diff_state {
	bool after_commit_title;
	bool after_diff;
	bool reading_diff_chunk;
	bool reading_diff_stat;
	bool combined_diff;
	bool adding_describe_ref;
	bool highlight;
	bool stage;
	unsigned int parents;
	const char *file;
	unsigned int lineno;
	struct position pos;
	struct io view_io;
	/* Syntax highlighting state */
	bool syntax_highlight;
	char syntax_file[SIZEOF_STR];	/* Current file from diff +++ header */
	struct diff_lazy_file *syntax_files;
	size_t syntax_nfiles;
	int syntax_current;		/* file being read, or -1 */
	unsigned int syntax_clock;	/* ticks the LRU stamps */
	struct diff_lazy_map *syntax_map;
	size_t syntax_map_size;		/* view lines covered by syntax_map */
};

enum request diff_common_edit(struct view *view, enum request request, struct line *line);
bool diff_common_read(struct view *view, const char *data, struct diff_state *state);
enum request diff_common_enter(struct view *view, enum request request, struct line *line);
struct line *diff_common_add_diff_stat(struct view *view, const char *text, size_t offset);
void diff_common_select(struct view *view, struct line *line, const char *changes_msg);
void diff_save_line(struct view *view, struct diff_state *state, enum open_flags flags);
void diff_restore_line(struct view *view, struct diff_state *state);
enum status_code diff_init_highlight(struct view *view, struct diff_state *state);
bool diff_done_highlight(struct diff_state *state);
void diff_init_syntax_highlight(struct diff_state *state);
void diff_done_syntax_highlight(struct diff_state *state);

/*
 * Draw and teardown hooks shared by the diff-like views, which defer syntax
 * highlighting until a line is about to be drawn.
 */
bool diff_draw(struct view *view, struct line *line, unsigned int lineno);
void diff_done(struct view *view);

/*
 * A wrapped diff line occupies several view lines.  Code that turns view lines
 * back into a patch must skip the continuations and use the original text.
 */
bool diff_is_wrapped_continuation(struct view *view, struct line *line);
const char *diff_original_text(struct view *view, struct line *line,
			       char *buf, size_t bufsize);

unsigned int diff_get_lineno(struct view *view, struct line *line, bool old);
const char *diff_get_pathname(struct view *view, struct line *line, bool old);

extern struct view diff_view;

static inline void
open_diff_view(struct view *prev, enum open_flags flags)
{
	open_view(prev, &diff_view, flags);
}

#endif
/* vim: set ts=8 sw=8 noexpandtab: */
