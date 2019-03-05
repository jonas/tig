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

#include "tig/tig.h"
#include "tig/graph.h"
#include "tig/draw.h"
#include "tig/options.h"
#include "compat/hashtab.h"

static const enum line_type palette_colors[] = {
	LINE_PALETTE_0,
	LINE_PALETTE_1,
	LINE_PALETTE_2,
	LINE_PALETTE_3,
	LINE_PALETTE_4,
	LINE_PALETTE_5,
	LINE_PALETTE_6,
	LINE_PALETTE_7,
	LINE_PALETTE_8,
	LINE_PALETTE_9,
	LINE_PALETTE_10,
	LINE_PALETTE_11,
	LINE_PALETTE_12,
	LINE_PALETTE_13,
};

/*
 * View drawing.
 */

static inline void
set_view_attr(struct view *view, enum line_type type)
{
	if (!view->curline->selected && view->curtype != type) {
		(void) wattrset(view->win, get_view_attr(view, type));
		wchgat(view->win, -1, 0, get_view_color(view, type), NULL);
#if defined(NCURSES_VERSION_PATCH) && NCURSES_VERSION_PATCH < 20061217
		touchwin(view->win);
#endif
		view->curtype = type;
	}
}

#define VIEW_MAX_LEN(view) ((view)->width + (view)->pos.col - (view)->col)

static bool
draw_chars(struct view *view, enum line_type type, const char *string, int length,
	   int max_width, bool use_tilde)
{
	int len = 0;
	int col = 0;
	int trimmed = false;
	size_t skip = view->pos.col > view->col ? view->pos.col - view->col : 0;

	if (max_width <= 0)
		return VIEW_MAX_LEN(view) <= 0;

	len = utf8_length(&string, length, skip, &col, max_width, &trimmed, use_tilde, opt_tab_size);

	if (opt_iconv_out != ICONV_NONE) {
		string = encoding_iconv(opt_iconv_out, string, len);
		if (!string)
			return VIEW_MAX_LEN(view) <= 0;
	}

	set_view_attr(view, type);
	if (len > 0)
	{
		if (view_has_flags(view, VIEW_DIFF_LIKE) &&
		    !opt_diff_show_signs &&
		    view->col == 0 &&
		    (type == LINE_DIFF_ADD || type == LINE_DIFF_DEL || type == LINE_DEFAULT) &&
		    (string[0] == ' ' || string[0] == '+' || string[0] == '-')
		   ) {
			if (opt_diff_column_highlight == DIFF_COLUMN_HIGHLIGHT_ALL) {
				if (type == LINE_DIFF_ADD) {
					set_view_attr(view, LINE_DIFF_ADD_HIGHLIGHT);
				} else if (type == LINE_DIFF_DEL) {
					set_view_attr(view, LINE_DIFF_DEL_HIGHLIGHT);
				}
				waddch(view->win, ' ');
				set_view_attr(view, type);
			}
			else if (opt_diff_column_highlight == DIFF_COLUMN_HIGHLIGHT_ONLY_EMPTY && len == 1) {
				if (type == LINE_DIFF_ADD) {
					set_view_attr(view, LINE_DIFF_ADD_HIGHLIGHT);
					waddch(view->win, ' ');
				} else if (type == LINE_DIFF_DEL) {
					set_view_attr(view, LINE_DIFF_DEL_HIGHLIGHT);
					waddch(view->win, ' ');
				}

				set_view_attr(view, type);
			}

			waddnstr(view->win, string+1, len-1);
		} else {
			waddnstr(view->win, string, len);
		}
	}

	if (trimmed && use_tilde) {
		set_view_attr(view, LINE_DELIMITER);
		waddstr(view->win, opt_truncation_delimiter ? opt_truncation_delimiter : "~");
		col++;
	}

	view->col += col;
	return VIEW_MAX_LEN(view) <= 0;
}

static bool
draw_space(struct view *view, enum line_type type, int max, int spaces)
{
	static char space[] = "                    ";

	spaces = MIN(max, spaces);

	while (spaces > 0) {
		int len = MIN(spaces, sizeof(space) - 1);

		if (draw_chars(view, type, space, -1, len, false))
			return true;
		spaces -= len;
	}

	return VIEW_MAX_LEN(view) <= 0;
}

