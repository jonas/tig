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

#ifndef TIG_PROMPT_H
#define TIG_PROMPT_H

#include "tig/tig.h"
#include "tig/keys.h"

struct view;

struct menu_item {
	int hotkey;
	const char *text;
	void *data;
};

char *read_prompt(const char *prompt);
void prompt_init(void);
bool prompt_yesno(const char *prompt);
bool prompt_menu(const char *prompt, const struct menu_item *items, int *selected);

enum request run_prompt_command(struct view *view, const char *argv[]);
enum request open_prompt(struct view *view);

#endif
/* vim: set ts=8 sw=8 noexpandtab: */
