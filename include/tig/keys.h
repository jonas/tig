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

#ifndef TIG_KEYS_H
#define TIG_KEYS_H

#include "tig/tig.h"
#include "tig/request.h"
#include "tig/util.h"

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
		bool control:1;
		bool multibytes:1;
	} modifiers;
};

static inline int
key_to_value(const struct key *key)
{
	return key->modifiers.multibytes ? 0 : key->data.value;
}

static inline unsigned long
key_to_unicode(const struct key *key)
{
	return key->modifiers.multibytes
		? utf8_to_unicode(key->data.bytes, strlen(key->data.bytes))
		: 0;
}

static inline char
key_to_control(const struct key *key)
{
	return (key->modifiers.control && key->modifiers.multibytes && strlen(key->data.bytes) == 1)
		? key->data.bytes[0]
		: 0;
}

struct keymap *get_keymap(const char *name, size_t namelen);

const char *get_key_name(const struct key key[], size_t keys, bool quote_comma);
enum status_code get_key_value(const char **name, struct key *key);

/* Looks for a key binding first in the given map, then in the generic map, and
 * lastly in the default keybindings. */
enum request get_keybinding(const struct keymap *keymap, const struct key key[], size_t keys, int *matches);
enum status_code add_keybinding(struct keymap *table, enum request request, const struct key key[], size_t keys);

const char *get_keys(const struct keymap *keymap, enum request request, bool all);
#define get_view_key(view, request) get_keys((view)->keymap, request, false)

struct run_request_flags {
	bool silent;
	bool confirm;
	bool exit;
	bool internal;
	bool echo;
	bool quick;
};

struct run_request {
	struct keymap *keymap;
	struct run_request_flags flags;
	const char **argv;
};

struct run_request *get_run_request(enum request request);
enum status_code add_run_request(struct keymap *keymap, const struct key key[], size_t keys, const char **argv);
enum status_code parse_run_request_flags(struct run_request_flags *flags, const char **argv);
const char *format_run_request_flags(const struct run_request *req);

typedef bool (*key_visitor_fn)(void *data, const char *group, struct keymap *keymap,
			       enum request request, const char *key,
			       const struct request_info *req_info, const struct run_request *run_req);
bool foreach_key(key_visitor_fn fn, void *data, bool combine_keys);

#endif
/* vim: set ts=8 sw=8 noexpandtab: */
