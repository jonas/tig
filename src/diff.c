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

#include <sys/wait.h>
#include "tig/argv.h"
#include "tig/refdb.h"
#include "tig/repo.h"
#include "tig/options.h"
#include "tig/display.h"
#include "tig/parse.h"
#include "tig/pager.h"
#include "tig/diff.h"
#include "tig/draw.h"
#include "tig/apps.h"
#include "tig/ansi.h"

static enum status_code
diff_open(struct view *view, enum open_flags flags)
{
	const char *diff_argv[] = {
		"git", "show", encoding_arg, "--pretty=fuller", "--root",
			"--patch-with-stat", use_mailmap_arg(),
			show_notes_arg(), diff_context_arg(), ignore_space_arg(),
			DIFF_ARGS, "%(cmdlineargs)", "--no-color", word_diff_arg(),
			"%(commit)", "--", "%(fileargs)", NULL
	};
	enum status_code code;

	diff_save_line(view, view->private, flags);

	code = begin_update(view, NULL, diff_argv, flags | OPEN_WITH_STDERR);
	if (code != SUCCESS)
		return code;

	diff_init_syntax_highlight(view->private);

	return diff_init_highlight(view, view->private);
}

enum status_code
diff_init_highlight(struct view *view, struct diff_state *state)
{
	if (!opt_diff_highlight || !*opt_diff_highlight || opt_word_diff)
		return SUCCESS;

	struct app_external *app = app_diff_highlight_load(opt_diff_highlight);
	struct io io;

	/* XXX This empty string keeps valgrind happy while preserving earlier
	 * behavior of test diff/diff-highlight-test:diff-highlight-misconfigured.
	 * Simpler would be to return error when user misconfigured, though we
	 * don't want tig to fail when diff-highlight isn't present.  io_exec
	 * below does not return error when app->argv[0] is empty or null as the
	 * conditional might suggest. */
	if (!*app->argv)
		app->argv[0] = "";

	if (!io_exec(&io, IO_RP, view->dir, app->env, app->argv, view->io.pipe))
		return error("Failed to run %s", opt_diff_highlight);

	state->view_io = view->io;
	view->io = io;
	state->highlight = true;

	return SUCCESS;
}

bool
diff_done_highlight(struct diff_state *state)
{
	if (!state->highlight)
		return true;
	io_kill(&state->view_io);
	return io_done(&state->view_io);
}

/*
 * Syntax highlighting for diff content lines.
 * Composes syntax foreground colors with diff background colors.
 */

/*
 * Highlighter processes stay alive so scrolling back into a file does not
 * replay it, but a big diff must not accumulate one process per file.
 */
#define SYNTAX_MAX_OPEN_PIPES	4

DEFINE_ALLOCATOR(realloc_lazy_files, struct diff_lazy_file, 8)
DEFINE_ALLOCATOR(realloc_lazy_lines, struct diff_lazy_line, 64)
DEFINE_ALLOCATOR(realloc_lazy_map, struct diff_lazy_map, 256)

void
diff_init_syntax_highlight(struct diff_state *state)
{
	/* Reloading reuses the same state and drops every view line, so any
	 * recorded lines and live processes from the previous load have to go
	 * before new ones are registered.  Safe on a zeroed state. */
	diff_done_syntax_highlight(state);

	state->syntax_highlight = opt_syntax_highlight && *opt_syntax_highlight
				  && COLORS >= 256;
	state->syntax_file[0] = '\0';
	state->syntax_files = NULL;
	state->syntax_nfiles = 0;
	state->syntax_current = -1;
	state->syntax_clock = 0;
	state->syntax_map = NULL;
	state->syntax_map_size = 0;
}

static void
syntax_pipe_close(struct diff_lazy_file *file)
{
	if (file->write_fd >= 0) {
		close(file->write_fd);
		file->write_fd = -1;
	}
	if (file->read_fp) {
		fclose(file->read_fp);
		file->read_fp = NULL;
	}
	if (file->pid > 0) {
		kill(file->pid, SIGTERM);
		waitpid(file->pid, NULL, 0);
		file->pid = 0;
	}
	/* Lexer state died with the process; a reopen must replay */
	file->fed = 0;
}

/*
 * Discard the remainder of the current line from bat's output.  fgets(3)
 * leaves the tail in the pipe when a colorized line overflows the read
 * buffer, which would misalign every line read after it.
 */
static bool
syntax_skip_rest_of_line(struct diff_lazy_file *file)
{
	int c;

	while ((c = fgetc(file->read_fp)) != EOF)
		if (c == '\n')
			return true;

	return false;
}

static bool
syntax_pipe_open(struct diff_lazy_file *file)
{
	int stdin_pipe[2], stdout_pipe[2];
	pid_t pid;
	struct app_external *app;
	const char *buffered[SIZEOF_ARG];
	size_t argc = 0, i;

	syntax_pipe_close(file);

	/* Resolve bat and build its argv in one place (see apps.c) */
	app = app_syntax_highlight_load(opt_syntax_highlight, file->path);
	if (!app->argv[0])
		return false;

	/* Line-buffer bat's output so each line lands as soon as it is
	 * written; without this the pipe stalls behind a block buffer. */
	buffered[argc++] = "stdbuf";
	buffered[argc++] = "-oL";
	for (i = 0; app->argv[i] && argc < ARRAY_SIZE(buffered) - 1; i++)
		buffered[argc++] = app->argv[i];
	buffered[argc] = NULL;

	if (pipe(stdin_pipe) < 0)
		return false;
	if (pipe(stdout_pipe) < 0) {
		close(stdin_pipe[0]);
		close(stdin_pipe[1]);
		return false;
	}

	pid = fork();
	if (pid < 0) {
		close(stdin_pipe[0]); close(stdin_pipe[1]);
		close(stdout_pipe[0]); close(stdout_pipe[1]);
		return false;
	}

	if (pid == 0) {
		/* Child: bat process */
		close(stdin_pipe[1]);
		close(stdout_pipe[0]);
		dup2(stdin_pipe[0], STDIN_FILENO);
		dup2(stdout_pipe[1], STDOUT_FILENO);
		close(stdin_pipe[0]);
		close(stdout_pipe[1]);

		execvp(buffered[0], (char * const *) buffered);
		/* If stdbuf is not available, run bat directly */
		execvp(app->argv[0], (char * const *) app->argv);
		_exit(127);
	}

	/* Parent */
	close(stdin_pipe[0]);
	close(stdout_pipe[1]);

	file->pid = pid;
	file->write_fd = stdin_pipe[1];
	file->read_fp = fdopen(stdout_pipe[0], "r");
	if (!file->read_fp) {
		syntax_pipe_close(file);
		return false;
	}

	return true;
}

/*
 * Keep at most SYNTAX_MAX_OPEN_PIPES highlighters alive, dropping the least
 * recently used.  A dropped file keeps its `applied` count, so revisiting it
 * only costs a replay to rebuild lexer state, never duplicate work.
 */
static bool
syntax_pipe_acquire(struct diff_state *state, struct diff_lazy_file *file)
{
	file->used = ++state->syntax_clock;

	if (file->read_fp)
		return true;

	{
		size_t i, open = 0;
		struct diff_lazy_file *lru = NULL;

		for (i = 0; i < state->syntax_nfiles; i++) {
			struct diff_lazy_file *other = &state->syntax_files[i];

			if (other == file || !other->read_fp)
				continue;
			open++;
			if (!lru || other->used < lru->used)
				lru = other;
		}

		if (open >= SYNTAX_MAX_OPEN_PIPES && lru)
			syntax_pipe_close(lru);
	}

	return syntax_pipe_open(file);
}

