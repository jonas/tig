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

#ifndef TIG_DISPLAY_H
#define TIG_DISPLAY_H

#include "tig/tig.h"
#include "tig/keys.h"

int get_input(int prompt_position, struct key *key);
int get_input_char(void);

extern WINDOW *status_win;

void update_status(const char *msg, ...) PRINTF_LIKE(1, 2);
void update_status_with_context(const char *context, const char *msg, ...) PRINTF_LIKE(2, 3);
void report(const char *msg, ...) PRINTF_LIKE(1, 2);
void report_clear(void);

/*
 * Display management.
 */

/* The display array of active views and the index of the current view. */
extern struct view *display[2];
extern unsigned int current_view;

#define foreach_displayed_view(view, i) \
	for (i = 0; i < ARRAY_SIZE(display) && (view = display[i]); i++)

#define displayed_views()	(!!display[0] + !!display[1])

#define view_is_displayed(view) \
	(view == display[0] || view == display[1])

void init_tty(void);
void init_display(void);
void resize_display(void);
void redraw_display(bool clear);
bool save_display(const char *path);
bool save_view(struct view *view, const char *path);

bool vertical_split_is_enabled(enum vertical_split vsplit, int height, int width);
int apply_vertical_split(int base_width);

bool open_external_viewer(const char *argv[], const char *dir, bool silent, bool confirm, bool echo, bool quick, bool refresh, const char *notice);
void open_editor(const char *file, unsigned int lineno);
void enable_mouse(bool enable);

enum status_code open_script(const char *path);
bool is_script_executing(void);

#define get_cursor_pos(cursor_y, cursor_x) getyx(newscr, cursor_y, cursor_x)
#define set_cursor_pos(cursor_y, cursor_x) wmove(newscr, cursor_y, cursor_x)

#endif
/* vim: set ts=8 sw=8 noexpandtab: */
