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

#include "tig/util.h"
#include "tig/parse.h"
#include "tig/repo.h"
#include "tig/prompt.h"
#include "tig/display.h"
#include "tig/view.h"
#include "tig/ui.h"

struct file_finder_line {
	size_t matches;
	char text[1];
};

DEFINE_ALLOCATOR(realloc_file_array, struct file_finder_line *, 256)

struct file_finder {
	WINDOW *win;
	int height, width;

	struct file_finder_line **file;

	struct file_finder_line **line;
	size_t lines;
	struct position pos;

	struct keymap *keymap;
	const char **search;
	size_t searchlen;
};

static bool
file_finder_read(struct file_finder *finder, const char *commit)
{
	const char *tree = string_rev_is_null(commit) ? "HEAD" : commit;
	const char *ls_tree_files_argv[] = {
		"git", "ls-tree", "-z", "-r", "--name-only", "--full-name",
			tree, NULL
	};
	struct buffer buf;
	struct io io;
	size_t files;
	bool ok = true;

	if (!io_run(&io, IO_RD, repo.exec_dir, NULL, ls_tree_files_argv))
		return false;

	for (files = 0; io_get(&io, &buf, 0, true); files++) {
		/* Alloc two to ensure NULL terminated array. */
		if (!realloc_file_array(&finder->file, files, 2)) {
			ok = false;
			break;
		}

		finder->file[files] = calloc(1, sizeof(struct file_finder_line) + buf.size);
		if (!finder->file[files]) {
			ok = false;
			break;
		}

		strncpy(finder->file[files]->text, buf.data, buf.size);
	}

	if (io_error(&io) || !realloc_file_array(&finder->line, 0, files + 1))
		ok = false;
	io_done(&io);
	return ok;
}

static void
file_finder_done(struct file_finder *finder)
{
	int i;

	free(finder->line);
	if (finder->file) {
		for (i = 0; finder->file[i]; i++)
			free(finder->file[i]);
		free(finder->file);
	}

	if (finder->win)
		delwin(finder->win);
}

static void
file_finder_move(struct file_finder *finder, int direction)
{
	if (direction < 0 && finder->pos.lineno <= -direction)
		finder->pos.lineno = 0;
	else
		finder->pos.lineno += direction;

	if (finder->pos.lineno >= finder->lines) {
		if (finder->lines > 0)
			finder->pos.lineno = finder->lines - 1;
		else
			finder->pos.lineno = 0;
        }

	if (finder->pos.offset + finder->height <= finder->pos.lineno)
		finder->pos.offset = finder->pos.lineno - (finder->height / 2);

	if (finder->pos.offset > finder->pos.lineno)
		finder->pos.offset = finder->pos.lineno;

	if (finder->lines <= finder->height)
		finder->pos.offset = 0;
}

static void
file_finder_draw_line(struct file_finder *finder, struct file_finder_line *line)
{
	const char **search = finder->search;
	const char *text = line->text;
	const char *pos;

	for (; *text && search && *search && (pos = strstr(text, *search)); search++) {
		if (text < pos)
			waddnstr(finder->win, text, pos - text);
		wattron(finder->win, A_STANDOUT);
		waddnstr(finder->win, pos, 1);
		wattroff(finder->win, A_STANDOUT);
		text = pos + 1;
	}

	if (*text)
		waddstr(finder->win, text);
}

static void
file_finder_draw(struct file_finder *finder)
{
	struct position *pos = &finder->pos;
	struct file_finder_line *current_line = finder->line[pos->lineno];
	struct file_finder_line **line_pos = &finder->line[pos->offset];
	int column;

	wbkgdset(finder->win, get_line_attr(NULL, LINE_DEFAULT));
	werase(finder->win);

	for (column = 0; *line_pos && column < finder->height - 1; line_pos++) {
		struct file_finder_line *line = *line_pos;

		if (finder->searchlen != line->matches)
			continue;

		wmove(finder->win, column++, 0);
		if (line == current_line) {
			wbkgdset(finder->win, get_line_attr(NULL, LINE_CURSOR));
		}
		file_finder_draw_line(finder, line);
		if (line == current_line) {
			wclrtoeol(finder->win);
			wbkgdset(finder->win, get_line_attr(NULL, LINE_DEFAULT));
		}
	}

	wmove(finder->win, finder->height - 1, 0);
	wbkgdset(finder->win, get_line_attr(NULL, LINE_TITLE_FOCUS));
	wprintw(finder->win, "[finder] file %d of %d", pos->lineno + 1, finder->lines);
	wclrtoeol(finder->win);
	wrefresh(finder->win);
}