static bool
draw_text_expanded(struct view *view, enum line_type type, const char *string, int length, int max_width, bool use_tilde)
{
	static char text[SIZEOF_STR];

	if (length == -1)
		length = strlen(string);

	do {
		size_t pos = string_expand(text, sizeof(text), string, length, opt_tab_size);
		size_t col = view->col;

		if (draw_chars(view, type, text, -1, max_width, use_tilde))
			return true;
		string += pos;
		length -= pos;
		max_width -= view->col - col;
	} while (*string && length > 0);

	return VIEW_MAX_LEN(view) <= 0;
}

static inline bool
draw_textn(struct view *view, enum line_type type, const char *string, int length)
{
	return draw_text_expanded(view, type, string, length, VIEW_MAX_LEN(view), false);
}

bool
draw_text(struct view *view, enum line_type type, const char *string)
{
	return draw_textn(view, type, string, -1);
}

static bool
draw_text_overflow(struct view *view, const char *text, enum line_type type,
		   int overflow_length, int offset)
{
	bool on = overflow_length > 0;

	if (on) {
		int overflow = overflow_length + offset;
		int max = MIN(VIEW_MAX_LEN(view), overflow);
		const char *tmp = text;
		int text_width = 0;
		int trimmed = false;
		size_t len = utf8_length(&tmp, -1, 0, &text_width, max, &trimmed, false, 1);

		if (draw_text_expanded(view, type, text, -1, text_width, max < overflow))
			return true;

		text += len;
		type = LINE_OVERFLOW;
	}

	if (*text && draw_text(view, type, text))
		return true;

	return VIEW_MAX_LEN(view) <= 0;
}

bool PRINTF_LIKE(3, 4)
draw_formatted(struct view *view, enum line_type type, const char *format, ...)
{
	char text[SIZEOF_STR];
	int retval;

	FORMAT_BUFFER(text, sizeof(text), format, retval, true);
	return retval >= 0 ? draw_text(view, type, text) : VIEW_MAX_LEN(view) <= 0;
}

bool
draw_graphic(struct view *view, enum line_type type, const chtype graphic[], size_t size, bool separator)
{
	size_t skip = view->pos.col > view->col ? view->pos.col - view->col : 0;
	int max = VIEW_MAX_LEN(view);
	int i;

	if (max < size)
		size = max;

	set_view_attr(view, type);
	/* Using waddch() instead of waddnstr() ensures that
	 * they'll be rendered correctly for the cursor line. */
	for (i = skip; i < size; i++)
		waddch(view->win, graphic[i]);

	view->col += size;
	if (separator) {
		if (size < max && skip <= size)
			waddch(view->win, ' ');
		view->col++;
	}

	return VIEW_MAX_LEN(view) <= 0;
}

bool
draw_field(struct view *view, enum line_type type, const char *text, int width, enum align align, bool trim)
{
	int max = MIN(VIEW_MAX_LEN(view), width + 1);
	int col = view->col;

	if (!text)
		return draw_space(view, type, max, max);

	if (align == ALIGN_RIGHT) {
		int textlen = utf8_width_max(text, max);
		int leftpad = max - textlen - 1;

		if (leftpad > 0) {
			if (draw_space(view, type, leftpad, leftpad))
				return true;
			max -= leftpad;
			col += leftpad;;
		}
	}

	return draw_chars(view, type, text, -1, max - 1, trim)
	    || draw_space(view, type, max - (view->col - col), max);
}

static bool
draw_date(struct view *view, struct view_column *column, const struct time *time)
{
	struct date_options *opt = &column->opt.date;
	enum date date = opt->display;
	const char *text = mkdate(time, date, opt->local, opt->format);
	enum align align = date == DATE_RELATIVE ? ALIGN_RIGHT : ALIGN_LEFT;

	if (date == DATE_NO)
		return false;

	return draw_field(view, LINE_DATE, text, column->width, align, false);
}

static bool
draw_author(struct view *view, struct view_column *column, const struct ident *author)
{
	bool trim = author_trim(column->width);
	const char *text = mkauthor(author, MAX(column->opt.author.width, column->opt.author.maxwidth),
				    column->opt.author.display);

	if (column->opt.author.display == AUTHOR_NO)
		return false;

	return draw_field(view, LINE_AUTHOR, text, column->width, ALIGN_LEFT, trim);
}

