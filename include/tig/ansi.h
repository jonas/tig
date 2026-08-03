/* Copyright (c) 2006-2026 Jonas Fonseca <jonas.fonseca@gmail.com>
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

#ifndef TIG_ANSI_H
#define TIG_ANSI_H

#include "tig/tig.h"

#define ANSI_MAX_SPANS	512

enum ansi_color_type {
	ANSI_COLOR_DEFAULT,
	ANSI_COLOR_BASIC,	/* 0-7: standard ANSI colors */
	ANSI_COLOR_256,		/* 0-255: extended palette */
	ANSI_COLOR_RGB		/* 24-bit truecolor */
};

struct ansi_color {
	enum ansi_color_type type;
	union {
		int index;
		struct { unsigned char r, g, b; } rgb;
	};
};

struct ansi_span {
	struct ansi_color fg;
	struct ansi_color bg;
	int attr;		/* ncurses attributes: A_BOLD, A_UNDERLINE, etc. */
	size_t offset;		/* byte offset in stripped text */
	size_t length;		/* byte length of this span */
};

/*
 * Parse a line containing ANSI escape sequences.
 *
 * Strips escape codes from `raw` and writes plain text to `stripped`.
 * Records color/attribute spans in `spans`.
 *
 * Returns the number of spans written, or -1 on error.
 */
int ansi_parse_line(const char *raw, char *stripped, size_t stripped_size,
		    struct ansi_span *spans, int max_spans);

/*
 * Returns true if the string contains ANSI escape sequences.
 */
static inline bool
ansi_has_escapes(const char *text)
{
	return text && strchr(text, 0x1b) != NULL;
}

#endif
/* vim: set ts=8 sw=8 noexpandtab: */
