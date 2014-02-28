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
#include "types.h"
#include "argv.h"
#include "io.h"
#include "keys.h"
#include "util.h"

struct keybinding {
	int alias;
	enum request request;
};

static struct keymap generic_keymap = { "generic" };
#define is_generic_keymap(keymap) ((keymap) == &generic_keymap)

static struct keymap *keymaps = &generic_keymap;

struct keymap *
get_keymaps(void)
{
	return keymaps;
}

void
add_keymap(struct keymap *keymap)
{
	keymap->next = keymaps;
	keymaps = keymap;
}

struct keymap *
get_keymap(const char *name)
{
	struct keymap *keymap = keymaps;

	while (keymap) {
		if (!strcasecmp(keymap->name, name))
			return keymap;
		keymap = keymap->next;
	}

	return NULL;
}


void
add_keybinding(struct keymap *table, enum request request, int key)
{
	size_t i;

	for (i = 0; i < table->size; i++) {
		if (table->data[i].alias == key) {
			table->data[i].request = request;
			return;
		}
	}

	table->data = realloc(table->data, (table->size + 1) * sizeof(*table->data));
	if (!table->data)
		die("Failed to allocate keybinding");
	table->data[table->size].alias = key;
	table->data[table->size++].request = request;
}

/* Looks for a key binding first in the given map, then in the generic map, and
 * lastly in the default keybindings. */
enum request
get_keybinding(struct keymap *keymap, int key)
{
	size_t i;

	for (i = 0; i < keymap->size; i++)
		if (keymap->data[i].alias == key)
			return keymap->data[i].request;

	for (i = 0; i < generic_keymap.size; i++)
		if (generic_keymap.data[i].alias == key)
			return generic_keymap.data[i].request;

	return (enum request) key;
}


struct key {
	const char *name;
	int value;
};

static const struct key key_table[] = {
	{ "Enter",	KEY_RETURN },
	{ "Space",	' ' },
	{ "Backspace",	KEY_BACKSPACE },
	{ "Tab",	KEY_TAB },
	{ "Escape",	KEY_ESC },
	{ "Left",	KEY_LEFT },
	{ "Right",	KEY_RIGHT },
	{ "Up",		KEY_UP },
	{ "Down",	KEY_DOWN },
	{ "Insert",	KEY_IC },
	{ "Delete",	KEY_DC },
	{ "Hash",	'#' },
	{ "Home",	KEY_HOME },
	{ "End",	KEY_END },
	{ "PageUp",	KEY_PPAGE },
	{ "PgUp",	KEY_PPAGE },
	{ "PageDown",	KEY_NPAGE },
	{ "PgDown",	KEY_NPAGE },
	{ "F1",		KEY_F(1) },
	{ "F2",		KEY_F(2) },
	{ "F3",		KEY_F(3) },
	{ "F4",		KEY_F(4) },
	{ "F5",		KEY_F(5) },
	{ "F6",		KEY_F(6) },
	{ "F7",		KEY_F(7) },
	{ "F8",		KEY_F(8) },
	{ "F9",		KEY_F(9) },
	{ "F10",	KEY_F(10) },
	{ "F11",	KEY_F(11) },
	{ "F12",	KEY_F(12) },
};

int
get_key_value(const char *name)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(key_table); i++)
		if (!strcasecmp(key_table[i].name, name))
			return key_table[i].value;

	if (strlen(name) == 3 && name[0] == '^' && name[1] == '[' && isprint(*name))
		return (int)name[2] + 0x80;
	if (strlen(name) == 2 && name[0] == '^' && isprint(*name))
		return (int)name[1] & 0x1f;
	if (strlen(name) == 1 && isprint(*name))
		return (int) *name;
	return ERR;
}

const char *
get_key_name(int key_value)
{
	static char key_char[] = "'X'\0";
	const char *seq = NULL;
	int key;

	for (key = 0; key < ARRAY_SIZE(key_table); key++)
		if (key_table[key].value == key_value)
			seq = key_table[key].name;

	if (seq == NULL && key_value < 0x7f) {
		char *s = key_char + 1;

		if (key_value >= 0x20) {
			*s++ = key_value;
		} else {
			*s++ = '^';
			*s++ = 0x40 | (key_value & 0x1f);
		}
		*s++ = '\'';
		*s++ = '\0';
		seq = key_char;
	}

	return seq ? seq : "(no key)";
}

static bool
append_key(char *buf, size_t *pos, const struct keybinding *keybinding)
{
	const char *sep = *pos > 0 ? ", " : "";
	const char *keyname = get_key_name(keybinding->alias);

	return string_nformat(buf, BUFSIZ, pos, "%s%s", sep, keyname);
}

static bool
append_keymap_request_keys(char *buf, size_t *pos, enum request request,
			   struct keymap *keymap, bool all)
{
	int i;

	for (i = 0; i < keymap->size; i++) {
		if (keymap->data[i].request == request) {
			if (!append_key(buf, pos, &keymap->data[i]))
				return FALSE;
			if (!all)
				break;
		}
	}

	return TRUE;
}

#define get_view_key(view, request) get_keys(&(view)->ops->keymap, request, FALSE)

const char *
get_keys(struct keymap *keymap, enum request request, bool all)
{
	static char buf[BUFSIZ];
	size_t pos = 0;

	buf[pos] = 0;

	if (!append_keymap_request_keys(buf, &pos, request, keymap, all))
		return "Too many keybindings!";
	if (pos > 0 && !all)
		return buf;

	if (!is_generic_keymap(keymap)) {
		/* Only the generic keymap includes the default keybindings when
		 * listing all keys. */
		if (all)
			return buf;

		if (!append_keymap_request_keys(buf, &pos, request, &generic_keymap, all))
			return "Too many keybindings!";
		if (pos)
			return buf;
	}

	return buf;
}

static struct run_request *run_request;
static size_t run_requests;

DEFINE_ALLOCATOR(realloc_run_requests, struct run_request, 8)

bool
add_run_request(struct keymap *keymap, int key, const char **argv, enum run_request_flag flags)
{
	bool force = flags & RUN_REQUEST_FORCE;
	struct run_request *req;

	if (!force && get_keybinding(keymap, key) != key)
		return TRUE;

	if (!realloc_run_requests(&run_request, run_requests, 1))
		return FALSE;

	if (!argv_copy(&run_request[run_requests].argv, argv))
		return FALSE;

	req = &run_request[run_requests++];
	req->silent = flags & RUN_REQUEST_SILENT;
	req->confirm = flags & RUN_REQUEST_CONFIRM;
	req->exit = flags & RUN_REQUEST_EXIT;
	req->internal = flags & RUN_REQUEST_INTERNAL;
	req->keymap = keymap;
	req->key = key;

	add_keybinding(keymap, REQ_RUN_REQUESTS + run_requests, key);
	return TRUE;
}

struct run_request *
get_run_request(enum request request)
{
	if (request <= REQ_RUN_REQUESTS || request > REQ_RUN_REQUESTS + run_requests)
		return NULL;
	return &run_request[request - REQ_RUN_REQUESTS - 1];
}

/* vim: set ts=8 sw=8 noexpandtab: */