static bool
draw_id(struct view *view, struct view_column *column, const char *id)
{
	enum line_type type = LINE_ID;

	if (!column->opt.id.display)
		return false;

	if (column->opt.id.color && id) {
		hashval_t color = iterative_hash(id, SIZEOF_REV - 1, 0);

		type = palette_colors[color % ARRAY_SIZE(palette_colors)];
	}

	return draw_field(view, type, id, column->width, ALIGN_LEFT, false);
}

static bool
draw_filename(struct view *view, struct view_column *column, const char *filename, mode_t mode)
{
	size_t width = filename ? utf8_width(filename) : 0;
	bool trim = width >= column->width;
	enum line_type type = S_ISDIR(mode) ? LINE_DIRECTORY : LINE_FILE;
	int column_width = column->width ? column->width : width;

	if (column->opt.file_name.display == FILENAME_NO)
		return false;

	return draw_field(view, type, filename, column_width, ALIGN_LEFT, trim);
}

static bool
draw_file_size(struct view *view, struct view_column *column, unsigned long size, mode_t mode)
{
	const char *str = S_ISDIR(mode) ? NULL : mkfilesize(size, column->opt.file_size.display);

	if (!column->width || column->opt.file_size.display == FILE_SIZE_NO)
		return false;

	return draw_field(view, LINE_FILE_SIZE, str, column->width, ALIGN_RIGHT, false);
}

static bool
draw_mode(struct view *view, struct view_column *column, mode_t mode)
{
	const char *str = mkmode(mode);

	if (!column->width || !column->opt.mode.display)
		return false;

	return draw_field(view, LINE_MODE, str, column->width, ALIGN_LEFT, false);
}

static bool
draw_lineno_custom(struct view *view, struct view_column *column, unsigned int lineno)
{
	char number[10];
	unsigned long digits3 = MIN(9,MAX(3,column->width));
	int max = MIN(VIEW_MAX_LEN(view), digits3);
	char *text = NULL;
	chtype separator = opt_line_graphics ? ACS_VLINE : '|';
	struct line_number_options *opts = &column->opt.line_number;
	int interval = opts->interval > 0 ? opts->interval : 5;

	if (!column->opt.line_number.display)
		return false;

	if (lineno == 1 || (lineno % interval) == 0) {
		static char fmt[] = "%ld";

		fmt[1] = '0' + digits3;
		if (string_format(number, fmt, lineno))
			text = number;
	}
	if (text)
		draw_chars(view, LINE_LINE_NUMBER, text, -1, max, true);
	else
		draw_space(view, LINE_LINE_NUMBER, max, digits3);
	return draw_graphic(view, LINE_DEFAULT, &separator, 1, true);
}

bool
draw_lineno(struct view *view, struct view_column *column, unsigned int lineno, bool add_offset)
{
	lineno += 1;
	if (add_offset)
		lineno += view->pos.offset;
	return draw_lineno_custom(view, column, lineno);
}

static bool
draw_ref(struct view *view, struct view_column *column, const struct ref *ref)
{
	enum line_type type = !ref || !ref->valid ? LINE_DEFAULT : get_line_type_from_ref(ref);
	const char *name = ref ? ref->name : NULL;

	return draw_field(view, type, name, column->width, ALIGN_LEFT, false);
}

static bool
draw_refs(struct view *view, struct view_column *column, const struct ref *refs)
{
	if (!column->opt.commit_title.refs || !refs)
		return false;

	for (; refs; refs = refs->next) {
		const struct ref *ref = refs;
		enum line_type type = get_line_type_from_ref(ref);
		const struct ref_format *format = get_ref_format(opt_reference_format, ref);

		if (!strcmp(format->start, "hide:") && !*format->end)
			continue;

		if (draw_formatted(view, type, "%s%s%s", format->start, ref->name, format->end))
			return true;

		if (draw_text(view, LINE_DEFAULT, " "))
			return true;
	}

	return false;
}

static bool
draw_status(struct view *view, struct view_column *column,
	    enum line_type type, const char *status)
{
	const char *label = mkstatus(status ? *status : 0, column->opt.status.display);

	return draw_field(view, type, label, column->width, ALIGN_LEFT, false);
}

