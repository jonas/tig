/* Copyright (c) 2006-2015 Jonas Fonseca <jonas.fonseca@gmail.com>
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

struct keybinding {
	enum request request;
	size_t keys;
	struct key key[1];
};

static struct keymap keymaps[] = {
	{ "generic" },
	{ "search" },
#define VIEW_KEYMAP(id, name) { #name }
	VIEW_INFO(VIEW_KEYMAP)
};

static struct keymap *generic_keymap = keymaps;
#define is_generic_keymap(keymap) ((keymap) == generic_keymap)

static struct keymap *search_keymap = keymaps + 1;
#define is_search_keymap(keymap) ((keymap) == search_keymap)

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
keybinding_matches(const struct keybinding *keybinding, const struct key key[],
		  size_t keys, bool *conflict_ptr)
{
	bool conflict = false;
	int i;

	if (keybinding->keys < keys)
		return false;

	for (i = 0; i < keys; i++) {
		const struct key *key1 = &keybinding->key[i];
		const struct key *key2 = &key[i];

		if (key1->modifiers.control &&
		    key1->modifiers.multibytes &&
		    !memcmp(&key1->modifiers, &key2->modifiers, sizeof(key1->modifiers)) &&
		    strlen(key1->data.bytes) == 1 &&
		    strlen(key2->data.bytes) == 1) {
			int c1 = key1->data.bytes[0];
			int c2 = key2->data.bytes[0];

			if (ascii_toupper(c1) != ascii_toupper(c2))
				return false;
			if (c1 != c2)
				conflict = true;
		} else {
			if (memcmp(key1, key2, sizeof(*key1)))
				return false;
		}
	}

	if (conflict_ptr && keybinding->request != REQ_NONE)
		*conflict_ptr = conflict;
	return true;
}

static bool
keybinding_equals(const struct keybinding *keybinding, const struct key key[],
		  size_t keys, bool *conflict_ptr)
{
	if (keybinding->keys != keys)
		return false;
	return keybinding_matches(keybinding, key, keys, conflict_ptr);
}

enum status_code
add_keybinding(struct keymap *table, enum request request,
	       const struct key key[], size_t keys)
{
	struct keybinding *keybinding;
	char buf[SIZEOF_STR];
	bool conflict = false;
	size_t i;

	for (i = 0; i < table->size; i++) {
		if (keybinding_equals(table->data[i], key, keys, &conflict)) {
			enum request old_request = table->data[i]->request;
			const char *old_name;

			table->data[i]->request = request;
			if (!conflict)
				return SUCCESS;

			old_name = get_request_name(old_request);
			string_ncopy(buf, old_name, strlen(old_name));
			return error("Key binding for %s and %s conflict; "
				     "keys using Ctrl are case insensitive",
				     buf, get_request_name(request));
		}
	}

	table->data = realloc(table->data, (table->size + 1) * sizeof(*table->data));
	keybinding = calloc(1, sizeof(*keybinding) + (sizeof(*key) * (keys - 1)));
	if (!table->data || !keybinding)
		die("Failed to allocate keybinding");

	memcpy(keybinding->key, key, sizeof(*key) * keys);
	keybinding->keys = keys;
	keybinding->request = request;
	table->data[table->size++] = keybinding;
	return SUCCESS;
}

static enum request
get_keybinding_in_keymap(const struct keymap *keymap, const struct key key[], size_t keys, int *matches)
{
	enum request request = REQ_UNKNOWN;
	size_t i;

	for (i = 0; i < keymap->size; i++)
		if (keybinding_matches(keymap->data[i], key, keys, NULL)) {
			if (matches && keymap->data[i]->request != REQ_NONE)
				(*matches)++;
			/* Overriding keybindings, might have been added
			 * at the end of the keymap so we need to
			 * iterate all keybindings. */
			if (keymap->data[i]->keys == keys)
				request = keymap->data[i]->request;
		}

	return request;
}