void
diff_done_syntax_highlight(struct diff_state *state)
{
	size_t i, j;

	for (i = 0; i < state->syntax_nfiles; i++) {
		struct diff_lazy_file *file = &state->syntax_files[i];

		syntax_pipe_close(file);
		for (j = 0; j < file->nlines; j++)
			free(file->lines[j].data);
		free(file->lines);
	}

	free(state->syntax_files);
	free(state->syntax_map);
	state->syntax_files = NULL;
	state->syntax_nfiles = 0;
	state->syntax_current = -1;
	state->syntax_map = NULL;
	state->syntax_map_size = 0;
}

static void
diff_track_filename(struct diff_state *state, const char *data, enum line_type type)
{
	/* Extract filename from "+++ b/path" header lines */
	if (type == LINE_DIFF_ADD_FILE && !state->reading_diff_chunk) {
		const char *path = data;

		/* Skip "+++ " prefix */
		if (!prefixcmp(path, "+++ "))
			path += 4;
		/* Skip "b/" prefix from git diffs */
		if (!prefixcmp(path, "b/"))
			path += 2;

		/* Start a new lazy file when the path changes.  No process is
		 * spawned until one of its lines is about to be drawn. */
		if (strcmp(state->syntax_file, path)) {
			struct diff_lazy_file *file;

			string_ncopy(state->syntax_file, path, strlen(path));

			if (!realloc_lazy_files(&state->syntax_files,
						state->syntax_nfiles, 1))
				return;

			file = &state->syntax_files[state->syntax_nfiles];
			memset(file, 0, sizeof(*file));
			string_ncopy(file->path, path, strlen(path));
			file->write_fd = -1;
			state->syntax_current = state->syntax_nfiles++;
		}
	}
}

static int
diff_bg_color_for_type(enum line_type type)
{
	/* Map diff line types to background color indices.
	 * Use dark 256-color shades so syntax fg colors remain readable.
	 * 22 = dark green, 52 = dark red */
	switch (type) {
	case LINE_DIFF_ADD:
	case LINE_DIFF_ADD2:
		return 22;   /* dark green background */
	case LINE_DIFF_DEL:
	case LINE_DIFF_DEL2:
		return 52;   /* dark red background */
	default:
		return -1;   /* default background */
	}
}

static int
diff_bg_emphasis_for_type(enum line_type type)
{
	/* Brighter background for diff-highlight emphasized regions.
	 * 28 = medium green, 88 = medium red */
	switch (type) {
	case LINE_DIFF_ADD:
	case LINE_DIFF_ADD2:
		return 65;   /* muted olive-green background */
	case LINE_DIFF_DEL:
	case LINE_DIFF_DEL2:
		return 88;   /* medium red background */
	default:
		return -1;
	}
}

static int
diff_prefix_fg_for_type(enum line_type type)
{
	/* Bright green/red for the +/- prefix characters */
	switch (type) {
	case LINE_DIFF_ADD:
	case LINE_DIFF_ADD2:
		return 2;    /* COLOR_GREEN */
	case LINE_DIFF_DEL:
	case LINE_DIFF_DEL2:
		return 1;    /* COLOR_RED */
	default:
		return -1;
	}
}

/*
 * Width of the +/- indicator column(s) on a diff content line.  Combined
 * diffs carry one column per parent.  Returns (size_t) -1 when the line is
 * shorter than its own indicator, which would push the content pointer past
 * the terminator.
 */
static size_t
diff_prefix_length(const char *text, enum line_type type,
		   struct diff_state *state, bool indicator_stripped)
{
	if (indicator_stripped)
		return 0;

	if (type == LINE_DIFF_ADD || type == LINE_DIFF_ADD2
	    || type == LINE_DIFF_DEL || type == LINE_DIFF_DEL2
	    || text[0] == ' ') {
		if (strlen(text) < state->parents)
			return (size_t) -1;
		return state->parents;
	}

	return 0;
}

/*
 * Escape-free view of a diff line.  The deferred render and the later
 * highlight must wrap the exact same bytes, so both go through this.
 * Returns NULL if the line does not fit, meaning it stays unhighlighted.
 */
static const char *
diff_plain_text(const char *data, char *buf, size_t bufsize)
{
	struct ansi_span ignored[1];

	if (!ansi_has_escapes(data))
		return data;
	if (ansi_parse_line(data, buf, bufsize, ignored, 0) < 0)
		return NULL;
	return buf;
}

/*
 * Split syntax spans at diff-highlight emphasis boundaries so only the
 * characters that actually changed get the brighter background.  Rewrites
 * `spans` in place and returns the new span count.
 */
static int
diff_merge_emphasis(enum line_type type, const char *stripped,
		    struct ansi_span *spans, int nspans,
		    const struct ansi_span *dh_spans, int dh_nspans)
{
	int emph_bg = diff_bg_emphasis_for_type(type);
	struct ansi_span merged[ANSI_MAX_SPANS];
	int nmerged = 0;
	int i, j;
	/* Reverse-video bitmap over `stripped`.  Only the bytes indexed
	 * below are read, so clear just those rather than zero-filling a
	 * kilobyte for every diff line. */
	char is_emph[SIZEOF_STR];
	size_t emph_len = strlen(stripped);

	if (emph_len > sizeof(is_emph))
		emph_len = sizeof(is_emph);
	memset(is_emph, 0, emph_len);

	for (j = 0; j < dh_nspans; j++) {
		size_t k;

		if (!(dh_spans[j].attr & A_REVERSE))
			continue;
		for (k = dh_spans[j].offset;
		     k < dh_spans[j].offset + dh_spans[j].length && k < emph_len;
		     k++)
			is_emph[k] = 1;
	}

	for (i = 0; i < nspans && nmerged < ANSI_MAX_SPANS - 1; i++) {
		size_t pos = spans[i].offset;
		size_t end = pos + spans[i].length;

		while (pos < end && nmerged < ANSI_MAX_SPANS - 1) {
			bool emph = pos < emph_len && is_emph[pos];
			size_t run = pos;

			/* Find run of same emphasis state */
			while (run < end && run < emph_len
			       && (is_emph[run] ? 1 : 0) == emph)
				run++;
			if (run >= emph_len)
				run = end;

			merged[nmerged] = spans[i];
			merged[nmerged].offset = pos;
			merged[nmerged].length = run - pos;
			if (emph && emph_bg >= 0) {
				merged[nmerged].bg.type = ANSI_COLOR_256;
				merged[nmerged].bg.index = emph_bg;
			}
			nmerged++;
			pos = run;
		}
	}

	memcpy(spans, merged, sizeof(spans[0]) * nmerged);
	return nmerged;
}

#define DIFF_MAX_WRAP_SEGS	256

/*
 * Bytes taken by each wrapped segment.  This depends only on the text, the
 * view width and the tab size -- never on the colors -- which is what lets
 * the deferred render and the later highlight agree on the line count.
 *
 * Returns the segment count, or 0 if the text could not be laid out.
 */
