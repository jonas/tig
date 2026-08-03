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

#include "tig/color_mode.h"

enum tig_color_mode
tig_classify_color_mode(int ncurses_colors)
{
	if (ncurses_colors >= (1 << 24))
		return TIG_COLOR_TRUECOLOR;
	if (ncurses_colors >= 256)
		return TIG_COLOR_256;
	return TIG_COLOR_BASIC;
}

/*
 * Convert an RGB color to the nearest xterm-256 color index.
 * Uses the 6x6x6 color cube (indices 16-231) and grayscale ramp
 * (232-255).
 */
static int
rgb_to_256(unsigned char r, unsigned char g, unsigned char b)
{
	int ri, gi, bi, ci;
	int gray, gray_idx;
	int cube_r, cube_g, cube_b;
	int cube_dist, gray_dist;

	ri = (r < 48) ? 0 : (r < 115) ? 1 : (r - 35) / 40;
	gi = (g < 48) ? 0 : (g < 115) ? 1 : (g - 35) / 40;
	bi = (b < 48) ? 0 : (b < 115) ? 1 : (b - 35) / 40;
	ci = 16 + 36 * ri + 6 * gi + bi;

	cube_r = ri ? 55 + ri * 40 : 0;
	cube_g = gi ? 55 + gi * 40 : 0;
	cube_b = bi ? 55 + bi * 40 : 0;
	cube_dist = (r - cube_r) * (r - cube_r)
		  + (g - cube_g) * (g - cube_g)
		  + (b - cube_b) * (b - cube_b);

	gray = (r + g + b) / 3;
	gray_idx = (gray < 4) ? 0 : (gray > 243) ? 23 : (gray - 4) / 10;
	gray = 8 + gray_idx * 10;
	gray_dist = (r - gray) * (r - gray)
		  + (g - gray) * (g - gray)
		  + (b - gray) * (b - gray);

	return (gray_dist < cube_dist) ? 232 + gray_idx : ci;
}

/*
 * Map an xterm 256-palette index to its packed 24-bit RGB value
 * (the canonical xterm color cube + grayscale ramp).
 */
static int
xterm_256_to_packed_rgb(int idx)
{
	static const int cube_steps[6] = { 0, 0x5f, 0x87, 0xaf, 0xd7, 0xff };

	if (idx < 16 || idx > 255)
		return idx;

	if (idx < 232) {
		int n = idx - 16;
		int r = cube_steps[(n / 36) % 6];
		int g = cube_steps[(n / 6) % 6];
		int b = cube_steps[n % 6];

		return (r << 16) | (g << 8) | b;
	}

	{
		int gray = 8 + (idx - 232) * 10;

		return (gray << 16) | (gray << 8) | gray;
	}
}

/*
 * Coarse fall-back: pick the closest of the 8 basic ANSI colors for
 * a 256-palette index. Used when the terminal can't even render the
 * 256-color palette; the dark/medium emphasis distinction is lost.
 */
static int
xterm_256_to_basic(int idx)
{
	if (idx < 16)
		return idx & 7;

	if (idx < 232) {
		int n = idx - 16;
		int r = (n / 36) % 6;
		int g = (n / 6) % 6;
		int b = n % 6;
		int code = 0;

		if (r > 1) code |= 1;	/* COLOR_RED */
		if (g > 1) code |= 2;	/* COLOR_GREEN */
		if (b > 1) code |= 4;	/* COLOR_BLUE */
		return code;
	}

	{
		int gray = 8 + (idx - 232) * 10;

		return gray > 128 ? 7 : 0;
	}
}

int
ansi_color_to_ncurses_for_mode(const struct ansi_color *color,
			       enum tig_color_mode mode)
{
	switch (color->type) {
	case ANSI_COLOR_DEFAULT:
		return -1;
	case ANSI_COLOR_BASIC:
		return color->index;
	case ANSI_COLOR_256:
		if (color->index < 16)
			return color->index;
		if (mode == TIG_COLOR_TRUECOLOR)
			return xterm_256_to_packed_rgb(color->index);
		if (mode == TIG_COLOR_BASIC)
			return xterm_256_to_basic(color->index);
		return color->index;
	case ANSI_COLOR_RGB:
		if (mode == TIG_COLOR_TRUECOLOR)
			return (color->rgb.r << 16)
			     | (color->rgb.g << 8)
			     |  color->rgb.b;
		if (mode == TIG_COLOR_BASIC)
			return xterm_256_to_basic(
				rgb_to_256(color->rgb.r,
					   color->rgb.g,
					   color->rgb.b));
		return rgb_to_256(color->rgb.r, color->rgb.g, color->rgb.b);
	}
	return -1;
}

/* vim: set ts=8 sw=8 noexpandtab: */