/* Looks for a key binding first in the given keymap, then in the generic keymap. */
enum request
get_keybinding(const struct keymap *keymap, const struct key key[], size_t keys, int *matches)
{
	enum request request = get_keybinding_in_keymap(keymap, key, keys, matches);

	if (!is_search_keymap(keymap)) {
		int generic_matches = 0;
		enum request generic_request = get_keybinding_in_keymap(generic_keymap, key, keys, &generic_matches);

		/* Include generic matches iff there are more than one
		 * so unbound keys in the current keymap still override
		 * generic keys while still ensuring that the key combo
		 * handler continues to wait for more keys if there is
		 * another possible match. E.g. while in `main` view:
		 *
		 *   bind generic q  quit  # 'q' will quit
		 *   bind main    q  none  # 'q' will do nothing
		 *   bind generic qa quit  # 'qa' will quit
		 *   bind main    qn next  # 'qn' will move to next entry
		 */
		if (matches && (request == REQ_UNKNOWN || generic_matches > 1))
			(*matches) += generic_matches;
		if (request == REQ_UNKNOWN)
			request = generic_request;
	}

	return request == REQ_NONE ? REQ_UNKNOWN : request;
}


struct key_mapping {
	const char *name;
	int value;
};

static const struct key_mapping key_mappings[] = {
	{ "Enter",	KEY_RETURN },
	{ "Space",	' ' },
	{ "Backspace",	KEY_BACKSPACE },
	{ "Tab",	KEY_TAB },
	{ "Escape",	KEY_ESC },
	{ "Esc",	KEY_ESC },
	{ "Left",	KEY_LEFT },
	{ "Right",	KEY_RIGHT },
	{ "Up",		KEY_UP },
	{ "Down",	KEY_DOWN },
	{ "Insert",	KEY_IC },
	{ "Ins",	KEY_IC },
	{ "Delete",	KEY_DC },
	{ "Del",	KEY_DC },
	{ "Hash",	'#' },
	{ "Home",	KEY_HOME },
	{ "End",	KEY_END },
	{ "PageUp",	KEY_PPAGE },
	{ "PgUp",	KEY_PPAGE },
	{ "PageDown",	KEY_NPAGE },
	{ "PgDown",	KEY_NPAGE },
	{ "LessThan",	'<' },
	{ "LT",		'<' },
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
	{ "ScrollBack",	KEY_SR },
	{ "SBack",	KEY_SR },
	{ "ScrollFwd",	KEY_SF },
	{ "SFwd",	KEY_SF },
};

static const struct key_mapping *
get_key_mapping(const char *name, size_t namelen)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(key_mappings); i++) {
		if (namelen == strlen(key_mappings[i].name) &&
		    !strncasecmp(key_mappings[i].name, name, namelen))
			return &key_mappings[i];
	}

	return NULL;
}

static enum status_code
parse_key_value(struct key *key, const char **name_ptr, size_t offset,
		const char *replacement, const char *end)
{
	const char *name = replacement ? replacement : *name_ptr + offset;
	size_t namelen = utf8_char_length(name);
	const char *nameend = name + namelen;

	if (strlen(name) < namelen || utf8_to_unicode(name, namelen) == 0)
		return error("Error parsing UTF-8 bytes: %s", name);

	strncpy(key->data.bytes, name, namelen);
	key->modifiers.multibytes = 1;
	if (end) {
		*name_ptr = end + 1;
		if (!replacement && nameend + 1 < end)
			return success("Ignoring text after key mapping: %.*s",
				(int) (end - nameend), nameend);
	} else {
		*name_ptr = nameend;
	}

	return SUCCESS;
}