static unsigned int
diff_wrap_measure(struct view *view, const char *text, size_t len,
		  size_t *segbytes, unsigned int maxsegs)
{
	unsigned int nsegs = 0;
	size_t pos = 0;

	while (pos < len && nsegs < maxsegs) {
		int avail = view->width;
		int indent = nsegs ? (2 < avail ? 2 : 1) : 0;
		int width = indent;
		size_t take = 0;

		/* The indent shares the segment's buffer, so leave room for it
		 * here rather than clamping later and losing bytes */
		while (pos + take < len && width < avail
		       && take + indent < SIZEOF_STR - 1) {
			if (text[pos + take] == '\t')
				width += opt_tab_size - (width % opt_tab_size);
			else
				width++;
			take++;
		}

		if (!take)
			break;

		segbytes[nsegs++] = take;
		pos += take;
	}

	return pos == len ? nsegs : 0;
}

/*
 * Install one wrapped segment.  In replace mode the view line already exists
 * and only its box is swapped, so the line count never shifts under the
 * cursor; the line is marked dirty for the next repaint.
 */
static struct line *
diff_emit_segment(struct view *view, struct diff_lazy_line *rec, bool replace,
		  unsigned int seg, const char *text, size_t textlen,
		  enum line_type type, const struct box_cell *cells, size_t ncells)
{
	struct line *line;
	struct box *box;

	if (!replace) {
		line = add_line_text_at_(view, view->lines, text, textlen, type,
					 ncells, false);
		if (!line)
			return NULL;

		/* add_line_text_at_ leaves one cell covering the whole text;
		 * swap in the real ones it reserved room for. */
		box = line->data;
		memcpy(box->cell, cells, sizeof(*cells) * ncells);
		box->cells = ncells;
		return line;
	}

	if (seg >= rec->segs || rec->lineno + seg >= view->lines)
		return NULL;

	/* Cells and text share one allocation, so growing the cell count
	 * means building a new box rather than extending in place. */
	box = calloc(1, box_sizeof(NULL, ncells, textlen));
	if (!box)
		return NULL;

	memcpy(box->cell, cells, sizeof(*cells) * ncells);
	box->cells = ncells;
	box_text_copy(box, ncells, text, textlen);

	line = &view->line[rec->lineno + seg];
	free(line->data);
	line->data = box;
	line->dirty = 1;
	if (view->ops->column_bits)
		view_column_info_update(view, line);

	return line;
}

/*
 * Emit `full_line` as one or more view lines, wrapping GitHub-style with a
 * two-space continuation indent so the diff background stays contiguous.
 *
 * With nspans == 0 the line gets a single flat cell, which is how a deferred
 * line is rendered: the diff background is already right, so filling in the
 * real spans later only changes foregrounds.
 */
static bool
diff_emit_wrapped(struct view *view, const char *full_line, size_t prefix_len,
		  enum line_type type, const struct ansi_span *spans, int nspans,
		  struct diff_lazy_line *rec, bool replace)
{
	struct ansi_color plain_fg = { ANSI_COLOR_DEFAULT, { .index = 0 } };
	struct box_cell all_cells[ANSI_MAX_SPANS + 2];
	size_t segbytes[DIFF_MAX_WRAP_SEGS];
	size_t full_len = strlen(full_line);
	size_t covered = 0;
	int diff_bg = diff_bg_color_for_type(type);
	struct ansi_color bg_color;
	unsigned long first_lineno = 0;
	unsigned int nsegs, seg;
	int ncells = 0;
	int i;

	bg_color.type = (diff_bg >= 0) ? ANSI_COLOR_256 : ANSI_COLOR_DEFAULT;
	bg_color.index = diff_bg;

	if (prefix_len > 0) {
		int pfx_fg = diff_prefix_fg_for_type(type);
		struct ansi_color pfx_fg_color;

		pfx_fg_color.type = (pfx_fg >= 0) ? ANSI_COLOR_BASIC : ANSI_COLOR_DEFAULT;
		pfx_fg_color.index = pfx_fg;

		memset(&all_cells[0], 0, sizeof(all_cells[0]));
		all_cells[0].type = type;
		all_cells[0].length = prefix_len;
		all_cells[0].direct = 1;
		all_cells[0].color_pair = get_dynamic_color_pair(&pfx_fg_color, &bg_color);
		all_cells[0].attr = A_BOLD;
		ncells = 1;
		covered = prefix_len;
	}

	if (nspans > 0) {
		for (i = 0; i < nspans && ncells < (int) ARRAY_SIZE(all_cells); i++) {
			/* Zero-length cells would stall the slicing below */
			if (!spans[i].length)
				continue;
			memset(&all_cells[ncells], 0, sizeof(all_cells[0]));
			all_cells[ncells].type = type;
			all_cells[ncells].length = spans[i].length;
			all_cells[ncells].direct = 1;
			all_cells[ncells].color_pair = get_dynamic_color_pair(&spans[i].fg,
				spans[i].bg.type != ANSI_COLOR_DEFAULT ? &spans[i].bg : &bg_color);
			all_cells[ncells].attr = spans[i].attr;
			covered += spans[i].length;
			ncells++;
		}
	} else if (full_len > prefix_len) {
		memset(&all_cells[ncells], 0, sizeof(all_cells[0]));
		all_cells[ncells].type = type;
		all_cells[ncells].length = full_len - prefix_len;
		all_cells[ncells].direct = 1;
		all_cells[ncells].color_pair = get_dynamic_color_pair(&plain_fg, &bg_color);
		covered = full_len;
		ncells++;
	}

	/* The cells must tile the text exactly or the slicing below would
	 * cut at the wrong offsets */
	if (!ncells || covered != full_len)
		return false;

	nsegs = diff_wrap_measure(view, full_line, full_len, segbytes,
				  ARRAY_SIZE(segbytes));
	if (!nsegs)
		return false;

	/* Geometry moved since the line was recorded; leave it as it is
	 * rather than rewriting a different number of view lines. */
	if (replace && nsegs != rec->segs)
		return false;

	if (!replace) {
		rec->lineno = view->lines;
		rec->segs = nsegs;
	}

	{
		size_t pos = 0;
		int cell_idx = 0;
		size_t cell_offset = 0;

		for (seg = 0; seg < nsegs; seg++) {
			struct box_cell segcells[ANSI_MAX_SPANS + 2];
			char line_buf[SIZEOF_STR];
			size_t line_len = 0;
			size_t remaining = segbytes[seg];
			int seg_ncells = 0;
			struct line *line;

			/* Continuation indent is drawn by us rather than via
			 * line->wrapped so it carries the diff background */
			if (seg > 0) {
				int indent = 2 < view->width ? 2 : 1;

				memset(line_buf, ' ', indent);
				line_len = indent;
				memset(&segcells[0], 0, sizeof(segcells[0]));
				segcells[0].type = type;
				segcells[0].length = indent;
				segcells[0].direct = 1;
				segcells[0].color_pair =
					get_dynamic_color_pair(&plain_fg, &bg_color);
				seg_ncells = 1;
			}

			while (remaining > 0 && cell_idx < ncells
			       && seg_ncells < (int) ARRAY_SIZE(segcells)) {
				size_t avail = all_cells[cell_idx].length - cell_offset;
				size_t take = avail < remaining ? avail : remaining;

				if (line_len + take > sizeof(line_buf) - 1)
					take = sizeof(line_buf) - 1 - line_len;
				if (!take)
					break;

				memcpy(line_buf + line_len, full_line + pos, take);
				line_len += take;
				pos += take;
				remaining -= take;

				segcells[seg_ncells] = all_cells[cell_idx];
				segcells[seg_ncells].length = take;
				seg_ncells++;

				cell_offset += take;
				if (cell_offset >= all_cells[cell_idx].length) {
					cell_idx++;
					cell_offset = 0;
				}
			}

			if (!line_len)
				break;

			line_buf[line_len] = '\0';

			line = diff_emit_segment(view, rec, replace, seg, line_buf,
						 line_len, type, segcells, seg_ncells);
			if (!line)
				return seg > 0;

			/* Wrapped segments share the diff line number */
			if (!replace) {
				if (!seg)
					first_lineno = line->lineno;
				line->lineno = first_lineno;
			}
		}
	}