static size_t
file_finder_line_matches(struct file_finder_line *line, const char **search)
{
	const char *text = line->text;
	const char *pos;
	size_t matches = 0;

	for (; *text && *search && (pos = strstr(text, *search)); search++) {
		text = pos + strlen(*search);
		matches++;
	}

	return matches;
}

static void
file_finder_update(struct file_finder *finder)
{
	struct file_finder_line *current = finder->line[finder->pos.lineno];
	size_t new_lineno = 0;
	int i;

	memset(finder->line, 0, sizeof(*finder->line) * finder->lines);
	finder->lines = 0;

	for (i = 0; finder->file && finder->file[i]; i++) {
		struct file_finder_line *line = finder->file[i];

		if (line == current)
			current = NULL;

		if (line->matches + 1 < finder->searchlen) {
			continue;
		}

		if (line->matches >= finder->searchlen) {
			line->matches = finder->searchlen;
		} else {
			line->matches = file_finder_line_matches(line, finder->search);
			if (line->matches < finder->searchlen)
				continue;
		}

		if (current != NULL)
			new_lineno++;

		finder->line[finder->lines++] = line;
	}

	finder->pos.lineno = new_lineno;
}

static enum input_status
file_finder_input_handler(struct input *input, struct key *key)
{
	struct file_finder *finder = input->data;
	enum input_status status;

	status = prompt_default_handler(input, key);
	if (status == INPUT_DELETE) {
		if (finder->searchlen > 0) {
			finder->searchlen--;
			free((void *) finder->search[finder->searchlen]);
			finder->search[finder->searchlen] = NULL;
		}
		file_finder_update(finder);
		file_finder_move(finder, 0);
		file_finder_draw(finder);
		return status;
	}

	if (status != INPUT_SKIP)
		return status;

	switch (get_keybinding(finder->keymap, key, 1, NULL)) {
	case REQ_FIND_PREV:
		file_finder_move(finder, -1);
		file_finder_draw(finder);
		return INPUT_SKIP;

	case REQ_FIND_NEXT:
		file_finder_move(finder, +1);
		file_finder_draw(finder);
		return INPUT_SKIP;

	case REQ_BACK:
	case REQ_PARENT:
	case REQ_VIEW_CLOSE:
	case REQ_VIEW_CLOSE_NO_QUIT:
		return INPUT_CANCEL;

	default:
		if (key_to_value(key) == 0) {
			argv_append(&finder->search, key->data.bytes);
			finder->searchlen++;
			file_finder_update(finder);
			file_finder_move(finder, 0);
			file_finder_draw(finder);
			return INPUT_OK;
		}

		/* Catch all non-multibyte keys. */
		return INPUT_SKIP;
	}
}

const char *
open_file_finder(const char *commit)
{
	struct file_finder finder = {0};
	const char *file = NULL;

	if (!file_finder_read(&finder, commit)) {
		file_finder_done(&finder);
		return false;
	}

	getmaxyx(stdscr, finder.height, finder.width);
	finder.height--;
	finder.win = newwin(finder.height, finder.width, 0, 0);
	if (!finder.win) {
		file_finder_done(&finder);
		return false;
	}

	finder.keymap = get_keymap("search", STRING_SIZE("search")),
	file_finder_update(&finder);
	file_finder_draw(&finder);
	if (read_prompt_incremental("Find file: ", false, true, file_finder_input_handler, &finder) && finder.pos.lineno < finder.lines)
		file = get_path(finder.line[finder.pos.lineno]->text);

	file_finder_done(&finder);
	redraw_display(true);
	return file;
}

/* vim: set ts=8 sw=8 noexpandtab: */
