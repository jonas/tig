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
#include "tig/draw.h"

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

static enum status_code find_next_match(struct view *view, enum request request);

static enum status_code
setup_and_find_next(struct view *view, enum request request)
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

	return find_next_match(view, request);
}

static enum status_code
find_next_match_line(struct view *view, int direction, bool wrapped)
{
	/* Note, `i` is unsigned and will wrap around in which case it
	 * will become bigger than view->matched_lines. */
	size_t i = direction > 0 ? 0 : view->matched_lines - 1;

	for (; i < view->matched_lines; i += direction) {
		size_t lineno = view->matched_line[i];

		if (direction > 0) {
			if (!wrapped && lineno <= view->pos.lineno)
				continue;
			if (wrapped && lineno > view->pos.lineno)
				continue;
		} else {
			if (!wrapped && lineno >= view->pos.lineno)
				continue;
			if (wrapped && lineno < view->pos.lineno)
				continue;
		}

		select_view_line(view, lineno);
		return success("Line %zu matches '%s' (%zu of %zu)", lineno + 1, view->grep, i + 1, view->matched_lines);
	}

	return -1;
}

static enum status_code
find_next_match(struct view *view, enum request request)
{
	enum status_code code;
	int direction;

	if (!*view->grep || strcmp(view->grep, view->env->search)) {
		if (!*view->env->search)
			return success("No previous search");
		return setup_and_find_next(view, request);
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

	code = find_next_match_line(view, direction, false);
	if (code != SUCCESS && opt_wrap_search)
		code = find_next_match_line(view, direction, true);

	return code == SUCCESS ? code : success("No match found for '%s'", view->grep);
}

void
find_next(struct view *view, enum request request)
{
	enum status_code code = find_next_match(view, request);

	report("%s", get_status_message(code));
}

void
reset_search(struct view *view)
{
	free(view->matched_line);
	view->matched_line = NULL;
	view->matched_lines = 0;
}

void
search_view(struct view *view, enum request request)
{
	const char *prompt = request == REQ_SEARCH ? "/" : "?";
	char *search = read_prompt(prompt);

	if (search) {
		enum status_code code;

		string_ncopy(argv_env.search, search, strlen(search));
		code = setup_and_find_next(view, request);
		report("%s", get_status_message(code));
	} else if (*argv_env.search) {
		find_next(view, request);
	} else {
		report_clear();
	}
}

/* vim: set ts=8 sw=8 noexpandtab: */