enum status_code
get_key_value(const char **name_ptr, struct key *key)
{
	const char *name = *name_ptr;
	const char *end = NULL;

	memset(key, 0, sizeof(*key));

	if (*name == '<') {
		end = strchr(name + 1, '>');
		if (!end)
			return error("Missing '>' from key mapping: %s", name);

		if (!prefixcmp(name, "<Ctrl-")) {
			key->modifiers.control = 1;
			return parse_key_value(key, name_ptr, 6, NULL, end);

		} else if (!prefixcmp(name, "<C-")) {
			key->modifiers.control = 1;
			return parse_key_value(key, name_ptr, 3, NULL, end);

		} else {
			const struct key_mapping *mapping;
			const char *start = name + 1;
			int len = end - start;

			mapping = get_key_mapping(start, len);
			if (!mapping)
				return error("Unknown key mapping: %.*s", len, start);

			if (mapping->value == ' ')
				return parse_key_value(key, name_ptr, 0, " ", end);

			if (mapping->value == '#')
				return parse_key_value(key, name_ptr, 0, "#", end);

			if (mapping->value == '<')
				return parse_key_value(key, name_ptr, 0, "<", end);

			*name_ptr = end + 1;
			key->data.value = mapping->value;
			return SUCCESS;
		}
	}

	if (name[0] == '^' && name[1] == '[') {
		return error("Escape key combo must now use '<Esc>%s' "
			     "instead of '%s'", name + 2, name);
	} else if (name[0] == '^' && name[1] != '\0') {
		return error("Control key mapping must now use '<Ctrl-%s>' "
			     "instead of '%s'", name + 1, name);
	}

	return parse_key_value(key, name_ptr, 0, NULL, end);
}

const char *
get_key_name(const struct key key[], size_t keys, bool quote_comma)
{
	static char buf[SIZEOF_STR];
	size_t pos = 0;
	int i;

	for (i = 0; i < keys; i++) {
		bool multibytes = key[i].modifiers.multibytes;
		const char *name = multibytes ? key[i].data.bytes : "";
		const char *start = "";
		const char *end = "";
		bool use_symbolic;

		if (key[i].modifiers.control) {
			start = "<Ctrl-";
			end = ">";
		} else if (*name == ',' && quote_comma) {
			/* Quote commas so they stand out in the help view. */
			start = "'";
			end = "'";
		}

		/* Use symbolic name for spaces so they are readable. */
		use_symbolic = !*name || *name == ' ';
		/* When listing keys for :save-options quote illegal characters. */
		if (!quote_comma && (*name == '<' || *name == '#'))
			use_symbolic = true;

		if (use_symbolic) {
			int value = *name ? *name : key[i].data.value;
			int j;

			name = "<?>";
			for (j = 0; j < ARRAY_SIZE(key_mappings); j++)
				if (key_mappings[j].value == value) {
					start = "<";
					end = ">";
					name = key_mappings[j].name;
					break;
				}
		}

		if (!string_format_from(buf, &pos, "%s%s%s", start, name, end))
			return "(no key)";
	}

	return buf;
}

static bool
append_key(char *buf, size_t *pos, const struct keybinding *keybinding, bool all)
{
	const char *sep = *pos > 0 ? ", " : "";
	const char *keyname = get_key_name(keybinding->key, keybinding->keys, all);

	return string_nformat(buf, BUFSIZ, pos, "%s%s", sep, keyname);
}

static bool
append_keymap_request_keys(char *buf, size_t *pos, enum request request,
			   const struct keymap *keymap, bool all)
{
	int i;

	for (i = 0; i < keymap->size; i++) {
		if (keymap->data[i]->request == request) {
			if (!append_key(buf, pos, keymap->data[i], all))
				return false;
			if (!all)
				break;
		}
	}

	return true;
}

const char *
get_keys(const struct keymap *keymap, enum request request, bool all)
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

#define COMMAND_FLAGS ":!?@<"

enum status_code
parse_run_request_flags(struct run_request_flags *flags, const char **argv)
{
	if (!strchr(COMMAND_FLAGS, *argv[0]))
		return error("Unknown command flag '%c'; expected one of %s", argv[0][0], COMMAND_FLAGS);

	while (*argv[0]) {
		if (*argv[0] == ':') {
			flags->internal = 1;
			argv[0]++;
			break;
		} else if (*argv[0] == '@') {
			flags->silent = 1;
		} else if (*argv[0] == '?') {
			flags->confirm = 1;
		} else if (*argv[0] == '<') {
			flags->exit = 1;
		} else if (*argv[0] != '!') {
			break;
		}
		argv[0]++;
	}

	return SUCCESS;
}