/*
 * Revision graph
 */

static inline enum line_type
get_graph_color(int color_id)
{
	if (color_id == GRAPH_COMMIT_COLOR)
		return LINE_GRAPH_COMMIT;
	assert(color_id < ARRAY_SIZE(palette_colors));
	return palette_colors[color_id];
}

static bool
draw_graph_utf8(void *view, const struct graph *graph, const struct graph_symbol *symbol, int color_id, bool first)
{
	const char *chars = graph->symbol_to_utf8(symbol);

	return draw_text(view, get_graph_color(color_id), chars + !!first);
}

static bool
draw_graph_ascii(void *view, const struct graph *graph, const struct graph_symbol *symbol, int color_id, bool first)
{
	const char *chars = graph->symbol_to_ascii(symbol);

	return draw_text(view, get_graph_color(color_id), chars + !!first);
}

static bool
draw_graph_chtype(void *view, const struct graph *graph, const struct graph_symbol *symbol, int color_id, bool first)
{
	const chtype *chars = graph->symbol_to_chtype(symbol);

	return draw_graphic(view, get_graph_color(color_id), chars + !!first, 2 - !!first, false);
}

static bool
draw_graph(struct view *view, const struct graph *graph, const struct graph_canvas *canvas)
{
	static const graph_symbol_iterator_fn fns[] = {
		draw_graph_ascii,
		draw_graph_chtype,
		draw_graph_utf8
	};

	graph->foreach_symbol(graph, canvas, fns[opt_line_graphics], view);
	return draw_text(view, LINE_DEFAULT, " ");
}

static bool
draw_commit_title(struct view *view, struct view_column *column,
		  const struct graph *graph, const struct graph_canvas *graph_canvas,
		  const struct ref *refs, const char *commit_title)
{
	if (graph && graph_canvas && column->opt.commit_title.graph &&
	    draw_graph(view, graph, graph_canvas))
		return true;
	if (draw_refs(view, column, refs))
		return true;
	return draw_text_overflow(view, commit_title, LINE_DEFAULT,
			column->opt.commit_title.overflow, 0);
}

bool
view_column_draw(struct view *view, struct line *line, unsigned int lineno)
{
	struct view_column *column = view->columns;
	struct view_column_data column_data = {0};

	if (!view->ops->get_column_data(view, line, &column_data))
		return true;

	if (column_data.section)
		column = column_data.section;

	for (; column; column = column->next) {
		mode_t mode = column_data.mode ? *column_data.mode : 0;

		if (column->hidden)
			continue;

		switch (column->type) {
		case VIEW_COLUMN_DATE:
			if (draw_date(view, column, column_data.date))
				return true;
			continue;

		case VIEW_COLUMN_AUTHOR:
			if (draw_author(view, column, column_data.author))
				return true;
			continue;

		case VIEW_COLUMN_REF:
			if (draw_ref(view, column, column_data.ref))
				return true;
			continue;

		case VIEW_COLUMN_ID:
			if (draw_id(view, column, column_data.reflog ? column_data.reflog : column_data.id))
				return true;
			continue;

		case VIEW_COLUMN_LINE_NUMBER:
			/* Avoid corrupting line numbers (which actually are search results)
			 * in grep mode by special-treating that view. */
			if (draw_lineno(view, column,
			                column_data.line_number ? *column_data.line_number : lineno,
			                !view_has_flags(view, VIEW_GREP_LIKE)))
				return true;
			continue;

		case VIEW_COLUMN_MODE:
			if (draw_mode(view, column, mode))
				return true;
			continue;

		case VIEW_COLUMN_FILE_SIZE:
			if (draw_file_size(view, column, column_data.file_size ? *column_data.file_size : 0, mode))
				return true;
			continue;

		case VIEW_COLUMN_COMMIT_TITLE:
			if (draw_commit_title(view, column, column_data.graph, column_data.graph_canvas,
					      column_data.refs, column_data.commit_title))
				return true;
			continue;

		case VIEW_COLUMN_FILE_NAME:
			if (draw_filename(view, column, column_data.file_name, mode))
				return true;
			continue;

		case VIEW_COLUMN_SECTION:
			if (draw_text(view, column->opt.section.type, column->opt.section.text))
				return true;
			continue;

		case VIEW_COLUMN_STATUS:
			if (draw_status(view, column, line->type, column_data.status))
				return true;
			continue;

		case VIEW_COLUMN_TEXT:
		{
			enum line_type type = line->type;
			const char *text = column_data.text;

			if (line->wrapped && draw_text(view, LINE_DELIMITER, "+"))
				return true;

			if (line->graph_indent) {
				size_t indent = get_graph_indent(text);

				if (draw_text_expanded(view, LINE_DEFAULT, text, -1, indent, false))
					return true;
				text += indent;
			}

			if (line->commit_title) {
				if (draw_text_overflow(view, text, LINE_DEFAULT,
						       column->opt.text.commit_title_overflow, 4))
					return true;

			} else if (column_data.box) {
				const struct box *box = column_data.box;
				const char *text = box->text;
				size_t i;

				for (i = 0; i < box->cells; i++) {
					const struct box_cell *cell = &box->cell[i];

					if (draw_textn(view, cell->type, text, cell->length))
						return true;

					text += cell->length;
				}

			} else if (draw_text(view, type, text)) {
				return true;
			}
		}
			continue;
		}
	}

	return true;
}

