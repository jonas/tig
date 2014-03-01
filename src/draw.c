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

#include "tig.h"
#include "draw.h"
#include "options.h"

/*
 * View drawing.
 */

static inline void
set_view_attr(struct view *view, enum line_type type)
{
	if (!view->curline->selected && view->curtype != type) {
		(void) wattrset(view->win, get_view_attr(view, type));
		wchgat(view->win, -1, 0, get_view_color(view, type), NULL);
		view->curtype = type;
	}
}

#define VIEW_MAX_LEN(view) ((view)->width + (view)->pos.col - (view)->col)

static bool
draw_chars(struct view *view, enum line_type type, const char *string,
	   int max_len, bool use_tilde)
{
	int len = 0;
	int col = 0;
	int trimmed = FALSE;
	size_t skip = view->pos.col > view->col ? view->pos.col - view->col : 0;

	if (max_len <= 0)
		return VIEW_MAX_LEN(view) <= 0;

	if (opt_iconv_out != ICONV_NONE) {
		string = encoding_iconv(opt_iconv_out, string);
		if (!string)
			return VIEW_MAX_LEN(view) <= 0;
	}

	len = utf8_length(&string, skip, &col, max_len, &trimmed, use_tilde, opt_tab_size);

	set_view_attr(view, type);
	if (len > 0) {
		waddnstr(view->win, string, len);

		if (trimmed && use_tilde) {
			set_view_attr(view, LINE_DELIMITER);
			waddch(view->win, '~');
			col++;
		}
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

		if (draw_chars(view, type, space, len, FALSE))
			return TRUE;
		spaces -= len;
	}

	return VIEW_MAX_LEN(view) <= 0;
}

static bool
draw_text_expanded(struct view *view, enum line_type type, const char *string, int max_len, bool use_tilde)
{
	static char text[SIZEOF_STR];

	do {
		size_t pos = string_expand(text, sizeof(text), string, opt_tab_size);

		if (draw_chars(view, type, text, max_len, use_tilde))
			return TRUE;
		string += pos;
	} while (*string);

	return VIEW_MAX_LEN(view) <= 0;
}

bool
draw_text(struct view *view, enum line_type type, const char *string)
{
	return draw_text_expanded(view, type, string, VIEW_MAX_LEN(view), TRUE);
}

bool
draw_text_overflow(struct view *view, const char *text, bool on, int overflow, enum line_type type)
{
	if (on) {
		int max = MIN(VIEW_MAX_LEN(view), overflow);
		int len = strlen(text);

		if (draw_text_expanded(view, type, text, max, max < overflow))
			return TRUE;

		text = len > overflow ? text + overflow : "";
		type = LINE_OVERFLOW;
	}

	if (*text && draw_text(view, type, text))
		return TRUE;

	return VIEW_MAX_LEN(view) <= 0;
}

bool PRINTF_LIKE(3, 4)
draw_formatted(struct view *view, enum line_type type, const char *format, ...)
{
	char text[SIZEOF_STR];
	int retval;

	FORMAT_BUFFER(text, sizeof(text), format, retval, TRUE);
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
		int textlen = strlen(text);
		int leftpad = max - textlen - 1;

		if (leftpad > 0) {
	    		if (draw_space(view, type, leftpad, leftpad))
				return TRUE;
			max -= leftpad;
			col += leftpad;;
		}
	}

	return draw_chars(view, type, text, max - 1, trim)
	    || draw_space(view, LINE_DEFAULT, max - (view->col - col), max);
}

bool
draw_date(struct view *view, struct time *time)
{
	const char *date = mkdate(time, opt_show_date);
	int cols = opt_show_date == DATE_SHORT ? DATE_SHORT_WIDTH : DATE_WIDTH;

	if (opt_show_date == DATE_NO)
		return FALSE;

	return draw_field(view, LINE_DATE, date, cols, ALIGN_LEFT, FALSE);
}

bool
draw_author(struct view *view, const struct ident *author)
{
	bool trim = author_trim(opt_author_width);
	const char *text = mkauthor(author, opt_author_width, opt_show_author);

	if (opt_show_author == AUTHOR_NO)
		return FALSE;

	return draw_field(view, LINE_AUTHOR, text, opt_author_width, ALIGN_LEFT, trim);
}

