/* Copyright (c) 2006-2013 Jonas Fonseca <jonas.fonseca@gmail.com>
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

void
compat_wordfree (wordexp_t *pwordexp)
{
	free(pwordexp->we_wordv[0]);
	free(pwordexp->we_wordv);
}

int
compat_wordexp (const char *words, wordexp_t *pwordexp, int flags)
{
	pwordexp->we_wordv = calloc(2, sizeof(*pwordexp->we_wordv));
	pwordexp->we_wordv[0] = calloc(1, strlen(words) + 1);
	strncpy(pwordexp->we_wordv[0], words, strlen(words) + 1);

	if (    pwordexp->we_wordv[0][0] == '~'
	    && (pwordexp->we_wordv[0][1] == '/' || pwordexp->we_wordv[0][1] == 0)) {
		const char *home = getenv("HOME") ? getenv("HOME") : "~";
		pwordexp->we_wordv[0] = realloc(pwordexp->we_wordv[0],
						strlen(pwordexp->we_wordv[0]) + strlen(home));
		memmove(pwordexp->we_wordv[0] + strlen(home) - 1, pwordexp->we_wordv[0],
			strlen(pwordexp->we_wordv[0]));
		/* intentional overwrite by one character */
		memmove(pwordexp->we_wordv[0], home, strlen(home));
	}

	return 0;
}

/* vim: set ts=8 sw=8 noexpandtab: */