	return true;
}

/*
 * Read path: record the line and render it plain.  The highlighter is not
 * touched here, so a diff touching many files costs no process spawns until
 * something is actually drawn.
 */
static bool
diff_syntax_highlight_line(struct view *view, const char *data,
			   enum line_type type, struct diff_state *state,
			   bool indicator_stripped)
{
	char plain_buf[SIZEOF_STR];
	const char *plain;
	struct diff_lazy_file *file;
	struct diff_lazy_line *rec;
	size_t prefix_len;
	unsigned long ln;

	if (state->syntax_current < 0)
		return false;

	plain = diff_plain_text(data, plain_buf, sizeof(plain_buf));
	if (!plain)
		return false;

	prefix_len = diff_prefix_length(plain, type, state, indicator_stripped);
	if (prefix_len == (size_t) -1)
		return false;

	file = &state->syntax_files[state->syntax_current];
	if (!realloc_lazy_lines(&file->lines, file->nlines, 1))
		return false;

	rec = &file->lines[file->nlines];
	memset(rec, 0, sizeof(*rec));
	rec->type = type;
	rec->indicator_stripped = indicator_stripped;
	rec->data = strdup(data);
	if (!rec->data)
		return false;

	if (!diff_emit_wrapped(view, plain, prefix_len, type, NULL, 0, rec, false)) {
		free(rec->data);
		rec->data = NULL;
		return false;
	}

	file->nlines++;

	/* Map the emitted view lines back to this diff line.  Lines added by
	 * other readers get file == -1 and are simply never highlighted. */
	if (view->lines > state->syntax_map_size) {
		if (!realloc_lazy_map(&state->syntax_map, state->syntax_map_size,
				      view->lines - state->syntax_map_size))
			return true;

		for (ln = state->syntax_map_size; ln < view->lines; ln++) {
			state->syntax_map[ln].file = -1;
			state->syntax_map[ln].index = 0;
		}
		state->syntax_map_size = view->lines;
	}

	for (ln = rec->lineno; ln < rec->lineno + rec->segs
			       && ln < state->syntax_map_size; ln++) {
		state->syntax_map[ln].file = state->syntax_current;
		state->syntax_map[ln].index = file->nlines - 1;
	}

	return true;
}

/*
 * Push one recorded line through the highlighter.  `apply` is false while
 * replaying lines whose colors are already installed, which happens when a
 * recycled process has to rebuild its lexer state.
 *
 * Returns false only when the highlighter itself is unusable.
 */
static bool
diff_lazy_highlight_one(struct view *view, struct diff_state *state,
			struct diff_lazy_file *file, struct diff_lazy_line *rec,
			bool apply)
{
	char highlighted[SIZEOF_STR * 2];
	char stripped[SIZEOF_STR];
	char full_line[SIZEOF_STR];
	char clean_content[SIZEOF_STR];
	char plain_buf[SIZEOF_STR];
	struct ansi_span spans[ANSI_MAX_SPANS];
	struct ansi_span dh_spans[ANSI_MAX_SPANS];
	int nspans, dh_nspans = 0;
	const char *plain;
	const char *content;
	size_t prefix_len;
	bool has_emphasis;

	plain = diff_plain_text(rec->data, plain_buf, sizeof(plain_buf));
	if (!plain)
		return true;

	prefix_len = diff_prefix_length(plain, rec->type, state,
					rec->indicator_stripped);
	if (prefix_len == (size_t) -1)
		return true;

	/* A prefix-only line has nothing to highlight and was already
	 * rendered with the right background */
	if (!plain[prefix_len])
		return true;

	/* diff-highlight marks changed regions with reverse video; strip
	 * those before bat sees them but remember where they were */
	content = rec->data + prefix_len;
	has_emphasis = ansi_has_escapes(content);
	if (has_emphasis) {
		dh_nspans = ansi_parse_line(content, clean_content,
					    sizeof(clean_content),
					    dh_spans, ANSI_MAX_SPANS);
		if (dh_nspans < 0)
			return true;
		content = clean_content;
	}

	{
		size_t content_len = strlen(content);

		/* Give up before writing: the reply has to round-trip through
		 * `stripped`, which must also hold the +/- prefix.  Writing
		 * first would leave a line in the pipe that nobody reads
		 * back, desyncing the rest of the file. */
		if (content_len + prefix_len >= sizeof(stripped))
			return true;

		if (write(file->write_fd, content, content_len) < 0
		    || write(file->write_fd, "\n", 1) < 0)
			return false;
	}

	highlighted[0] = '\0';
	if (!fgets(highlighted, sizeof(highlighted), file->read_fp))
		return false;

	{
		size_t len = strlen(highlighted);

		if (len == 0 || highlighted[len - 1] != '\n')
			/* Colorized line outgrew the buffer; drop its tail to
			 * keep the pipe aligned and leave this line plain */
			return syntax_skip_rest_of_line(file);
		highlighted[len - 1] = '\0';
	}

	if (!apply)
		return true;

	/* Reserve room for the prefix prepended below, so the
	 * reconstruction cannot run past `full_line` */
	nspans = ansi_parse_line(highlighted, stripped,
				 sizeof(stripped) - prefix_len,
				 spans, ANSI_MAX_SPANS);
	if (nspans <= 0)
		return true;

	if (has_emphasis && dh_nspans > 0)
		nspans = diff_merge_emphasis(rec->type, stripped, spans, nspans,
					     dh_spans, dh_nspans);

	if (prefix_len > 0) {
		memcpy(full_line, plain, prefix_len);
		memcpy(full_line + prefix_len, stripped, strlen(stripped) + 1);
	} else {
		memcpy(full_line, stripped, strlen(stripped) + 1);
	}

	diff_emit_wrapped(view, full_line, prefix_len, rec->type,
			  spans, nspans, rec, true);
	return true;
}

/*
 * Bring `line` up to date before it is drawn, feeding its file to the
 * highlighter in order from wherever the current process left off.
 */
static void
diff_lazy_fill(struct view *view, struct diff_state *state, struct line *line)
{
	struct diff_lazy_file *file;
	unsigned long lineno;
	unsigned int index;

	if (!state->syntax_highlight || !state->syntax_map)
		return;

	lineno = line - view->line;
	if (lineno >= state->syntax_map_size || state->syntax_map[lineno].file < 0)
		return;

	file = &state->syntax_files[state->syntax_map[lineno].file];
	index = state->syntax_map[lineno].index;

	if (file->failed || index >= file->nlines)
		return;

	/* Already colored.  Scrolling back into a file must not restart its
	 * highlighter; only a line past `applied` needs the pipe advanced. */
	if (index < file->applied)
		return;

	if (!file->read_fp && !syntax_pipe_acquire(state, file)) {
		file->failed = true;
		return;
	}

	while (file->fed <= index) {
		bool apply = file->fed >= file->applied;

		if (!diff_lazy_highlight_one(view, state, file,
					     &file->lines[file->fed], apply)) {
			file->failed = true;
			syntax_pipe_close(file);
			return;
		}
		if (apply)
			file->applied = file->fed + 1;
		file->fed++;
	}
}