bool
draw_id_custom(struct view *view, enum line_type type, const char *id, int width)
{
	return draw_field(view, type, id, width, ALIGN_LEFT, FALSE);
}

bool
draw_id(struct view *view, const char *id)
{
	if (!opt_show_id)
		return FALSE;

	return draw_id_custom(view, LINE_ID, id, opt_id_width);
}

bool
draw_filename(struct view *view, const char *filename, bool auto_enabled)
{
	bool trim = filename && strlen(filename) >= opt_show_filename_width;

	if (opt_show_filename == FILENAME_NO)
		return FALSE;

	if (opt_show_filename == FILENAME_AUTO && !auto_enabled)
		return FALSE;

	return draw_field(view, LINE_FILENAME, filename, opt_show_filename_width, ALIGN_LEFT, trim);
}

bool
draw_file_size(struct view *view, unsigned long size, int width, bool pad)
{
	const char *str = pad ? NULL : mkfilesize(size, opt_show_file_size);

	if (!width || opt_show_file_size == FILE_SIZE_NO)
		return FALSE;

	return draw_field(view, LINE_FILE_SIZE, str, width, ALIGN_RIGHT, FALSE);
}

bool
draw_mode(struct view *view, mode_t mode)
{
	const char *str = mkmode(mode);

	return draw_field(view, LINE_MODE, str, STRING_SIZE("-rw-r--r--"), ALIGN_LEFT, FALSE);
}

bool
draw_lineno(struct view *view, unsigned int lineno)
{
	char number[10];
	int digits3 = view->digits < 3 ? 3 : view->digits;
	int max = MIN(VIEW_MAX_LEN(view), digits3);
	char *text = NULL;
	chtype separator = opt_line_graphics ? ACS_VLINE : '|';

	if (!opt_show_line_numbers)
		return FALSE;

	lineno += view->pos.offset + 1;
	if (lineno == 1 || (lineno % opt_line_number_interval) == 0) {
		static char fmt[] = "%1ld";

		fmt[1] = '0' + (view->digits <= 9 ? digits3 : 1);
		if (string_format(number, fmt, lineno))
			text = number;
	}
	if (text)
		draw_chars(view, LINE_LINE_NUMBER, text, max, TRUE);
	else
		draw_space(view, LINE_LINE_NUMBER, max, digits3);
	return draw_graphic(view, LINE_DEFAULT, &separator, 1, TRUE);
}

bool
draw_refs(struct view *view, struct ref_list *refs)
{
	size_t i;

	if (!opt_show_refs || !refs)
		return FALSE;

	for (i = 0; i < refs->size; i++) {
		struct ref *ref = refs->refs[i];
		enum line_type type = get_line_type_from_ref(ref);

		if (draw_formatted(view, type, "[%s]", ref->name))
			return TRUE;

		if (draw_text(view, LINE_DEFAULT, " "))
			return TRUE;
	}

	return FALSE;
}

bool
draw_view_line(struct view *view, unsigned int lineno)
{
	struct line *line;
	bool selected = (view->pos.offset + lineno == view->pos.lineno);

	/* FIXME: Disabled during code split.
	assert(view_is_displayed(view));
	*/

	if (view->pos.offset + lineno >= view->lines)
		return FALSE;

	line = &view->line[view->pos.offset + lineno];

	wmove(view->win, lineno, 0);
	if (line->cleareol)
		wclrtoeol(view->win);
	view->col = 0;
	view->curline = line;
	view->curtype = LINE_NONE;
	line->selected = FALSE;
	line->dirty = line->cleareol = 0;

	if (selected) {
		set_view_attr(view, LINE_CURSOR);
		line->selected = TRUE;
		view->ops->select(view, line);
	}

	return view->ops->draw(view, line, lineno);
}

void
redraw_view_dirty(struct view *view)
{
	bool dirty = FALSE;
	int lineno;

	for (lineno = 0; lineno < view->height; lineno++) {
		if (view->pos.offset + lineno >= view->lines)
			break;
		if (!view->line[view->pos.offset + lineno].dirty)
			continue;
		dirty = TRUE;
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
