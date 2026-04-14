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

void
diff_init_syntax_highlight(struct diff_state *state)
{
	state->syntax_highlight = opt_syntax_highlight && *opt_syntax_highlight
				  && COLORS >= 256;
	state->syntax_file[0] = '\0';
	state->syntax_pid = 0;
	state->syntax_write_fd = -1;
	state->syntax_read_fp = NULL;
}

static void
syntax_pipe_close(struct diff_state *state)
{
	if (state->syntax_write_fd >= 0) {
		close(state->syntax_write_fd);
		state->syntax_write_fd = -1;
	}
	if (state->syntax_read_fp) {
		fclose(state->syntax_read_fp);
		state->syntax_read_fp = NULL;
	}
	if (state->syntax_pid > 0) {
		kill(state->syntax_pid, SIGTERM);
		waitpid(state->syntax_pid, NULL, 0);
		state->syntax_pid = 0;
	}
}

static bool
syntax_pipe_open(struct diff_state *state, const char *filename)
{
	int stdin_pipe[2], stdout_pipe[2];
	pid_t pid;
	char bat_path[SIZEOF_STR];
	const char *env_path;

	/* Close existing pipe if any */
	syntax_pipe_close(state);

	/* Find bat */
	env_path = getenv("PATH");
	if (!env_path || !*env_path)
		env_path = _PATH_DEFPATH;
	if (!path_search(bat_path, sizeof(bat_path), opt_syntax_highlight, env_path, X_OK))
		return false;

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
		char file_arg[SIZEOF_STR];

		close(stdin_pipe[1]);
		close(stdout_pipe[0]);
		dup2(stdin_pipe[0], STDIN_FILENO);
		dup2(stdout_pipe[1], STDOUT_FILENO);
		close(stdin_pipe[0]);
		close(stdout_pipe[1]);

		string_format(file_arg, "--file-name=%s", filename);
		execlp("stdbuf", "stdbuf", "-oL",
			bat_path, "--color=always", "--style=plain",
			"--paging=never", file_arg, "-", NULL);
		/* If stdbuf not available, try without */
		execlp(bat_path, bat_path, "--color=always", "--style=plain",
			"--paging=never", file_arg, "-", NULL);
		_exit(127);
	}

	/* Parent */
	close(stdin_pipe[0]);
	close(stdout_pipe[1]);

	state->syntax_pid = pid;
	state->syntax_write_fd = stdin_pipe[1];
	state->syntax_read_fp = fdopen(stdout_pipe[0], "r");
	if (!state->syntax_read_fp) {
		syntax_pipe_close(state);
		return false;
	}

	return true;
}

