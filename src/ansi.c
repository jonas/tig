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

#include "tig/ansi.h"

#ifdef HAVE_NCURSESW_CURSES_H
#include <ncursesw/curses.h>
#elif defined(HAVE_NCURSES_CURSES_H)
#include <ncurses/curses.h>
#elif defined(HAVE_NCURSES_H)
#include <ncurses.h>
#elif defined(HAVE_CURSES_H)
#include <curses.h>
#endif

/*
 * Parse a semicolon-separated list of SGR parameters.
 * Updates the current fg, bg, and attr state.
 */
/*
 * Consume an extended-color run starting at codes[*i] (38 or 48) and update
 * `color`, leaving *i on the last parameter consumed.
 *
 * Accepts both the legacy semicolon form (38;5;N and 38;2;R;G;B) and the
 * ITU T.416 colon form (38:5:N and 38:2:CS:R:G:B), which carries an extra
 * colorspace field ahead of the components.
 */
static void
ansi_parse_extended_color(const int *codes, const bool *subparam, int ncodes,
			  size_t *i, struct ansi_color *color)
{
	size_t sel = *i + 1;	/* selects 256-palette (5) or truecolor (2) */

	if (sel >= (size_t) ncodes)
		return;

	if (codes[sel] == 5 && sel + 1 < (size_t) ncodes) {
		color->type = ANSI_COLOR_256;
		color->index = codes[sel + 1];
		*i = sel + 1;

	} else if (codes[sel] == 2) {
		size_t r = subparam[sel] ? sel + 2 : sel + 1;

		if (r + 2 < (size_t) ncodes) {
			color->type = ANSI_COLOR_RGB;
			color->rgb.r = codes[r];
			color->rgb.g = codes[r + 1];
			color->rgb.b = codes[r + 2];
			*i = r + 2;
		}
	}
}

static void
ansi_parse_sgr(const char *params, size_t len,
		struct ansi_color *fg, struct ansi_color *bg, int *attr)
{
	int codes[ANSI_MAX_SGR_CODES];
	/* Whether the separator *before* codes[i] was a colon.  The colon
	 * form of 38/48 carries an extra colorspace field, so the RGB
	 * components sit one slot further along than in the semicolon form. */
	bool subparam[ANSI_MAX_SGR_CODES] = { false };
	int ncodes = 0;
	int val = 0;
	bool has_val = false;
	bool colon = false;
	size_t i;

	/* Parse the parameter list; both ';' and ':' separate values */
	for (i = 0; i < len && ncodes < ANSI_MAX_SGR_CODES; i++) {
		if (params[i] >= '0' && params[i] <= '9') {
			val = val * 10 + (params[i] - '0');
			has_val = true;
		} else if (params[i] == ';' || params[i] == ':') {
			subparam[ncodes] = colon;
			codes[ncodes++] = has_val ? val : 0;
			colon = params[i] == ':';
			val = 0;
			has_val = false;
		}
	}
	if (has_val && ncodes < ANSI_MAX_SGR_CODES) {
		subparam[ncodes] = colon;
		codes[ncodes++] = val;
	}

	/* Empty CSI m is equivalent to CSI 0 m (reset) */
	if (ncodes == 0) {
		codes[0] = 0;
		ncodes = 1;
	}