/*
 * Locate the recorded diff line behind a view line, if any.  Header, chunk and
 * stat lines are not recorded and yield NULL.
 */
static struct diff_lazy_line *
diff_lazy_record(struct view *view, struct line *line, unsigned long *lineno_out)
{
	struct diff_state *state = view->private;
	struct diff_lazy_file *file;
	unsigned long lineno;
	unsigned int index;

	if (!state || !state->syntax_map)
		return NULL;

	lineno = line - view->line;
	if (lineno >= state->syntax_map_size || state->syntax_map[lineno].file < 0)
		return NULL;

	file = &state->syntax_files[state->syntax_map[lineno].file];
	index = state->syntax_map[lineno].index;
	if (index >= file->nlines)
		return NULL;

	if (lineno_out)
		*lineno_out = lineno;

	return &file->lines[index];
}

/*
 * True when this view line only continues a diff line that was wrapped across
 * several rows.  Callers that reconstruct a patch have to skip these: their
 * text is a slice of the first row's line, and their line type repeats it.
 */
bool
diff_is_wrapped_continuation(struct view *view, struct line *line)
{
	unsigned long lineno = 0;
	struct diff_lazy_line *rec = diff_lazy_record(view, line, &lineno);

	return rec && rec->segs > 1 && lineno > rec->lineno;
}

/*
 * The diff line as git produced it, with any highlighter escapes removed.
 * Wrapping and syntax colors never reach this, so a reconstructed patch matches
 * what git emitted regardless of how the line was displayed.
 */
const char *
diff_original_text(struct view *view, struct line *line, char *buf, size_t bufsize)
{
	struct diff_lazy_line *rec = diff_lazy_record(view, line, NULL);
	const char *plain;

	if (!rec)
		return box_text(line);

	plain = diff_plain_text(rec->data, buf, bufsize);

	return plain ? plain : box_text(line);
}

/*
 * Draw hook for the diff-like views.  `struct diff_state` is the first member
 * of every private struct that reaches here, so the cast holds for the diff,
 * stage and pager views alike.
 */
bool
diff_draw(struct view *view, struct line *line, unsigned int lineno)
{
	diff_lazy_fill(view, view->private, line);
	return view_column_draw(view, line, lineno);
}

void
diff_done(struct view *view)
{
	diff_done_syntax_highlight(view->private);
}

struct diff_stat_context {
	const char *text;
	enum line_type type;
	bool skip;
	size_t cells;
	const char **cell_text;
	struct box_cell cell[8192];
};

static bool
diff_common_add_cell(struct diff_stat_context *context, size_t length, bool allow_empty)
{
	if (!allow_empty && (length == 0))
		return true;
	if (context->cells > ARRAY_SIZE(context->cell) - 1) {
		report("Too many diff cells, truncating");
		return false;
	}
	if (context->skip && !argv_appendn(&context->cell_text, context->text, length))
		return false;
	memset(&context->cell[context->cells], 0, sizeof(context->cell[0]));
	context->cell[context->cells].length = length;
	context->cell[context->cells].type = context->type;
	context->cells++;
	return true;
}

static struct line *
diff_common_add_line(struct view *view, const char *text, enum line_type type, struct diff_stat_context *context)
{
	char *cell_text = context->cell_text ? argv_to_string_alloc(context->cell_text, "") : NULL;
	const char *line_text = cell_text ? cell_text : text;
	struct line *line = add_line_text_at(view, view->lines, line_text, type, context->cells);
	struct box *box;

	free(cell_text);
	argv_free(context->cell_text);
	free(context->cell_text);

	if (!line)
		return NULL;

	box = line->data;
	if (context->cells)
		memcpy(box->cell, context->cell, sizeof(struct box_cell) * context->cells);
	box->cells = context->cells;
	return line;
}

static bool
diff_common_add_cell_until(struct diff_stat_context *context, const char *s, enum line_type next_type)
{
	const char *sep = strstr(context->text, s);

	if (sep == NULL)
		return false;

	if (!diff_common_add_cell(context, sep - context->text, false))
		return false;

	context->text = sep + (context->skip ? strlen(s) : 0);
	context->type = next_type;

	return true;
}

static bool
diff_common_read_diff_stat_part(struct diff_stat_context *context, char c, enum line_type next_type)
{
	const char *sep = c == '|' ? strrchr(context->text, c) : strchr(context->text, c);

	if (sep == NULL)
		return false;

	diff_common_add_cell(context, sep - context->text, false);
	context->text = sep;
	context->type = next_type;

	return true;
}

static struct line *
diff_common_read_diff_stat(struct view *view, const char *text)
{
	struct diff_stat_context context = { text, LINE_DIFF_STAT };

	diff_common_read_diff_stat_part(&context, '|', LINE_DEFAULT);
	if (diff_common_read_diff_stat_part(&context, 'B', LINE_DEFAULT)) {
		/* Handle binary diffstat: Bin <deleted> -> <added> bytes */
		diff_common_read_diff_stat_part(&context, ' ', LINE_DIFF_DEL);
		diff_common_read_diff_stat_part(&context, '-', LINE_DEFAULT);
		diff_common_read_diff_stat_part(&context, ' ', LINE_DIFF_ADD);
		diff_common_read_diff_stat_part(&context, 'b', LINE_DEFAULT);

	} else {
		diff_common_read_diff_stat_part(&context, '+', LINE_DIFF_ADD);
		diff_common_read_diff_stat_part(&context, '-', LINE_DIFF_DEL);
	}
	diff_common_add_cell(&context, strlen(context.text), false);

	return diff_common_add_line(view, text, LINE_DIFF_STAT, &context);
}

struct line *
diff_common_add_diff_stat(struct view *view, const char *text, size_t offset)
{
	const char *start = text + offset;
	const char *data = start + strspn(start, " ");
	size_t len = strlen(data);
	const char *pipe = strchr(data, '|');

	/* Ensure that '|' is present and the file name part contains
	 * non-space characters. */
	if (!pipe || pipe == data)
		return NULL;

	/* Detect remaining part of a diff stat line:
	 *
	 *	added                    |   40 +++++++++++
	 *	remove                   |  124 --------------------------
	 *	updated                  |   14 +----
	 *	rename.from => rename.to |    0
	 *	.../truncated file name  |   11 ++---
	 *	binary add               |  Bin 0 -> 1234 bytes
	 *	binary update            |  Bin 1234 -> 2345 bytes
	 *	binary copy              |  Bin
	 *	unmerged                 | Unmerged
	 */
	if ((data[len - 1] == '-' || data[len - 1] == '+') ||
	    strstr(pipe, " 0") || strstr(pipe, "Bin") || strstr(pipe, "Unmerged") ||
	    (data[len - 1] == '0' && (strstr(data, "=>") || !prefixcmp(data, "..."))))
		return diff_common_read_diff_stat(view, text);
	return NULL;
}

