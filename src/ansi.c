/* Copyright (c) 2006-2015 Jonas Fonseca <jonas.fonseca@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
 * GNU General Public License for more details.
 */

#include "tig/ansi.h"
#include "tig/draw.h"
#include "tig/line.h"
#include "tig/tig.h"
#include "tig/view.h"
#include "compat/utf8proc.h"

void
split_ansi(const char *string, int *ansi_num, char **ansi_ptrs) {
	char *head_of_ansi = "\033[";
	int current_ansi_idx = 0;
	char *next_ansi_ptr = strstr(string + current_ansi_idx, head_of_ansi);

	if (next_ansi_ptr == NULL)
		return;
	while (next_ansi_ptr != NULL) {
		if (strcmp(string, next_ansi_ptr) == 0) {
			next_ansi_ptr = strstr(string + current_ansi_idx + strlen(head_of_ansi), head_of_ansi);
			continue;
		}
		int current_ansi_length = strlen(string + current_ansi_idx) - strlen(next_ansi_ptr);
		strncpy(ansi_ptrs[*ansi_num], string + current_ansi_idx, current_ansi_length);
		ansi_ptrs[*ansi_num][current_ansi_length] = '\0';
		*ansi_num += 1;
		current_ansi_idx += current_ansi_length / sizeof(char);
		next_ansi_ptr = strstr(string + current_ansi_idx + strlen(head_of_ansi), head_of_ansi);
	}

	strcpy(ansi_ptrs[*ansi_num], string + current_ansi_idx);
	*ansi_num += 1;
}

void
draw_ansi(struct view *view, int *ansi_num, char **ansi_ptrs, int max_width, size_t skip) {
	static struct ansi_status cur_ansi_status;
	cur_ansi_status.fg = COLOR_WHITE;
	cur_ansi_status.bg = COLOR_BLACK;
	cur_ansi_status.attr = A_NORMAL;
	int cur_width = 0;

	for (int i = 0; i < *ansi_num; i++) {
		if (cur_width >= view->width)
			break;

		int len = strlen(ansi_ptrs[i]);
		char text[len + 1];
		strcpy(text, ansi_ptrs[i]);

		if (i == 0 && text[0] != '\033') {
			waddnstr(view->win, text, len);
			continue;
		}
		// delta won't add moving ansi codes which are
		// A, B, C, D, E, F, G, H, f, S, T.
		// J, K exists for filling lines with a color, but ncurses can't do.
		if ((text[3] == 'J') || (text[3] == 'K'))
			continue;

		char *ansi_end_ptr = strchr(text, 'm');
		int after_ansi_len = strlen(ansi_end_ptr);
		int ansi_code_len = len - after_ansi_len - 2;
		ansi_end_ptr += 1;

		int widths_of_display = utf8_width_of(ansi_end_ptr, after_ansi_len, after_ansi_len);
		if (skip > widths_of_display) {
			skip -= widths_of_display;
			continue;
		}

		if (view->curline->selected) {
			draw_ansi_line(view, ansi_end_ptr, after_ansi_len, &skip, &cur_width, &widths_of_display);
			cur_width += widths_of_display;
			continue;
		}

		char *ansi_code = malloc(sizeof(char) * (ansi_code_len + 1));
		strncpy(ansi_code, text + 2, ansi_code_len);
		ansi_code[ansi_code_len] = '\0';
		if (strchr(ansi_code, ';') == NULL) {
			if (strcmp(ansi_code, "0") == 0)
				cur_ansi_status.attr = A_NORMAL;
			if (strcmp(ansi_code, "1") == 0)
				cur_ansi_status.attr = A_BOLD;
			if (strcmp(ansi_code, "2") == 0)
				cur_ansi_status.attr = A_DIM;
			if (strcmp(ansi_code, "3") == 0)
				cur_ansi_status.attr = A_ITALIC;
			if (strcmp(ansi_code, "4") == 0)
				cur_ansi_status.attr = A_UNDERLINE;
			if (strcmp(ansi_code, "5") == 0)
				cur_ansi_status.attr = A_BLINK;
			if (strcmp(ansi_code, "6") == 0)
				cur_ansi_status.attr = A_BLINK; // This is supposed to be faster than normal blink, but ncurses doesn't have any way to achieve.
			if (strcmp(ansi_code, "7") == 0)
				cur_ansi_status.attr = A_REVERSE;
			if (strcmp(ansi_code, "8") == 0)
				cur_ansi_status.attr = A_INVIS;
			if (strcmp(ansi_code, "9") == 0)
				// This is supposed to be strikethrough, but ncurses doesn't have any way to achieve.
			if (strcmp(ansi_code, "30") == 0)
				cur_ansi_status.fg = COLOR_BLACK;
			if (strcmp(ansi_code, "31") == 0)
				cur_ansi_status.fg = COLOR_RED;
			if (strcmp(ansi_code, "32") == 0)
				cur_ansi_status.fg = COLOR_GREEN;
			if (strcmp(ansi_code, "33") == 0)
				cur_ansi_status.fg = COLOR_YELLOW;
			if (strcmp(ansi_code, "34") == 0)
				cur_ansi_status.fg = COLOR_BLUE;
			if (strcmp(ansi_code, "35") == 0)
				cur_ansi_status.fg = COLOR_MAGENTA;
			if (strcmp(ansi_code, "36") == 0)
				cur_ansi_status.fg = COLOR_CYAN;
			if (strcmp(ansi_code, "37") == 0)
				cur_ansi_status.fg = COLOR_WHITE;
			if (strcmp(ansi_code, "40") == 0)
				cur_ansi_status.bg = COLOR_BLACK;
			if (strcmp(ansi_code, "41") == 0)
				cur_ansi_status.bg = COLOR_RED;
			if (strcmp(ansi_code, "42") == 0)
				cur_ansi_status.bg = COLOR_GREEN;
			if (strcmp(ansi_code, "43") == 0)
				cur_ansi_status.bg = COLOR_YELLOW;
			if (strcmp(ansi_code, "44") == 0)
				cur_ansi_status.bg = COLOR_BLUE;
			if (strcmp(ansi_code, "45") == 0)
				cur_ansi_status.bg = COLOR_MAGENTA;
			if (strcmp(ansi_code, "46") == 0)
				cur_ansi_status.bg = COLOR_CYAN;
			if (strcmp(ansi_code, "47") == 0)
				cur_ansi_status.bg = COLOR_WHITE;
		} else {
			char *token = malloc(sizeof(char) * (ansi_code_len + 1));
			strcpy(token, ansi_code);
			char *ansi_code_part = strtok(token, ";");

			while (ansi_code_part != NULL) {
				char *color_method_mark = strtok(NULL, ";");
				if (strcmp(color_method_mark, "5") == 0) {
					char *c256 = strtok(NULL, ";");
					if (strcmp(ansi_code_part, "38") == 0)
						cur_ansi_status.fg = atoi(c256);
					if (strcmp(ansi_code_part, "48") == 0)
						cur_ansi_status.bg = atoi(c256);
				}
				// WONTFIX: You can't init_color with numerous RGB code in ncurses.
				// I decided to force delta users to use "true-color = never" when using tig,
				// so the process never comes to this condition.
				// I leave the code for someone who wants to implements in the future.
				// if (strcmp(color_method_mark, "2") == 0) {
					// char *r = strtok(NULL, ";");
					// char *g = strtok(NULL, ";");
					// char *b = strtok(NULL, ";");
				// }
				ansi_code_part = strtok(NULL, ";");
			}
			free(token);
			token = NULL;
		}
		wattrset_by_ansi_status(view, &cur_ansi_status);

		draw_ansi_line(view, ansi_end_ptr, after_ansi_len, &skip, &cur_width, &widths_of_display);
		cur_width += widths_of_display;

		free(ansi_code);
		ansi_code = NULL;
	}
}