void
diff_done_syntax_highlight(struct diff_state *state)
{
	syntax_pipe_close(state);
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

		/* Open new bat pipe if file changed */
		if (strcmp(state->syntax_file, path)) {
			string_ncopy(state->syntax_file, path, strlen(path));
			syntax_pipe_open(state, state->syntax_file);
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

static bool
diff_syntax_highlight_line(struct view *view, const char *data,
			   enum line_type type, struct diff_state *state)
{
	char highlighted[SIZEOF_STR * 2] = "";
	char stripped[SIZEOF_STR];
	char full_line[SIZEOF_STR];
	struct ansi_span spans[ANSI_MAX_SPANS];
	int nspans, i;
	int diff_bg;
	struct ansi_color bg_color;
	struct line *line;
	struct box *box;
	size_t prefix_len = 0;
	const char *content = data;
	int total_cells;

	if (!state->syntax_read_fp || state->syntax_write_fd < 0)
		return false;

	/* Detect and strip the +/- prefix before sending to bat */
	if (type == LINE_DIFF_ADD || type == LINE_DIFF_ADD2 ||
		type == LINE_DIFF_DEL || type == LINE_DIFF_DEL2) {
		prefix_len = state->parents;
		content = data + prefix_len;
	} else if (data[0] == ' ') {
		/* Context line */
		prefix_len = state->parents;
		content = data + prefix_len;
	}

	/* Empty content (e.g. bare "+" or "-" line): skip bat,
	 * just create a line with the prefix and background fill */
	if (!*content) {
		diff_bg = diff_bg_color_for_type(type);
		bg_color.type = (diff_bg >= 0) ? ANSI_COLOR_256 : ANSI_COLOR_DEFAULT;
		bg_color.index = diff_bg;
		total_cells = 1;

		line = add_line_text_at(view, view->lines, data, type, total_cells);
		if (!line)
			return false;
		box = line->data;

		if (prefix_len > 0) {
			int pfx_fg = diff_prefix_fg_for_type(type);
			struct ansi_color pfx_fg_color;

			pfx_fg_color.type = (pfx_fg >= 0) ? ANSI_COLOR_BASIC : ANSI_COLOR_DEFAULT;
			pfx_fg_color.index = pfx_fg;

			memset(&box->cell[0], 0, sizeof(box->cell[0]));
			box->cell[0].type = type;
			box->cell[0].length = prefix_len;
			box->cell[0].direct = 1;
			box->cell[0].color_pair = get_dynamic_color_pair(&pfx_fg_color, &bg_color);
			box->cell[0].attr = A_BOLD;
		} else {
			memset(&box->cell[0], 0, sizeof(box->cell[0]));
			box->cell[0].type = type;
			box->cell[0].length = strlen(data);
			box->cell[0].direct = 1;
			box->cell[0].color_pair = get_dynamic_color_pair(
				&(struct ansi_color){ ANSI_COLOR_DEFAULT, { .index = 0 } }, &bg_color);
		}
		box->cells = total_cells;
		return true;
	}

	/* If diff-highlight added reverse-video codes, strip them before
	 * sending to bat, but record which byte ranges are emphasized */
	{
		char clean_content[SIZEOF_STR];
		struct ansi_span dh_spans[ANSI_MAX_SPANS];
		int dh_nspans = 0;
		bool has_emphasis = ansi_has_escapes(content);

		if (has_emphasis) {
			dh_nspans = ansi_parse_line(content, clean_content,
							sizeof(clean_content),
							dh_spans, ANSI_MAX_SPANS);
			content = clean_content;
		}

		/* Write clean content (no ANSI) to bat's stdin */
		{
			size_t content_len = strlen(content);

			if (write(state->syntax_write_fd, content, content_len) < 0 ||
				write(state->syntax_write_fd, "\n", 1) < 0) {
				syntax_pipe_close(state);
				return false;
			}
		}

		/* Read highlighted line from bat's stdout */
		if (!fgets(highlighted, sizeof(highlighted), state->syntax_read_fp)) {
			syntax_pipe_close(state);
			return false;
		}

		/* Strip trailing newline */
		{
			size_t len = strlen(highlighted);
			if (len > 0 && highlighted[len - 1] == '\n')
				highlighted[len - 1] = '\0';
		}

		/* Parse ANSI from bat's output */
		nspans = ansi_parse_line(highlighted, stripped, sizeof(stripped),
					 spans, ANSI_MAX_SPANS);
		if (nspans <= 0)
			return false;

		/* Merge diff-highlight emphasis: split syntax spans at
		 * emphasis boundaries so only the changed characters
		 * get the brighter background */
		if (has_emphasis && dh_nspans > 0) {
			int emph_bg = diff_bg_emphasis_for_type(type);
			struct ansi_span merged[ANSI_MAX_SPANS];
			int nmerged = 0;

			/* Build a reverse-video bitmap */
			char is_emph[SIZEOF_STR] = {0};
			int j;

			for (j = 0; j < dh_nspans; j++) {
				if (!(dh_spans[j].attr & A_REVERSE))
					continue;
				size_t k;
				for (k = dh_spans[j].offset;
					k < dh_spans[j].offset + dh_spans[j].length
					&& k < sizeof(is_emph); k++)
					is_emph[k] = 1;
			}

			/* Split each syntax span at emphasis boundaries */
			for (i = 0; i < nspans && nmerged < ANSI_MAX_SPANS - 1; i++) {
				size_t pos = spans[i].offset;
				size_t end = pos + spans[i].length;

				while (pos < end && nmerged < ANSI_MAX_SPANS - 1) {
					bool emph = pos < sizeof(is_emph) && is_emph[pos];
					size_t run = pos;

					/* Find run of same emphasis state */
					while (run < end && run < sizeof(is_emph)
						&& (is_emph[run] ? 1 : 0) == emph)
						run++;
					if (run >= sizeof(is_emph))
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
			nspans = nmerged;
		}
	}

	/* Reconstruct full line: prefix + highlighted content */
	if (prefix_len > 0) {
		memcpy(full_line, data, prefix_len);
		memcpy(full_line + prefix_len, stripped, strlen(stripped) + 1);
		/* Adjust span offsets to account for prefix */
		for (i = 0; i < nspans; i++)
			spans[i].offset += prefix_len;
	} else {
		memcpy(full_line, stripped, strlen(stripped) + 1);
	}

	/* Determine background color for this diff line type */
	diff_bg = diff_bg_color_for_type(type);
	bg_color.type = (diff_bg >= 0) ? ANSI_COLOR_256 : ANSI_COLOR_DEFAULT;
	bg_color.index = diff_bg;

	/*
	 * Build cells and wrap long lines GitHub-style:
	 * - First line: prefix (+/-) + content, up to view width
	 * - Continuation lines: 2-space indent + content, marked as wrapped
	 */
	{
		const char *text_ptr = full_line;
		size_t text_remaining = strlen(full_line);
		int wrap_indent = 2;  /* spaces for continuation indent */
		bool first_line = true;
		bool has_first = false;
		unsigned int lineno = 0;

		/* Pre-compute color pairs for all spans + prefix */
		struct box_cell all_cells[ANSI_MAX_SPANS + 1];
		int ncells = 0;

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
		}

		for (i = 0; i < nspans; i++) {
			memset(&all_cells[ncells], 0, sizeof(all_cells[0]));
			all_cells[ncells].type = type;
			all_cells[ncells].length = spans[i].length;
			all_cells[ncells].direct = 1;
			all_cells[ncells].color_pair = get_dynamic_color_pair(&spans[i].fg,
				spans[i].bg.type != ANSI_COLOR_DEFAULT ? &spans[i].bg : &bg_color);
			all_cells[ncells].attr = spans[i].attr;
			ncells++;
		}

		/* Check total text width */
		{
			int total_width = 0;
			const char *p = full_line;
			while (*p) {
				if (*p == '\t')
					total_width += opt_tab_size - (total_width % opt_tab_size);
				else
					total_width++;
				p++;
			}

			/* If it fits in one line, just create a single line */
			if (total_width <= view->width) {
				line = add_line_text_at(view, view->lines, full_line, type, ncells);
				if (!line)
					return false;
				box = line->data;
				memcpy(box->cell, all_cells, sizeof(all_cells[0]) * ncells);
				box->cells = ncells;
				return true;
			}
		}

		/* Wrap: split text into chunks that fit view width */
		int cell_idx = 0;
		size_t cell_offset = 0;  /* bytes consumed within current cell */

		while (text_remaining > 0 && cell_idx < ncells) {
			int avail_width = view->width;
			char line_buf[SIZEOF_STR];
			size_t line_len = 0;
			int line_width = 0;
			struct box_cell line_cell_buf[ANSI_MAX_SPANS + 2];
			int line_ncells = 0;

			/* On continuation lines, add a colored indent
			 * (we handle this ourselves instead of using
			 * line->wrapped to keep the diff background) */
			if (!first_line) {
				int indent = wrap_indent < avail_width ? wrap_indent : 1;

				/* Leading spaces for indent */
				memset(line_buf, ' ', indent);
				line_len = indent;
				line_width = indent;

				/* Indent cell with diff background */
				memset(&line_cell_buf[0], 0, sizeof(line_cell_buf[0]));
				line_cell_buf[0].type = type;
				line_cell_buf[0].length = indent;
				line_cell_buf[0].direct = 1;
				line_cell_buf[0].color_pair = get_dynamic_color_pair(
					&(struct ansi_color){ ANSI_COLOR_DEFAULT, { .index = 0 } },
					&bg_color);
				line_ncells = 1;
			}

			/* Fill line with text from cells until we hit the width */
			while (cell_idx < ncells && line_width < avail_width) {
				size_t cell_remaining = all_cells[cell_idx].length - cell_offset;
				const char *cell_text = text_ptr;
				size_t bytes_to_take = 0;
				int width_taken = 0;

				/* Measure how much of this cell fits */
				for (size_t b = 0; b < cell_remaining && line_width + width_taken < avail_width; b++) {
					if (cell_text[b] == '\t')
						width_taken += opt_tab_size - ((line_width + width_taken) % opt_tab_size);
					else
						width_taken++;
					bytes_to_take = b + 1;
				}

				if (bytes_to_take > 0 && line_len + bytes_to_take < sizeof(line_buf) - 1) {
					memcpy(line_buf + line_len, cell_text, bytes_to_take);
					line_len += bytes_to_take;
					line_width += width_taken;
					text_ptr += bytes_to_take;
					text_remaining -= bytes_to_take;

					/* Add a cell for this chunk */
					memset(&line_cell_buf[line_ncells], 0, sizeof(line_cell_buf[0]));
					line_cell_buf[line_ncells].type = all_cells[cell_idx].type;
					line_cell_buf[line_ncells].length = bytes_to_take;
					line_cell_buf[line_ncells].direct = 1;
					line_cell_buf[line_ncells].color_pair = all_cells[cell_idx].color_pair;
					line_cell_buf[line_ncells].attr = all_cells[cell_idx].attr;
					line_ncells++;

					cell_offset += bytes_to_take;
					if (cell_offset >= all_cells[cell_idx].length) {
						cell_idx++;
						cell_offset = 0;
					}
				} else {
					break;
				}
			}

			if (line_len == 0)
				break;

			line_buf[line_len] = '\0';

			/* Create the view line */
			line = add_line_text_at_(view, view->lines, line_buf, line_len,
						 type, line_ncells, false);
			if (!line)
				return false;

			box = line->data;
			memcpy(box->cell, line_cell_buf, sizeof(line_cell_buf[0]) * line_ncells);
			box->cells = line_ncells;

			if (!has_first) {
				has_first = true;
				lineno = line->lineno;
			}

			line->lineno = lineno;
			first_line = false;
		}

		return has_first;
	}
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
	char *pipe = strchr(data, '|');

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
	}

	/* Syntax highlight content lines via bat.
	 * Pass original data (not clean_data) so diff-highlight's
	 * reverse-video codes can be detected and preserved. */
	if (state->syntax_highlight && state->reading_diff_chunk
		&& (type == LINE_DIFF_ADD || type == LINE_DIFF_DEL || type == LINE_DEFAULT)
		&& diff_syntax_highlight_line(view, data, type, state))
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
		diff_done_syntax_highlight(state);
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
	const char *dst;
	const char *prefixes[] = { "diff --cc ", "diff --combined " };
	const char *name;
	int i;

	header = find_prev_line_in_commit_by_type(view, line, LINE_DIFF_HEADER);
	if (!header)
		return NULL;

	if (!old) {
		for (i = 0; i < ARRAY_SIZE(prefixes); i++) {
			dst = strstr(box_text(header), prefixes[i]);
			if (dst)
				return dst + strlen(prefixes[i]);
		}
	}

	header = find_next_line_by_type(view, header, old ? LINE_DIFF_DEL_FILE : LINE_DIFF_ADD_FILE);
	if (!header)
		return NULL;

	name = box_text(header);
	if (old ? !prefixcmp(name, "--- ") : !prefixcmp(name, "+++ "))
		name += STRING_SIZE("+++ ");

	if (opt_diff_noprefix)
		return name;

	/* Handle mnemonic prefixes, such as "b/" and "w/". */
	if (!prefixcmp(name, "a/") || !prefixcmp(name, "b/") || !prefixcmp(name, "i/") || !prefixcmp(name, "w/"))
		name += STRING_SIZE("a/");
	return name;
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
	view_column_draw,
	diff_request,
	view_column_grep,
	diff_select,
	NULL,
	view_column_bit(LINE_NUMBER) | view_column_bit(TEXT),
	pager_get_column_data,
};

DEFINE_VIEW(diff);

/* vim: set ts=8 sw=8 noexpandtab: */
