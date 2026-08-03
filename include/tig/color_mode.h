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

#ifndef TIG_COLOR_MODE_H
#define TIG_COLOR_MODE_H

#include "tig/ansi.h"

/*
 * Runtime color-support tier.
 *
 * In direct-color mode (TERM=*-direct) ncurses treats color *numbers*
 * as packed 24-bit RGB. Passing a 256-palette index like 52 yields
 * RGB(0,0,52) — nearly black — instead of dark red. Code that wants
 * a specific hue must translate palette indices to real RGB.
 */
enum tig_color_mode {
	TIG_COLOR_BASIC,	/* < 256: coarse 8-color ANSI fallback */
	TIG_COLOR_256,		/* 256-palette indices work as-is */
	TIG_COLOR_TRUECOLOR	/* numbers are packed 24-bit RGB */
};

/*
 * Pure classification of an ncurses COLORS count into a tier.
 * Separated from the ncurses dependency so it is unit-testable.
 */
enum tig_color_mode tig_classify_color_mode(int ncurses_colors);

/*
 * Translate an ANSI color descriptor to the int value that should be
 * handed to init_extended_pair under the given color mode.
 *
 *  - Basic ANSI 0-7 are returned unchanged in every mode (terminfo
 *    setaf/setab entries special-case them even in direct mode).
 *  - 256-palette indices are returned unchanged in TIG_COLOR_256,
 *    converted to packed RGB in TIG_COLOR_TRUECOLOR, and approximated
 *    to a basic ANSI color in TIG_COLOR_BASIC.
 *  - Truecolor (24-bit) input is packed into 0xRRGGBB in TRUECOLOR
 *    mode, mapped to the nearest xterm-256 index in PALETTE_256, and
 *    coarsened to basic ANSI in BASIC mode.
 *  - ANSI_COLOR_DEFAULT always returns -1 (ncurses default-color).
 */
int ansi_color_to_ncurses_for_mode(const struct ansi_color *color,
				   enum tig_color_mode mode);

#endif
/* vim: set ts=8 sw=8 noexpandtab: */
