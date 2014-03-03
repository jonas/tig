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

#include "tig/tig.h"
#include "tig/types.h"
#include "tig/argv.h"
#include "tig/io.h"
#include "tig/keys.h"
#include "tig/util.h"

struct keybinding {
	struct key_input input;
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
get_keymap(const char *name, size_t namelen)
{
	struct keymap *keymap = keymaps;

	while (keymap) {
		if (!strncasecmp(keymap->name, name, namelen))
			return keymap;
		keymap = keymap->next;
	}

	return NULL;
}


void
add_keybinding(struct keymap *table, enum request request, struct key_input *input)
{
	size_t i;

	for (i = 0; i < table->size; i++) {
		if (!memcmp(&table->data[i].input, input, sizeof(*input))) {
			table->data[i].request = request;
			return;
		}
	}

	table->data = realloc(table->data, (table->size + 1) * sizeof(*table->data));
	if (!table->data)
		die("Failed to allocate keybinding");
	table->data[table->size].input = *input;
	table->data[table->size++].request = request;
}

/* Looks for a key binding first in the given map, then in the generic map, and
 * lastly in the default keybindings. */
enum request
get_keybinding(struct keymap *keymap, struct key_input *input)
{
	size_t i;

	for (i = 0; i < keymap->size; i++)
		if (!memcmp(&keymap->data[i].input, input, sizeof(*input)))
			return keymap->data[i].request;

	for (i = 0; i < generic_keymap.size; i++)
		if (!memcmp(&generic_keymap.data[i].input, input, sizeof(*input)))
			return generic_keymap.data[i].request;

	return REQ_NONE;
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
get_key_value(const char *name, struct key_input *input)
{
	int i;

	memset(input, 0, sizeof(*input));

	for (i = 0; i < ARRAY_SIZE(key_table); i++)
		if (!strcasecmp(key_table[i].name, name)) {
			if (key_table[i].value == ' ') {
				name = " ";
				break;
			}
			if (key_table[i].value == '#') {
				name = "#";
				break;
			}
			input->data.key = key_table[i].value;
			return OK;
		}

	if (name[0] == '^' && name[1] == '[') {
		input->modifiers.escape = 1;
		name += 2;
	} else if (name[0] == '^') {
		input->modifiers.control = 1;
		name += 1;
	}

	i = utf8_char_length(name);
	if (strlen(name) == i && utf8_to_unicode(name, i) != 0) {
		strncpy(input->data.bytes, name, i);
		input->modifiers.multibytes = 1;
		return OK;
	}

	return ERR;
}

const char *
get_key_name(const struct key_input *input)
{
	static char buf[SIZEOF_STR];
	const char *modifier = "";
	int key;

	if (!input->modifiers.multibytes) {
		for (key = 0; key < ARRAY_SIZE(key_table); key++)
			if (key_table[key].value == input->data.key)
				return key_table[key].name;
	}

	if (input->modifiers.escape)
		modifier = "^[";
	else if (input->modifiers.control)
		modifier = "^";

	if (string_format(buf, "'%s%s'", modifier, input->data.bytes))
		return buf;

	return "(no key)";
}

static bool
append_key(char *buf, size_t *pos, const struct keybinding *keybinding)
{
	const char *sep = *pos > 0 ? ", " : "";
	const char *keyname = get_key_name(&keybinding->input);

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
add_run_request(struct keymap *keymap, struct key_input *input, const char **argv, enum run_request_flag flags)
{
	struct run_request *req;

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
	req->input = *input;

	add_keybinding(keymap, REQ_RUN_REQUESTS + run_requests, input);
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