void
draw_ansi_line(struct view *view, char *ansi_end_ptr, int after_ansi_len, size_t *skip, int *cur_width, int *widths_of_display) {
	while (*skip > 0) {
		utf8proc_int32_t unicode;
		int bytes_to_skip = utf8proc_iterate((const utf8proc_uint8_t *) ansi_end_ptr, strlen(ansi_end_ptr), &unicode);
		ansi_end_ptr += bytes_to_skip;
		after_ansi_len -= bytes_to_skip;
		*skip -= 1;
		*widths_of_display -= 1;
	}

	if (*cur_width + *widths_of_display > view->width) {
		int left_widths = view->width - *cur_width;
		while (left_widths > 0) {
			utf8proc_int32_t unicode;
			int bytes_to_display = utf8proc_iterate((const utf8proc_uint8_t *) ansi_end_ptr, strlen(ansi_end_ptr), &unicode);
			waddnstr(view->win, ansi_end_ptr, bytes_to_display);
			ansi_end_ptr += bytes_to_display;
			after_ansi_len -= bytes_to_display;
			left_widths -= 1;
		}
	} else {
		waddnstr(view->win, ansi_end_ptr, after_ansi_len);
	}
}

void
wattrset_by_ansi_status(struct view *view, struct ansi_status* cur_ansi_status) {
	// Because init_extended_pair can't accept more than 32768 pairs,
	// we skip the colors with color codes odd numbered and greater than 15 currently.
	if (cur_ansi_status->fg > 15 && cur_ansi_status->fg % 2 == 1)
		cur_ansi_status->fg -= 1;
	if (cur_ansi_status->bg > 15 && cur_ansi_status->bg % 2 == 1)
		cur_ansi_status->bg -= 1;
	short id = color_pairs_map[cur_ansi_status->fg][cur_ansi_status->bg];
	wattr_set(view->win, cur_ansi_status->attr, id, NULL);
}

/* vim: set ts=8 sw=8 noexpandtab: */