enum status_code
add_run_request(struct keymap *keymap, const struct key key[],
		size_t keys, const char **argv)
{
	struct run_request *req;
	struct run_request_flags flags = {0};
	enum status_code code = parse_run_request_flags(&flags, argv);

	if (code != SUCCESS)
		return code;

	if (!realloc_run_requests(&run_request, run_requests, 1))
		return ERROR_OUT_OF_MEMORY;

	if (!argv_copy(&run_request[run_requests].argv, argv))
		return ERROR_OUT_OF_MEMORY;

	req = &run_request[run_requests++];
	req->flags = flags;
	req->keymap = keymap;

	return add_keybinding(keymap, REQ_RUN_REQUESTS + run_requests, key, keys);
}

struct run_request *
get_run_request(enum request request)
{
	if (request <= REQ_RUN_REQUESTS || request > REQ_RUN_REQUESTS + run_requests)
		return NULL;
	return &run_request[request - REQ_RUN_REQUESTS - 1];
}

const char *
format_run_request_flags(const struct run_request *req)
{
	static char flags[8];
	int flagspos = 0;

	memset(flags, 0, sizeof(flags));

	if (req->flags.internal)
		flags[flagspos++] = ':';
	else
		flags[flagspos] = '!'; /* Optional, if other flags are defined */

	if (req->flags.silent)
	    flags[flagspos++] = '@';
	if (req->flags.confirm)
	    flags[flagspos++] = '?';
	if (req->flags.exit)
		flags[flagspos++] = '<';
	if (flagspos > 1)
		flags[flagspos++] = 0;

	return flags;
}

struct key_visitor_state {
	key_visitor_fn visitor;
	void *data;
	struct keymap *keymap;
	bool combine_keys;
	const char *group;
};

static bool
foreach_key_visit(struct key_visitor_state *state, const char *group,
		  enum request request,
		  const struct request_info *req_info, const struct run_request *run_req)
{
	struct keymap *keymap = state->keymap;
	int i;

	if (state->group == group)
		group = NULL;

	if (state->combine_keys) {
		const char *key = get_keys(keymap, request, true);

		if (!key || !*key)
			return true;

		if (group)
			state->group = group;
		return state->visitor(state->data, group, keymap, request,
				      key, req_info, run_req);
	}

	for (i = 0; i < keymap->size; i++) {
		if (keymap->data[i]->request == request) {
			struct keybinding *keybinding = keymap->data[i];
			const char *key = get_key_name(keybinding->key, keybinding->keys, false);

			if (!key || !*key)
				continue;

			if (!state->visitor(state->data, group, keymap, request,
					    key, req_info, run_req))
				return false;

			if (group)
				state->group = group;
			group = NULL;
		}
	}

	return true;
}

static bool
foreach_key_request(void *data, const struct request_info *req_info, const char *group)
{
	struct key_visitor_state *state = data;

	if (req_info->request == REQ_NONE)
		return true;

	return foreach_key_visit(state, group, req_info->request, req_info, NULL);
}

static bool
foreach_key_run_request(struct key_visitor_state *state, bool internal, bool toggles)
{
	struct keymap *keymap = state->keymap;
	const char *group = !internal ?	"External commands:" :
			    toggles ?	"Option toggling:" :
					"Internal commands:";
	enum request request = REQ_RUN_REQUESTS + 1;

	for (; true; request++) {
		struct run_request *req = get_run_request(request);
		const char *key;

		if (!req)
			break;

		if (req->flags.internal != !!internal ||
		    req->keymap != keymap ||
		    !*(key = get_keys(keymap, request, true)))
			continue;

		if (toggles != !strcmp(req->argv[0], "toggle"))
			continue;

		if (!foreach_key_visit(state, group, request, NULL, req))
			return false;
	}

	return true;
}

bool
foreach_key(key_visitor_fn visitor, void *data, bool combine_keys)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(keymaps); i++) {
		struct key_visitor_state state = { visitor, data, &keymaps[i], combine_keys };

		if (!foreach_request(foreach_key_request, &state)
		    || !foreach_key_run_request(&state, true, true)
		    || !foreach_key_run_request(&state, true, false)
		    || !foreach_key_run_request(&state, false, false))
			return false;
	}

	return true;
}

/* vim: set ts=8 sw=8 noexpandtab: */