static bool
diff_common_read_diff_wdiff_group(struct diff_stat_context *context)
{
	const char *sep_add = strstr(context->text, "{+");
	const char *sep_del = strstr(context->text, "[-");
	const char *sep;
	enum line_type next_type;
	const char *end_delimiter;
	const char *end_sep;
	size_t len;

	if (sep_add == NULL && sep_del == NULL)
		return false;

	if (sep_del == NULL || (sep_add != NULL && sep_add < sep_del)) {
		sep = sep_add;
		next_type = LINE_DIFF_ADD;
		end_delimiter = "+}";
	} else {
		sep = sep_del;
		next_type = LINE_DIFF_DEL;
		end_delimiter = "-]";
	}

	diff_common_add_cell(context, sep - context->text, false);

	context->type = next_type;
	context->text = sep;

	// workaround for a single }/] change
	end_sep = strstr(context->text + sizeof("{+") - 1, end_delimiter);

	if (end_sep == NULL) {
		// diff is not terminated
		len = strlen(context->text);
	} else {
		// separators are included in the add/del highlight
		len = end_sep - context->text + sizeof("+}") - 1;
	}

	diff_common_add_cell(context, len, false);

	if (end_sep == NULL) {
		context->text += len;
	} else {
		context->text = end_sep + sizeof("+}") - 1;
	}
	context->type = LINE_DEFAULT;

	return true;
}

static bool
diff_common_read_diff_wdiff(struct view *view, const char *text)
{
	struct diff_stat_context context = { text, LINE_DEFAULT };

	/* Detect remaining part of a word diff line:
	 *
	 *	added {+new +} text
	 *	removed[- something -] from the file
	 *	replaced [-some-]{+same+} text
	 *	there could be [-one-] diff part{+s+} in the {+any +} line
	 */
	while (diff_common_read_diff_wdiff_group(&context))
		;

	diff_common_add_cell(&context, strlen(context.text), true);
	return diff_common_add_line(view, text, LINE_DEFAULT, &context);
}

static bool
diff_common_highlight(struct view *view, const char *text, enum line_type type)
{
	struct diff_stat_context context = { text, type, true };
	enum line_type hi_type = type == LINE_DIFF_ADD
				 ? LINE_DIFF_ADD_HIGHLIGHT : LINE_DIFF_DEL_HIGHLIGHT;
	const char *codes[] = { "\x1b[7m", "\x1b[27m" };
	const enum line_type types[] = { hi_type, type };
	int i;

	for (i = 0; diff_common_add_cell_until(&context, codes[i], types[i]); i = (i + 1) % 2)
		;

	diff_common_add_cell(&context, strlen(context.text), true);
	return diff_common_add_line(view, text, type, &context);
}

bool
diff_common_read(struct view *view, const char *data, struct diff_state *state)
{
	char stripped_buf[SIZEOF_STR];
	const char *clean_data = data;
	enum line_type type;
	bool indicator_stripped = false;

	/* If data contains ANSI codes (from bat pipe), strip them for
	 * line type detection, but keep original for rendering. */
	if (state->syntax_highlight && ansi_has_escapes(data)) {
		struct ansi_span spans[1];

		ansi_parse_line(data, stripped_buf, sizeof(stripped_buf), spans, 0);
		clean_data = stripped_buf;
	}

	type = get_line_type(clean_data);

	/* ADD2 and DEL2 are only valid in combined diff hunks */
	if (!state->combined_diff && (type == LINE_DIFF_ADD2 || type == LINE_DIFF_DEL2))
		type = LINE_DEFAULT;

	/* DEL_FILE, ADD_FILE and START are only valid outside diff chunks */
	if (state->reading_diff_chunk) {
		if (type == LINE_DIFF_DEL_FILE || type == LINE_DIFF_START)
			type = LINE_DIFF_DEL;
		else if (type == LINE_DIFF_ADD_FILE)
			type = LINE_DIFF_ADD;
	}

	if (!view->lines && type != LINE_COMMIT)
		state->reading_diff_stat = true;

	/* combined diffs lack LINE_DIFF_START and we don't know
	 * if this is a combined diff until we see a "@@@" */
	if (!state->after_diff && clean_data[0] == ' ' && clean_data[1] != ' ')
		state->reading_diff_stat = true;

	if (state->reading_diff_stat) {
		if (diff_common_add_diff_stat(view, clean_data, 0))
			return true;
		state->reading_diff_stat = false;

	} else if (type == LINE_DIFF_START) {
		state->reading_diff_stat = true;
	}

	if (!state->after_commit_title && !prefixcmp(clean_data, "    ")) {
		struct line *line = add_line_text(view, clean_data, LINE_DEFAULT);

		if (line)
			line->commit_title = 1;
		state->after_commit_title = true;
		return line != NULL;
	}

	/* Track filename from +++ headers for syntax highlighting */
	if (state->syntax_highlight && !state->reading_diff_chunk)
		diff_track_filename(state, clean_data, type);

	if (type == LINE_DIFF_HEADER) {
		state->after_diff = true;
		state->reading_diff_chunk = false;

	} else if (type == LINE_DIFF_CHUNK) {
		const unsigned int len = chunk_header_marker_length(clean_data);
		const char *context = strstr(clean_data + len, "@@");
		struct line *line =
			context ? add_line_text_at(view, view->lines, clean_data, LINE_DIFF_CHUNK, len)
				: NULL;
		struct box *box;

		if (!line)
			return false;

		box = line->data;
		box->cell[0].length = (context + len) - clean_data;
		box->cell[1].length = strlen(context + len);
		box->cell[box->cells++].type = LINE_DIFF_STAT;
		state->combined_diff = (len > 2);
		state->parents = len - 1;
		state->reading_diff_chunk = true;
		return true;

	} else if (type == LINE_COMMIT) {
		state->reading_diff_chunk = false;

	}

	if (opt_word_diff && state->reading_diff_chunk &&
	    /* combined diff format is not using word diff */
	    !state->combined_diff)
		return diff_common_read_diff_wdiff(view, clean_data);

	if (!opt_diff_indicator && state->reading_diff_chunk &&
		!state->stage) {
		data += state->parents;
		clean_data += state->parents;
		/* The +/- prefix is gone from `data` now, so the highlighter
		 * must not try to split it off a second time. */
		indicator_stripped = true;
	}

	/* Syntax highlight content lines via bat.
	 * Pass original data (not clean_data) so diff-highlight's
	 * reverse-video codes can be detected and preserved.
	 * ADD2/DEL2 are the added/removed lines of a combined diff, which
	 * is what git emits for every unmerged path. */
	if (state->syntax_highlight && state->reading_diff_chunk
		&& (type == LINE_DIFF_ADD || type == LINE_DIFF_ADD2
			|| type == LINE_DIFF_DEL || type == LINE_DIFF_DEL2
			|| type == LINE_DEFAULT)
		&& diff_syntax_highlight_line(view, data, type, state,
					      indicator_stripped))
		return true;

	if (strchr(data, 0x1b)) {
		/* diff-highlight: parse reverse-video codes */
		if (state->highlight)
			return diff_common_highlight(view, data, type);
	}

	return pager_common_read(view, clean_data, type, NULL);
}

static bool
diff_find_stat_entry(struct view *view, struct line *line, enum line_type type)
{
	struct line *marker = find_next_line_by_type(view, line, type);

	return marker &&
		line == find_prev_line_by_type(view, marker, LINE_DIFF_HEADER);
}