	for (i = 0; i < (size_t) ncodes; i++) {
		int code = codes[i];

		switch (code) {
		case 0: /* Reset */
			fg->type = ANSI_COLOR_DEFAULT;
			bg->type = ANSI_COLOR_DEFAULT;
			*attr = A_NORMAL;
			break;

		case 1:
			*attr |= A_BOLD;
			break;
		case 3:
#ifdef A_ITALIC
			*attr |= A_ITALIC;
#endif
			break;
		case 4:
			*attr |= A_UNDERLINE;
			break;
		case 7:
			*attr |= A_REVERSE;
			break;
		case 22:
			*attr &= ~A_BOLD;
			break;
		case 23:
#ifdef A_ITALIC
			*attr &= ~A_ITALIC;
#endif
			break;
		case 24:
			*attr &= ~A_UNDERLINE;
			break;
		case 27:
			*attr &= ~A_REVERSE;
			break;

		/* Standard foreground colors 30-37 */
		case 30: case 31: case 32: case 33:
		case 34: case 35: case 36: case 37:
			fg->type = ANSI_COLOR_BASIC;
			fg->index = code - 30;
			break;

		/* Default foreground */
		case 39:
			fg->type = ANSI_COLOR_DEFAULT;
			break;

		/* Standard background colors 40-47 */
		case 40: case 41: case 42: case 43:
		case 44: case 45: case 46: case 47:
			bg->type = ANSI_COLOR_BASIC;
			bg->index = code - 40;
			break;

		/* Default background */
		case 49:
			bg->type = ANSI_COLOR_DEFAULT;
			break;

		/* Bright foreground colors 90-97 */
		case 90: case 91: case 92: case 93:
		case 94: case 95: case 96: case 97:
			fg->type = ANSI_COLOR_256;
			fg->index = code - 90 + 8;
			break;

		/* Bright background colors 100-107 */
		case 100: case 101: case 102: case 103:
		case 104: case 105: case 106: case 107:
			bg->type = ANSI_COLOR_256;
			bg->index = code - 100 + 8;
			break;

		/* Extended color: 38;5;N, 38;2;R;G;B or the colon variants */
		case 38:
			ansi_parse_extended_color(codes, subparam, ncodes, &i, fg);
			break;

		case 48:
			ansi_parse_extended_color(codes, subparam, ncodes, &i, bg);
			break;
		}
	}
}

int
ansi_parse_line(const char *raw, char *stripped, size_t stripped_size,
		struct ansi_span *spans, int max_spans)
{
	struct ansi_color cur_fg = { ANSI_COLOR_DEFAULT, { .index = 0 } };
	struct ansi_color cur_bg = { ANSI_COLOR_DEFAULT, { .index = 0 } };
	int cur_attr = A_NORMAL;
	size_t out = 0;		/* position in stripped output */
	size_t span_start = 0;		/* start of current span in stripped */
	int nspans = 0;
	const char *p = raw;

	if (!stripped_size)
		return 0;

	while (*p && out < stripped_size - 1) {
		if (*p == 0x1b && *(p + 1) == '[') {
			const char *seq_start = p + 2;
			const char *seq_end = seq_start;
			struct ansi_color prev_fg = cur_fg;
			struct ansi_color prev_bg = cur_bg;
			int prev_attr = cur_attr;

			/* A CSI runs until its final byte in 0x40-0x7e; the
			 * parameter and intermediate bytes ahead of it are all
			 * below that range.  'm' selects SGR. */
			while (*seq_end && (*seq_end < 0x40 || *seq_end > 0x7e))
				seq_end++;

			if (*seq_end == 'm') {
				/* Finish current span if it has content */
				if (out > span_start && nspans < max_spans) {
					spans[nspans].fg = prev_fg;
					spans[nspans].bg = prev_bg;
					spans[nspans].attr = prev_attr;
					spans[nspans].offset = span_start;
					spans[nspans].length = out - span_start;
					nspans++;
				}

				ansi_parse_sgr(seq_start, seq_end - seq_start,
						&cur_fg, &cur_bg, &cur_attr);
				span_start = out;
				p = seq_end + 1;
				continue;
			}

			/* Not an SGR sequence; drop the whole thing rather than
			 * letting its parameters show up as text */
			p = *seq_end ? seq_end + 1 : seq_end;
			continue;
		}

		stripped[out++] = *p++;
	}

	stripped[out] = '\0';

	/* Ran out of room: the caller must not treat `stripped` as the whole
	 * line, so report it rather than silently truncating */
	if (*p)
		return -1;

	/* Finish the last span */
	if (out > span_start && nspans < max_spans) {
		spans[nspans].fg = cur_fg;
		spans[nspans].bg = cur_bg;
		spans[nspans].attr = cur_attr;
		spans[nspans].offset = span_start;
		spans[nspans].length = out - span_start;
		nspans++;
	}

	return nspans;
}

/* vim: set ts=8 sw=8 noexpandtab: */
