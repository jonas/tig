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

static struct keymap keymaps[] = {
	{ "generic" },
#define VIEW_KEYMAP(id, name) { #name }
	VIEW_INFO(VIEW_KEYMAP)
};

static struct keymap *generic_keymap = keymaps;
#define is_generic_keymap(keymap) ((keymap) == generic_keymap)

struct keymap *
get_keymap_by_index(int i)
{
	return 0 <= i && i < ARRAY_SIZE(keymaps) ? &keymaps[i] : NULL;
}

struct keymap *
get_keymap(const char *name, size_t namelen)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(keymaps); i++)
		if (!strncasecmp(keymaps[i].name, name, namelen))
			return &keymaps[i];

	return NULL;
}

static bool
keybinding_equals(struct key_input *input1, struct key_input *input2, bool *conflict)
{
	if (input1->modifiers.control &&
	    input1->modifiers.multibytes &&
	    !memcmp(&input1->modifiers, &input2->modifiers, sizeof(input1->modifiers)) &&
	    strlen(input1->data.bytes) == 1 &&
	    strlen(input2->data.bytes) == 1) {
		int c1 = input1->data.bytes[0];
		int c2 = input2->data.bytes[0];
		bool equals = ascii_toupper(c1) == ascii_toupper(c2);

		if (equals && c1 != c2)
			*conflict = TRUE;
		return equals;
	}

	return !memcmp(input1, input2, sizeof(*input1));
}

enum status_code
add_keybinding(struct keymap *table, enum request request, struct key_input *input)
{
	bool conflict = FALSE;
	size_t i;

	for (i = 0; i < table->size; i++) {
		if (keybinding_equals(&table->data[i].input, input, &conflict)) {
			table->data[i].request = request;
			return conflict ? ERROR_CTRL_KEY_CONFLICT : SUCCESS;
		}
	}

	table->data = realloc(table->data, (table->size + 1) * sizeof(*table->data));
	if (!table->data)
		die("Failed to allocate keybinding");
	table->data[table->size].input = *input;
	table->data[table->size++].request = request;
	return SUCCESS;
}

/* Looks for a key binding first in the given map, then in the generic map, and
 * lastly in the default keybindings. */
enum request
get_keybinding(struct keymap *keymap, struct key_input *input)
{
	size_t i;

	for (i = 0; i < keymap->size; i++)
		if (keybinding_equals(&keymap->data[i].input, input, NULL))
			return keymap->data[i].request;

	for (i = 0; i < generic_keymap->size; i++)
		if (keybinding_equals(&generic_keymap->data[i].input, input, NULL))
			return generic_keymap->data[i].request;

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

		if (!append_keymap_request_keys(buf, &pos, request, generic_keymap, all))
			return "Too many keybindings!";
		if (pos)
			return buf;
	}

	return buf;
}

static struct run_request *run_request;
static size_t run_requests;

DEFINE_ALLOCATOR(realloc_run_requests, struct run_request, 8)

enum status_code
add_run_request(struct keymap *keymap, struct key_input *input, const char **argv)
{
	struct run_request *req;
	struct run_request_flags flags = {};

	if (!strchr(":!?@<", *argv[0]))
		return ERROR_UNKNOWN_REQUEST_NAME;

	while (*argv[0]) {
		if (*argv[0] == ':') {
			flags.internal = 1;
			argv[0]++;
			break;
		} else if (*argv[0] == '@') {
			flags.silent = 1;
		} else if (*argv[0] == '?') {
			flags.confirm = 1;
		} else if (*argv[0] == '<') {
			flags.exit = 1;
		} else if (*argv[0] != '!') {
			break;
		}
		argv[0]++;
	}

	if (!realloc_run_requests(&run_request, run_requests, 1))
		return ERROR_OUT_OF_MEMORY;

	if (!argv_copy(&run_request[run_requests].argv, argv))
		return ERROR_OUT_OF_MEMORY;

	req = &run_request[run_requests++];
	req->flags = flags;
	req->keymap = keymap;

	return add_keybinding(keymap, REQ_RUN_REQUESTS + run_requests, input);
}

struct run_request *
get_run_request(enum request request)
{
	if (request <= REQ_RUN_REQUESTS || request > REQ_RUN_REQUESTS + run_requests)
		return NULL;
	return &run_request[request - REQ_RUN_REQUESTS - 1];
}

/* vim: set ts=8 sw=8 noexpandtab: */