static struct line *
diff_find_header_from_stat(struct view *view, struct line *line)
{
	if (line->type == LINE_DIFF_STAT) {
		int file_number = 0;

		while (view_has_line(view, line) && line->type == LINE_DIFF_STAT) {
			file_number++;
			line--;
		}

		for (line = view->line; view_has_line(view, line); line++) {
			line = find_next_line_by_type(view, line, LINE_DIFF_HEADER);
			if (!line)
				break;

			if (diff_find_stat_entry(view, line, LINE_DIFF_INDEX)
			    || diff_find_stat_entry(view, line, LINE_DIFF_OLDMODE)
			    || diff_find_stat_entry(view, line, LINE_DIFF_SIMILARITY)) {
				if (file_number == 1) {
					break;
				}
				file_number--;
			}
		}

		return line;
	}

	return NULL;
}

enum request
diff_common_enter(struct view *view, enum request request, struct line *line)
{
	if (line->type == LINE_DIFF_STAT) {
		line = diff_find_header_from_stat(view, line);
		if (!line) {
			report("Failed to find file diff");
			return REQ_NONE;
		}

		select_view_line(view, line - view->line);
		report_clear();
		return REQ_NONE;

	} else {
		return pager_request(view, request, line);
	}
}

void
diff_save_line(struct view *view, struct diff_state *state, enum open_flags flags)
{
	if (flags & OPEN_RELOAD) {
		struct line *line = &view->line[view->pos.lineno];
		const char *file = view_has_line(view, line) ? diff_get_pathname(view, line, false) : NULL;

		if (file) {
			state->file = get_path(file);
			state->lineno = diff_get_lineno(view, line, false);
			state->pos = view->pos;
		}
	}
}

void
diff_restore_line(struct view *view, struct diff_state *state)
{
	struct line *line = &view->line[view->lines - 1];

	if (!state->file)
		return;

	while ((line = find_prev_line_by_type(view, line, LINE_DIFF_HEADER))) {
		const char *file = diff_get_pathname(view, line, false);

		if (file && !strcmp(file, state->file))
			break;
		line--;
	}

	state->file = NULL;

	if (!line)
		return;

	while ((line = find_next_line_by_type(view, line, LINE_DIFF_CHUNK))) {
		unsigned int lineno = diff_get_lineno(view, line, false);

		for (line++; view_has_line(view, line) && line->type != LINE_DIFF_CHUNK; line++) {
			if (lineno == state->lineno) {
				unsigned long lineno = line - view->line;
				unsigned long offset = lineno - (state->pos.lineno - state->pos.offset);

				goto_view_line(view, offset, lineno);
				redraw_view(view);
				return;
			}
			if (line->type != LINE_DIFF_DEL &&
			    line->type != LINE_DIFF_DEL2)
				lineno++;
		}
	}
}

static bool
diff_read_describe(struct view *view, struct buffer *buffer, struct diff_state *state)
{
	struct line *line = find_next_line_by_type(view, view->line, LINE_PP_REFS);

	if (line && buffer) {
		const char *ref = string_trim(buffer->data);
		const char *sep = !strcmp("Refs: ", box_text(line)) ? "" : ", ";

		if (*ref && !append_line_format(view, line, "%s%s", sep, ref))
			return false;
	}

	return true;
}

static bool
diff_read(struct view *view, struct buffer *buf, bool force_stop)
{
	struct diff_state *state = view->private;

	if (state->adding_describe_ref)
		return diff_read_describe(view, buf, state);

	if (!buf) {
		/* Syntax highlighting runs at draw time, so its state outlives
		 * the read and is released by the view's done hook. */
		if (!diff_done_highlight(state)) {
			if (!force_stop)
				report("Failed to run the diff-highlight program: %s", opt_diff_highlight);
			return false;
		}

		/* Fall back to retry if no diff will be shown. */
		if (view->lines == 0 && opt_file_args) {
			int pos = argv_size(view->argv)
				- argv_size(opt_file_args) - 1;

			if (pos > 0 && !strcmp(view->argv[pos], "--")) {
				for (; view->argv[pos]; pos++) {
					free((void *) view->argv[pos]);
					view->argv[pos] = NULL;
				}

				if (view->pipe)
					io_done(view->pipe);
				if (view_exec(view, 0))
					return false;
			}
		}

		if (view->env->blame_lineno) {
			state->file = get_path(view->env->file);
			state->lineno = view->env->blame_lineno;
			state->pos.offset = 0;
			state->pos.lineno = view->lines - 1;

			view->env->blame_lineno = 0;
		}

		diff_restore_line(view, state);

		if (!state->adding_describe_ref && !ref_list_contains_tag(view->vid)) {
			const char *describe_argv[] = { "git", "describe", "--tags", view->vid, NULL };
			enum status_code code = begin_update(view, NULL, describe_argv, OPEN_EXTRA);

			if (code != SUCCESS) {
				report("Failed to load describe data: %s", get_status_message(code));
				return true;
			}

			state->adding_describe_ref = true;
			return false;
		}

		return true;
	}

	return diff_common_read(view, buf->data, state);
}

static bool
diff_blame_line(const char *ref, const char *file, unsigned long lineno,
		struct blame_header *header, struct blame_commit *commit)
{
	char author[SIZEOF_STR] = "";
	char committer[SIZEOF_STR] = "";
	char line_arg[SIZEOF_STR];
	const char *blame_argv[] = {
		"git", "blame", encoding_arg, "-p", line_arg, ref, "--", file, NULL
	};
	struct io io;
	bool ok = false;
	struct buffer buf;

	if (!string_format(line_arg, "-L%lu,+1", lineno))
		return false;

	if (!io_run(&io, IO_RD, repo.exec_dir, NULL, blame_argv))
		return false;

	while (io_get(&io, &buf, '\n', true)) {
		if (header) {
			if (!parse_blame_header(header, buf.data))
				break;
			header = NULL;

		} else if (parse_blame_info(commit, author, committer, buf.data)) {
			ok = commit->filename != NULL;
			break;
		}
	}

	if (io_error(&io))
		ok = false;

	io_done(&io);
	return ok;
}

unsigned int
diff_get_lineno(struct view *view, struct line *line, bool old)
{
	const struct line *header, *chunk;
	unsigned int lineno;
	struct chunk_header chunk_header;

	/* Verify that we are after a diff header and one of its chunks */
	header = find_prev_line_by_type(view, line, LINE_DIFF_HEADER);
	chunk = find_prev_line_by_type(view, line, LINE_DIFF_CHUNK);
	if (!header || !chunk || chunk < header)
		return 0;

	/*
	 * In a chunk header, the number after the '+' sign is the number of its
	 * following line, in the new version of the file. We increment this
	 * number for each non-deletion line, until the given line position.
	 */
	if (!parse_chunk_header(&chunk_header, box_text(chunk)))
		return 0;

	lineno = old ? chunk_header.old.position : chunk_header.new.position;

	for (chunk++; chunk < line; chunk++)
		if (old ? chunk->type != LINE_DIFF_ADD && chunk->type != LINE_DIFF_ADD2
			: chunk->type != LINE_DIFF_DEL && chunk->type != LINE_DIFF_DEL2)
			lineno++;

	return lineno;
}

