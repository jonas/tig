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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "compat.h"
#include "stddef.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void
compat_wordfree (wordexp_t *pwordexp)
{
	free(pwordexp->we_wordv[0]);
	free(pwordexp->we_wordv);
}

int
compat_wordexp (const char *words, wordexp_t *pwordexp, int flags)
{
	char *expanded = NULL;
	const char *home = getenv("HOME");

	if (home && words[0] == '~' && (words[1] == '/' || words[1] == 0)) {
		size_t len = strlen(home) + strlen(words + 1) + 1;
		if ((expanded = malloc(len)) && !snprintf(expanded, len, "%s%s", home, words + 1)) {
			free(expanded);
			return -1;
		}
	} else {
		expanded = strdup(words);
	}

	if (!expanded)
		return -1;

	pwordexp->we_wordv = calloc(2, sizeof(*pwordexp->we_wordv));
	if (!pwordexp->we_wordv) {
		free(expanded);
		return -1;
	}
	pwordexp->we_wordv[0] = expanded;

	return 0;
}

/* vim: set ts=8 sw=8 noexpandtab: */
