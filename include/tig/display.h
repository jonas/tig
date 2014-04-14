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

#ifndef TIG_DISPLAY_H
#define TIG_DISPLAY_H

#include "tig/tig.h"
#include "tig/keys.h"

enum input_status {
	INPUT_OK,
	INPUT_SKIP,
	INPUT_STOP,
	INPUT_CANCEL
};

int get_input(int prompt_position, struct key_input *input, bool modifiers);

extern WINDOW *status_win;
extern FILE *opt_tty;

void update_status(const char *msg, ...);
void report(const char *msg, ...) PRINTF_LIKE(1, 2);
#define report_clear() report("%s", "")

/*
 * Display management.
 */

/* The display array of active views and the index of the current view. */
extern struct view *display[2];
extern unsigned int current_view;

#define foreach_displayed_view(view, i) \
	for (i = 0; i < ARRAY_SIZE(display) && (view = display[i]); i++)

#define displayed_views()	(display[1] != NULL ? 2 : 1)

#define view_is_displayed(view) \
	(view == display[0] || view == display[1])

void init_display(void);
void resize_display(void);
void redraw_display(bool clear);

bool open_external_viewer(const char *argv[], const char *dir, bool confirm, const char *notice);
void open_editor(const char *file, unsigned int lineno);

#endif
/* vim: set ts=8 sw=8 noexpandtab: */
