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

#include "tig/search.h"
#include "tig/prompt.h"
#include "tig/display.h"
#include "tig/prompt.h"
#include "tig/draw.h"

enum typeahead {
	NO_TYPEAHEAD	=  0,
	TYPEAHEAD	=  1,
};

enum result_message {
	SHORT_MESSAGE,
	FULL_MESSAGE,
};

DEFINE_ALLOCATOR(realloc_unsigned_ints, unsigned int, 32)

bool
grep_text(struct view *view, const char *text[])
{
	regmatch_t pmatch;
	size_t i;

	for (i = 0; text[i]; i++)
		if (*text[i] && !regexec(view->regex, text[i], 1, &pmatch, 0))
			return TRUE;
	return FALSE;
}

static bool
find_matches(struct view *view)
{
	size_t lineno;

	/* Note, lineno is unsigned long so will wrap around in which case it
	 * will become bigger than view->lines. */
	for (lineno = 0; lineno < view->lines; lineno++) {
		if (!view->ops->grep(view, &view->line[lineno]))
			continue;

		if (!realloc_unsigned_ints(&view->matched_line, view->matched_lines, 1))
			return FALSE;

		view->matched_line[view->matched_lines++] = lineno;
	}

	return TRUE;
}

static enum status_code
init_search(struct view *view)
{
	int regex_err;
	int regex_flags = opt_ignore_case ? REG_ICASE : 0;

	if (view->regex) {
		regfree(view->regex);
		*view->grep = 0;
	} else {
		view->regex = calloc(1, sizeof(*view->regex));
		if (!view->regex)
			return ERROR_OUT_OF_MEMORY;
	}

	regex_err = regcomp(view->regex, view->env->search, REG_EXTENDED | regex_flags);
	if (regex_err != 0) {
		char buf[SIZEOF_STR] = "unknown error";

		regerror(regex_err, view->regex, buf, sizeof(buf));
		return error("Search failed: %s", buf);
	}

	string_copy(view->grep, view->env->search);

	reset_search(view);
	return SUCCESS;
}

static enum status_code
find_next_match(struct view *view, enum request request, enum typeahead typeahead, enum result_message msg)
{
	int direction;
	size_t i;

	if (!*view->grep || typeahead == TYPEAHEAD) {
		enum status_code code;

		if (!*view->env->search)
			return success("No previous search");
		code = init_search(view);
		if (code != SUCCESS)
			return code;
	}

	switch (request) {
	case REQ_SEARCH:
	case REQ_FIND_NEXT:
		direction = 1;
		break;

	case REQ_SEARCH_BACK:
	case REQ_FIND_PREV:
		direction = -1;
		break;

	default:
		return error("Unknown search request");
	}

	if (!view->matched_lines && !find_matches(view))
		return ERROR_OUT_OF_MEMORY;

	/* Note, `i` is unsigned and will wrap around in which case it
	 * will become bigger than view->matched_lines. */
	i = direction > 0 ? 0 : view->matched_lines - 1;
	typeahead *= direction;
	for (; i < view->matched_lines; i += direction) {
		size_t lineno = view->matched_line[i];

		if (direction > 0 && lineno + typeahead <= view->pos.lineno)
			continue;

		if (direction < 0 && lineno + typeahead >= view->pos.lineno)
			continue;

		select_view_line(view, lineno);
		if (msg == SHORT_MESSAGE)
			return success("%zu of %zu", i + 1, view->matched_lines);
		return success("Line %zu matches '%s' (%zu of %zu)", lineno + 1, view->grep, i + 1, view->matched_lines);
	}

	if (msg == SHORT_MESSAGE)
		return success("No match");
	return success("No match found for '%s'", view->grep);
}

void
find_next(struct view *view, enum request request)
{
	enum status_code code = find_next_match(view, request, NO_TYPEAHEAD, FULL_MESSAGE);

	report("%s", get_status_message(code));
}

void
reset_search(struct view *view)
{
	free(view->matched_line);
	view->matched_line = NULL;
	view->matched_lines = 0;
}

struct search_typeahead_context {
	struct view *view;
	struct keymap *keymap;
	enum request request;
	enum status_code code;
};

static enum input_status
search_update_status(struct input *input, enum status_code code, enum input_status status)
{
	if (code != SUCCESS)
		return INPUT_CANCEL;

	string_format(input->status, "(%s)", get_status_message(code));
	return status;
}

static enum input_status
search_typeahead(struct input *input, struct key *key)
{
	struct search_typeahead_context *search = input->data;
	enum input_status status = prompt_default_handler(input, key);
	enum request request;

	if (status != INPUT_SKIP)
		return status;

	request = get_keybinding(search->keymap, key, 1, NULL);
	if (request != REQ_UNKNOWN) {
		search->code = find_next_match(search->view, request, NO_TYPEAHEAD, SHORT_MESSAGE);
		return search_update_status(input, search->code, INPUT_SKIP);
	}

	if (!key_to_value(key)) {
		string_ncopy(argv_env.search, input->buf, strlen(input->buf));
		search->code = find_next_match(search->view, search->request, TYPEAHEAD, SHORT_MESSAGE);
		return search_update_status(input, search->code, INPUT_OK);
	}

	return INPUT_SKIP;
}

void
search_view(struct view *view, enum request request)
{
	const char *prompt = request == REQ_SEARCH ? "/" : "?";
	struct search_typeahead_context context = {
		view,
		get_keymap("search", STRING_SIZE("search")),
		request,
		SUCCESS
	};
	char *search = read_prompt_incremental(prompt, FALSE, search_typeahead, &context);

	if (context.code != SUCCESS) {
		report("%s", get_status_message(context.code));
	} else if (search) {
		enum status_code code = find_next_match(view, request, TYPEAHEAD, FULL_MESSAGE);

		report("%s", get_status_message(code));
	} else if (*argv_env.search) {
		find_next(view, request);
	} else {
		report_clear();
	}
}

/* vim: set ts=8 sw=8 noexpandtab: */
