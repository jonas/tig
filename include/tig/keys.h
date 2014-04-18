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

#ifndef TIG_KEYS_H
#define TIG_KEYS_H

#include "tig/tig.h"
#include "tig/request.h"

/*
 * Keys
 */

struct keybinding;

struct keymap {
	const char *name;
	struct keybinding **data;
	size_t size;
	bool hidden;
};

struct key {
	union {
		int value;
		char bytes[7];
	} data;
	struct {
		bool escape:1;
		bool control:1;
		bool multibytes:1;
	} modifiers;
};

static inline unsigned long
key_input_to_unicode(struct key *key)
{
	return key->modifiers.multibytes
		? utf8_to_unicode(key->data.bytes, strlen(key->data.bytes))
		: 0;
}

struct keymap *get_keymap(const char *name, size_t namelen);
struct keymap *get_keymap_by_index(int i);

const char *get_key_name(const struct key key[], size_t keys);
int get_key_value(const char **name, struct key *key);

/* Looks for a key binding first in the given map, then in the generic map, and
 * lastly in the default keybindings. */
enum request get_keybinding(struct keymap *keymap, struct key key[], size_t keys);
enum status_code add_keybinding(struct keymap *table, enum request request, struct key key[], size_t keys);

const char *get_keys(struct keymap *keymap, enum request request, bool all);
#define get_view_key(view, request) get_keys((view)->keymap, request, FALSE)

struct run_request_flags {
	bool silent;
	bool confirm;
	bool exit;
	bool internal;
};

struct run_request {
	struct keymap *keymap;
	struct run_request_flags flags;
	const char **argv;
};

struct run_request *get_run_request(enum request request);
enum status_code add_run_request(struct keymap *keymap, struct key key[], size_t keys, const char **argv);

#endif
/* vim: set ts=8 sw=8 noexpandtab: */