static enum request
diff_trace_origin(struct view *view, enum request request, struct line *line)
{
	struct line *commit_line = find_prev_line_by_type(view, line, LINE_COMMIT);
	char id[SIZEOF_REV];
	struct line *diff = find_prev_line_by_type(view, line, LINE_DIFF_HEADER);
	struct line *chunk = find_prev_line_by_type(view, line, LINE_DIFF_CHUNK);
	const char *chunk_data;
	int chunk_marker = line->type == LINE_DIFF_DEL ? '-' : '+';
	unsigned long lineno = 0;
	const char *file = NULL;
	char ref[SIZEOF_REF];
	struct blame_header header;
	struct blame_commit commit;

	if (!diff || !chunk || chunk == line || diff < commit_line) {
		report("The line to trace must be inside a diff chunk");
		return REQ_NONE;
	}

	file = diff_get_pathname(view, line, line->type == LINE_DIFF_DEL);

	if (!file) {
		report("Failed to read the file name");
		return REQ_NONE;
	}

	chunk_data = box_text(chunk);

	if (!parse_chunk_lineno(&lineno, chunk_data, chunk_marker)) {
		report("Failed to read the line number");
		return REQ_NONE;
	}

	if (lineno == 0) {
		report("This is the origin of the line");
		return REQ_NONE;
	}

	for (chunk += 1; chunk < line; chunk++) {
		if (chunk->type == LINE_DIFF_ADD) {
			lineno += chunk_marker == '+';
		} else if (chunk->type == LINE_DIFF_DEL) {
			lineno += chunk_marker == '-';
		} else {
			lineno++;
		}
	}

	if (commit_line)
		string_copy_rev_from_commit_line(id, box_text(commit_line));
	else
		string_copy(id, view->vid);

	if (chunk_marker == '-')
		string_format(ref, "%s^", id);
	else
		string_copy(ref, id);

	if (!diff_blame_line(ref, file, lineno, &header, &commit)) {
		report("Failed to read blame data");
		return REQ_NONE;
	}

	string_ncopy(view->env->file, commit.filename, strlen(commit.filename));
	string_copy_rev(request == REQ_VIEW_BLAME ? view->env->ref : view->env->commit, header.id);
	view->env->goto_lineno = header.orig_lineno - 1;

	return request;
}

const char *
diff_get_pathname(struct view *view, struct line *line, bool old)
{
	struct line *header;
	struct line *file;

	header = find_prev_line_in_commit_by_type(view, line, LINE_DIFF_HEADER);
	if (!header)
		return NULL;

	if (!old) {
		const char *dst;
		const char *prefixes[] = { "diff --cc ", "diff --combined " };
		int i;

		for (i = 0; i < ARRAY_SIZE(prefixes); i++) {
			dst = strstr(box_text(header), prefixes[i]);
			if (dst)
				return dst + strlen(prefixes[i]);
		}
	}

	file = find_next_line_in_diff_by_type(view, header + 1, old ? LINE_DIFF_DEL_FILE : LINE_DIFF_ADD_FILE);
	if (file) {
		const char *name;

		name = box_text(file);
		if (old ? !prefixcmp(name, "--- ") : !prefixcmp(name, "+++ "))
			name += STRING_SIZE("+++ ");

		if (opt_diff_noprefix)
			return name;

		/* Handle mnemonic prefixes, such as "b/" and "w/". */
		if (!prefixcmp(name, "a/") || !prefixcmp(name, "b/") || !prefixcmp(name, "i/") || !prefixcmp(name, "w/"))
			name += STRING_SIZE("a/");
		return name;
	}

	file = find_next_line_in_diff_by_type(view, header + 1, old ? LINE_DIFF_RENAME_FROM : LINE_DIFF_RENAME_TO);
	if (file) {
		const char *name;

		name = box_text(file);
		name += old ? STRING_SIZE("rename from ") : STRING_SIZE("rename to ");
		return name;
	}

	return NULL;
}

enum request
diff_common_edit(struct view *view, enum request request, struct line *line)
{
	const char *file;
	char path[SIZEOF_STR];
	unsigned int lineno;

	if (line->type == LINE_DIFF_STAT) {
		file = view->env->file;
		lineno = view->env->lineno;
	} else {
		file = diff_get_pathname(view, line, false);
		lineno = diff_get_lineno(view, line, false);
	}

	if (!file || !*file) {
		report("Nothing to edit");
		return REQ_NONE;
	}

	if (!string_concat_path(path, repo.cdup, file) || access(path, R_OK)) {
		report("Failed to open file: %s", file);
		return REQ_NONE;
	}

	open_editor(file, lineno);
	return REQ_NONE;
}

static enum request
diff_request(struct view *view, enum request request, struct line *line)
{
	switch (request) {
	case REQ_VIEW_BLAME:
	case REQ_VIEW_BLOB:
		if (line->type == LINE_DIFF_STAT) {
			string_copy_rev(request == REQ_VIEW_BLAME ? view->env->ref : view->env->commit, view->vid);
			view->env->goto_lineno = 0;
			return request;
		}
		return diff_trace_origin(view, request, line);

	case REQ_EDIT:
		return diff_common_edit(view, request, line);

	case REQ_ENTER:
		return diff_common_enter(view, request, line);

	case REQ_REFRESH:
		reload_view(view);
		return REQ_NONE;

	default:
		return pager_request(view, request, line);
	}
}

void
diff_common_select(struct view *view, struct line *line, const char *changes_msg)
{
	if (line->type == LINE_DIFF_STAT) {
		struct line *header = diff_find_header_from_stat(view, line);
		if (header) {
			const char *file = diff_get_pathname(view, header, false);

			if (file) {
				const char *old_file = diff_get_pathname(view, header, true);
				if (old_file)
					string_format(view->env->file_old, "%s", old_file);
				else
					view->env->file_old[0] = '\0';
				string_format(view->env->file, "%s", file);
				view->env->lineno = view->env->goto_lineno = 0;
				view->env->blob[0] = 0;
			}
		}

		string_format(view->ref, "Press '%s' to jump to file diff",
			      get_view_key(view, REQ_ENTER));
	} else {
		const char *file = diff_get_pathname(view, line, false);

		if (file) {
			const char *old_file = diff_get_pathname(view, line, true);
			if (old_file)
				string_format(view->env->file_old, "%s", old_file);
			else
				view->env->file_old[0] = '\0';
			if (changes_msg)
				string_format(view->ref, "%s to '%s'", changes_msg, file);
			string_format(view->env->file, "%s", file);
			view->env->lineno = view->env->goto_lineno = diff_get_lineno(view, line, false);
			if (view->env->goto_lineno > 0)
				view->env->goto_lineno--;
			view->env->lineno_old = diff_get_lineno(view, line, true);
			view->env->blob[0] = 0;
		} else {
			view->env->lineno = view->env->goto_lineno = (line - view->line) + 1;
			string_ncopy(view->ref, view->ops->id, strlen(view->ops->id));
		}
	}
	pager_select(view, line);
}

static void
diff_select(struct view *view, struct line *line)
{
	diff_common_select(view, line, "Changes");
}

static struct view_ops diff_ops = {
	"line",
	argv_env.commit,
	VIEW_DIFF_LIKE | VIEW_ADD_DESCRIBE_REF | VIEW_ADD_PAGER_REFS | VIEW_FILE_FILTER | VIEW_REFRESH | VIEW_FLEX_WIDTH,
	sizeof(struct diff_state),
	diff_open,
	diff_read,
	diff_draw,
	diff_request,
	view_column_grep,
	diff_select,
	diff_done,
	view_column_bit(LINE_NUMBER) | view_column_bit(TEXT),
	pager_get_column_data,
};

DEFINE_VIEW(diff);

/* vim: set ts=8 sw=8 noexpandtab: */
