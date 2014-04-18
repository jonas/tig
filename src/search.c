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
#include "tig/main.h"
#include "tig/graph.h"

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
	size_t i;

	for (i = 0; text[i]; i++)
		if (*text[i] && !regexec(view->regex, text[i], 0, NULL, 0))
			return true;
	return false;
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
			return false;

		view->line[lineno].search_result = true;
		view->matched_line[view->matched_lines++] = lineno;
	}

	/* Clear and show highlighted results. */
	redraw_view_from(view, 0);

	return true;
}

static enum status_code
find_next_match(struct view *view, enum request request, enum typeahead typeahead, enum result_message msg);

static bool contains_uppercase(const char *search)
{
	const char *c = search;
	for (; *c != '\0'; ++c) {
		if (isupper(*c))
			return true;
	}
	return false;
}

static enum status_code
init_search(struct view *view)
{
	int regex_err;
	int regex_flags = opt_ignore_case == IGNORE_CASE_YES ? REG_ICASE : 0;

	if (opt_ignore_case == IGNORE_CASE_SMART_CASE
	    && !contains_uppercase(view->env->search))
		regex_flags |= REG_ICASE;

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
find_next_match_line(struct view *view, int direction, bool wrapped, enum typeahead typeahead, enum result_message msg)
{
	/* Note, `i` is unsigned and will wrap around in which case it
	 * will become bigger than view->matched_lines. */
	size_t i = direction > 0 ? 0 : view->matched_lines - 1;
	size_t offset = typeahead * direction;

	for (; i < view->matched_lines; i += direction) {
		size_t lineno = view->matched_line[i];

		if (direction > 0 && lineno + offset <= view->pos.lineno)
			continue;

		if (direction < 0 && lineno + offset >= view->pos.lineno)
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

static enum status_code
find_next_match(struct view *view, enum request request, enum typeahead typeahead, enum result_message msg)
{
	enum status_code code;
	int direction;

	if (!*view->grep || strcmp(view->grep, view->env->search) || typeahead == TYPEAHEAD) {
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

	code = find_next_match_line(view, direction, false, typeahead, msg);
	if (code != SUCCESS && opt_wrap_search)
		code = find_next_match_line(view, direction, true, typeahead, msg);

	return code == SUCCESS ? code : success("No match found for '%s'", view->grep);
}

void
find_next(struct view *view, enum request request)
{
	enum status_code code = find_next_match(view, request, NO_TYPEAHEAD, FULL_MESSAGE);

	report("%s", get_status_message(code));
}

static enum status_code
find_next_merge_line(struct view *view, int direction, bool wrapped)
{
	int dist;
	size_t max;

	if (wrapped)
		max = view->lines;
	else
		max = direction > 0 ? view->lines - view->pos.lineno
				    : view->pos.lineno;

	for (dist = direction; abs(dist) < max; dist += direction) {
		ssize_t lineno = view->pos.lineno + dist;
		if (lineno < 0)
			lineno += view->lines;
		else if (lineno >= view->lines)
			lineno -= view->lines;

		struct line *line = &view->line[lineno];
		struct commit *commit = line->data;
		struct graph_canvas *canvas = &commit->graph;
		struct view_column_data column_data;

		if (!view->ops->get_column_data(view, line, &column_data))
			continue;

		if (column_data.graph && !column_data.graph->is_merge(canvas))
			continue;

		select_view_line(view, lineno);
		return SUCCESS;
	}

	return success("No merge commit found");
}

static enum status_code
find_next_merge(struct view *view, enum request request)
{
	switch (request) {
	case REQ_MOVE_NEXT_MERGE:
		return find_next_merge_line(view, 1, opt_wrap_search);

	case REQ_MOVE_PREV_MERGE:
		return find_next_merge_line(view, -1, opt_wrap_search);

	default:
		return error("Invalid request searching for next merge");
	}
}

void
find_merge(struct view *view, enum request request)
{
	enum status_code code = find_next_merge(view, request);

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

	string_format(input->context, "(%s)", get_status_message(code));
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
	char *search = read_prompt_incremental(prompt, false, false, search_typeahead, &context);

	if (context.code != SUCCESS) {
		report("%s", get_status_message(context.code));
	} else if (search && *search) {
		enum status_code code = find_next_match(view, request, TYPEAHEAD, FULL_MESSAGE);

		report("%s", get_status_message(code));
	} else if (search && *argv_env.search) {
		find_next(view, request);
	} else {
		report_clear();
	}
}

/* vim: set ts=8 sw=8 noexpandtab: */