static void
draw_view_line_search_result(struct view *view, unsigned int lineno)
{
	size_t bufsize = view->width * 4;
	char *buf = malloc(bufsize + 1);
	regmatch_t pmatch[1];
	regoff_t bufpos = 0;

	if (!buf || mvwinnstr(view->win, lineno, 0, buf, bufsize) == ERR) {
		free(buf);
		return;
	}

	while (bufpos < bufsize && !regexec(view->regex, buf + bufpos, ARRAY_SIZE(pmatch), pmatch, 0)) {
		regoff_t start = pmatch[0].rm_so;
		regoff_t end = pmatch[0].rm_eo;

		if (start == -1 || end <= 0 || end <= start)
			break;

		mvwchgat(view->win, lineno,
			 utf8_width_of(buf, bufpos + start, -1),
			 utf8_width_of(buf + bufpos + start, end - start, -1),
			 get_view_attr(view, LINE_SEARCH_RESULT),
			 get_view_color(view, LINE_SEARCH_RESULT),
			 NULL);

		bufpos += end;
	}

	free(buf);
}

bool
draw_view_line(struct view *view, unsigned int lineno)
{
	struct line *line;
	bool selected = (view->pos.offset + lineno == view->pos.lineno);
	bool ok;

	/* FIXME: Disabled during code split.
	assert(view_is_displayed(view));
	*/

	if (view->pos.offset + lineno >= view->lines)
		return false;

	line = &view->line[view->pos.offset + lineno];

	wmove(view->win, lineno, 0);
	if (line->cleareol)
		wclrtoeol(view->win);
	view->col = 0;
	view->curline = line;
	view->curtype = LINE_NONE;
	line->selected = false;
	line->dirty = line->cleareol = 0;

	if (selected) {
		set_view_attr(view, LINE_CURSOR);
		line->selected = true;
		view->ops->select(view, line);
	}

	ok = view->ops->draw(view, line, lineno);

	if (ok && line->search_result && view->regex)
		draw_view_line_search_result(view, lineno);

	return ok;
}

void
redraw_view_dirty(struct view *view)
{
	bool dirty = false;
	int lineno;

	for (lineno = 0; lineno < view->height; lineno++) {
		if (view->pos.offset + lineno >= view->lines)
			break;
		if (!view->line[view->pos.offset + lineno].dirty)
			continue;
		dirty = true;
		if (!draw_view_line(view, lineno))
			break;
	}

	if (!dirty)
		return;
	wnoutrefresh(view->win);
}

void
redraw_view_from(struct view *view, int lineno)
{
	assert(0 <= lineno && lineno < view->height);

	if (view->columns && view_column_info_changed(view, false)) {
		int i;

		view_column_reset(view);
		for (i = 0; i < view->lines; i++) {
			view_column_info_update(view, &view->line[i]);
		}
	}

	for (; lineno < view->height; lineno++) {
		if (!draw_view_line(view, lineno))
			break;
	}

	wnoutrefresh(view->win);
}

void
redraw_view(struct view *view)
{
	werase(view->win);
	redraw_view_from(view, 0);
}

/* vim: set ts=8 sw=8 noexpandtab: */
