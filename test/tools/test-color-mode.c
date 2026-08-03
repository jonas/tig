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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tig/ansi.h"
#include "tig/color_mode.h"

static int tests_run = 0;
static int tests_failed = 0;

#define ASSERT_EQ(msg, got, expected) do { \
	tests_run++; \
	if ((got) != (expected)) { \
		fprintf(stderr, "FAIL: %s: expected 0x%x (%d), got 0x%x (%d)\n", \
			msg, (int)(expected), (int)(expected), \
			(int)(got), (int)(got)); \
		tests_failed++; \
	} \
} while (0)

/* Convenience: build an ansi_color literal */
static struct ansi_color
basic(int idx)
{
	struct ansi_color c = { ANSI_COLOR_BASIC, { .index = idx } };
	return c;
}

static struct ansi_color
indexed(int idx)
{
	struct ansi_color c = { ANSI_COLOR_256, { .index = idx } };
	return c;
}

static struct ansi_color
rgb(unsigned char r, unsigned char g, unsigned char b)
{
	struct ansi_color c;
	c.type = ANSI_COLOR_RGB;
	c.rgb.r = r;
	c.rgb.g = g;
	c.rgb.b = b;
	return c;
}

static struct ansi_color
default_color(void)
{
	struct ansi_color c = { ANSI_COLOR_DEFAULT, { .index = 0 } };
	return c;
}

/*
 * Classifier thresholds.
 *
 * The boundaries are the load-bearing part of the gating logic. If
 * a future ncurses ever reports `COLORS == 256` we still want the
 * 256-palette path, and the truecolor path must not engage unless
 * the terminal really claims direct color (>= 1<<24).
 */
static void
test_classify_basic(void)
{
	ASSERT_EQ("classify: 0 -> BASIC",
		tig_classify_color_mode(0), TIG_COLOR_BASIC);
	ASSERT_EQ("classify: 8 -> BASIC",
		tig_classify_color_mode(8), TIG_COLOR_BASIC);
	ASSERT_EQ("classify: 16 -> BASIC",
		tig_classify_color_mode(16), TIG_COLOR_BASIC);
	ASSERT_EQ("classify: 255 -> BASIC (just below 256 cutoff)",
		tig_classify_color_mode(255), TIG_COLOR_BASIC);
}

static void
test_classify_256(void)
{
	ASSERT_EQ("classify: 256 -> 256 (boundary)",
		tig_classify_color_mode(256), TIG_COLOR_256);
	ASSERT_EQ("classify: 32767 -> 256",
		tig_classify_color_mode(32767), TIG_COLOR_256);
	ASSERT_EQ("classify: (1<<24)-1 -> 256 (just below truecolor cutoff)",
		tig_classify_color_mode((1 << 24) - 1), TIG_COLOR_256);
}

static void
test_classify_truecolor(void)
{
	ASSERT_EQ("classify: 1<<24 -> TRUECOLOR (boundary)",
		tig_classify_color_mode(1 << 24), TIG_COLOR_TRUECOLOR);
	ASSERT_EQ("classify: 0x1000001 -> TRUECOLOR",
		tig_classify_color_mode(0x1000001), TIG_COLOR_TRUECOLOR);
}

/*
 * The four diff-bg palette indices the fork actually uses. These
 * tests are the contract between diff.c and the color_mode module:
 * if these break, the diff bar renders the wrong hue.
 */
static void
test_diff_bg_truecolor_mapping(void)
{
	struct ansi_color c;

	c = indexed(22);  /* dark green */
	ASSERT_EQ("diff_bg: 22 (dark green) -> #005f00",
		ansi_color_to_ncurses_for_mode(&c, TIG_COLOR_TRUECOLOR),
		0x005f00);

	c = indexed(52);  /* dark red */
	ASSERT_EQ("diff_bg: 52 (dark red) -> #5f0000",
		ansi_color_to_ncurses_for_mode(&c, TIG_COLOR_TRUECOLOR),
		0x5f0000);

	c = indexed(65);  /* olive emphasis */
	ASSERT_EQ("diff_bg: 65 (olive) -> #5f875f",
		ansi_color_to_ncurses_for_mode(&c, TIG_COLOR_TRUECOLOR),
		0x5f875f);

	c = indexed(88);  /* medium red emphasis */
	ASSERT_EQ("diff_bg: 88 (medium red) -> #870000",
		ansi_color_to_ncurses_for_mode(&c, TIG_COLOR_TRUECOLOR),
		0x870000);
}

/*
 * In 256-palette mode, the indices are passed through unchanged so
 * ncurses can look them up in the terminal's palette. This is the
 * legacy behaviour the fork worked against before truecolor support.
 */
static void
test_256_passthrough_in_palette_mode(void)
{
	struct ansi_color c;

	c = indexed(22);
	ASSERT_EQ("256: 22 stays 22 in PALETTE_256",
		ansi_color_to_ncurses_for_mode(&c, TIG_COLOR_256), 22);

	c = indexed(52);
	ASSERT_EQ("256: 52 stays 52 in PALETTE_256",
		ansi_color_to_ncurses_for_mode(&c, TIG_COLOR_256), 52);

	c = indexed(231);  /* near-white in the cube */
	ASSERT_EQ("256: 231 stays 231",
		ansi_color_to_ncurses_for_mode(&c, TIG_COLOR_256), 231);
}

/*
 * BASIC fallback: 256 indices coarsen to one of the 8 ANSI colors.
 * The emphasis distinction (52 vs 88, 22 vs 65) is necessarily lost
 * at this tier, but the dominant hue must still come through.
 */
static void
test_256_basic_fallback(void)
{
	struct ansi_color c;

	c = indexed(22);   /* g=1, below threshold -> BLACK */
	ASSERT_EQ("basic: 22 (low green cube coord) -> 0 (black)",
		ansi_color_to_ncurses_for_mode(&c, TIG_COLOR_BASIC), 0);

	c = indexed(52);   /* r=1, below threshold -> BLACK */
	ASSERT_EQ("basic: 52 (low red cube coord) -> 0 (black)",
		ansi_color_to_ncurses_for_mode(&c, TIG_COLOR_BASIC), 0);

	c = indexed(65);   /* r=1, g=2, b=1 -> GREEN bit set */
	ASSERT_EQ("basic: 65 -> 2 (GREEN)",
		ansi_color_to_ncurses_for_mode(&c, TIG_COLOR_BASIC), 2);

	c = indexed(88);   /* r=2, g=0, b=0 -> RED bit set */
	ASSERT_EQ("basic: 88 -> 1 (RED)",
		ansi_color_to_ncurses_for_mode(&c, TIG_COLOR_BASIC), 1);

	c = indexed(196);  /* r=5, g=0, b=0 -> RED */
	ASSERT_EQ("basic: 196 (pure red) -> 1 (RED)",
		ansi_color_to_ncurses_for_mode(&c, TIG_COLOR_BASIC), 1);

	c = indexed(46);   /* r=0, g=5, b=0 -> GREEN */
	ASSERT_EQ("basic: 46 (pure green) -> 2 (GREEN)",
		ansi_color_to_ncurses_for_mode(&c, TIG_COLOR_BASIC), 2);

	c = indexed(21);   /* r=0, g=0, b=5 -> BLUE */
	ASSERT_EQ("basic: 21 (pure blue) -> 4 (BLUE)",
		ansi_color_to_ncurses_for_mode(&c, TIG_COLOR_BASIC), 4);
}

/*
 * Indices 0-15 are the basic ANSI colors and must pass through
 * unchanged in every mode. Direct-color terminfo entries special-
 * case them in setaf/setab so they keep working as palette colors.
 */
static void
test_low_indices_unchanged_in_all_modes(void)
{
	struct ansi_color c;
	int i;

	for (i = 0; i < 16; i++) {
		char msg[64];

		c = indexed(i);

		snprintf(msg, sizeof(msg), "low_idx: %d in BASIC", i);
		ASSERT_EQ(msg,
			ansi_color_to_ncurses_for_mode(&c, TIG_COLOR_BASIC),
			i);

		snprintf(msg, sizeof(msg), "low_idx: %d in 256", i);
		ASSERT_EQ(msg,
			ansi_color_to_ncurses_for_mode(&c, TIG_COLOR_256),
			i);

		snprintf(msg, sizeof(msg), "low_idx: %d in TRUECOLOR", i);
		ASSERT_EQ(msg,
			ansi_color_to_ncurses_for_mode(&c, TIG_COLOR_TRUECOLOR),
			i);
	}
}

/*
 * Basic ANSI 0-7 (the ANSI_COLOR_BASIC type, distinct from
 * ANSI_COLOR_256 indices that happen to be < 8) must always return
 * their literal value. setaf/setab terminfo entries handle these
 * via palette codes even in direct-color mode.
 */
static void
test_basic_type_unchanged_in_all_modes(void)
{
	struct ansi_color c;
	int i;
	int modes[] = { TIG_COLOR_BASIC, TIG_COLOR_256, TIG_COLOR_TRUECOLOR };

	for (i = 0; i < 8; i++) {
		size_t m;
		char msg[64];

		c = basic(i);
		for (m = 0; m < sizeof(modes) / sizeof(modes[0]); m++) {
			snprintf(msg, sizeof(msg),
				 "basic_type: %d in mode %d", i, modes[m]);
			ASSERT_EQ(msg,
				ansi_color_to_ncurses_for_mode(&c, modes[m]),
				i);
		}
	}
}

/*
 * ANSI_COLOR_DEFAULT must always yield -1 (ncurses' default-color
 * sentinel) so assume_default_colors works in every tier.
 */
static void
test_default_color_in_all_modes(void)
{
	struct ansi_color c = default_color();

	ASSERT_EQ("default: BASIC",
		ansi_color_to_ncurses_for_mode(&c, TIG_COLOR_BASIC), -1);
	ASSERT_EQ("default: 256",
		ansi_color_to_ncurses_for_mode(&c, TIG_COLOR_256), -1);
	ASSERT_EQ("default: TRUECOLOR",
		ansi_color_to_ncurses_for_mode(&c, TIG_COLOR_TRUECOLOR), -1);
}

/*
 * Truecolor RGB input: in TRUECOLOR mode it packs into 0xRRGGBB
 * unchanged; in PALETTE_256 it is rounded to the nearest 256-index;
 * in BASIC it coarsens further to one of the 8 ANSI colors.
 */
static void
test_rgb_truecolor_packing(void)
{
	struct ansi_color c = rgb(0x12, 0x34, 0x56);

	ASSERT_EQ("rgb: packed in TRUECOLOR",
		ansi_color_to_ncurses_for_mode(&c, TIG_COLOR_TRUECOLOR),
		0x123456);

	c = rgb(0xff, 0xff, 0xff);
	ASSERT_EQ("rgb: white packs to 0xffffff",
		ansi_color_to_ncurses_for_mode(&c, TIG_COLOR_TRUECOLOR),
		0xffffff);

	c = rgb(0, 0, 0);
	ASSERT_EQ("rgb: black packs to 0x000000",
		ansi_color_to_ncurses_for_mode(&c, TIG_COLOR_TRUECOLOR), 0);
}

static void
test_rgb_palette_round_trip(void)
{
	/* xterm cube anchors land on themselves. */
	struct ansi_color c = rgb(0x5f, 0, 0);  /* exactly index 52 */
	int idx = ansi_color_to_ncurses_for_mode(&c, TIG_COLOR_256);

	ASSERT_EQ("rgb_256: #5f0000 -> index 52", idx, 52);

	c = rgb(0, 0x5f, 0);
	idx = ansi_color_to_ncurses_for_mode(&c, TIG_COLOR_256);
	ASSERT_EQ("rgb_256: #005f00 -> index 22", idx, 22);
}

static void
test_rgb_basic_coarse(void)
{
	struct ansi_color c = rgb(0xff, 0, 0);

	ASSERT_EQ("rgb_basic: red -> 1",
		ansi_color_to_ncurses_for_mode(&c, TIG_COLOR_BASIC), 1);

	c = rgb(0, 0xff, 0);
	ASSERT_EQ("rgb_basic: green -> 2",
		ansi_color_to_ncurses_for_mode(&c, TIG_COLOR_BASIC), 2);

	c = rgb(0, 0, 0xff);
	ASSERT_EQ("rgb_basic: blue -> 4",
		ansi_color_to_ncurses_for_mode(&c, TIG_COLOR_BASIC), 4);
}

/*
 * Grayscale ramp (232..255) in truecolor mode maps to repeating-byte
 * RGB values: ramp value G yields RGB(G, G, G).
 */
static void
test_grayscale_truecolor(void)
{
	struct ansi_color c;

	c = indexed(232);
	ASSERT_EQ("gray: 232 -> 0x080808 (ramp start)",
		ansi_color_to_ncurses_for_mode(&c, TIG_COLOR_TRUECOLOR),
		0x080808);

	c = indexed(255);
	ASSERT_EQ("gray: 255 -> 0xeeeeee (ramp end)",
		ansi_color_to_ncurses_for_mode(&c, TIG_COLOR_TRUECOLOR),
		0xeeeeee);
}

int
main(int argc, const char *argv[])
{
	(void) argc;
	(void) argv;

	test_classify_basic();
	test_classify_256();
	test_classify_truecolor();

	test_diff_bg_truecolor_mapping();
	test_256_passthrough_in_palette_mode();
	test_256_basic_fallback();

	test_low_indices_unchanged_in_all_modes();
	test_basic_type_unchanged_in_all_modes();
	test_default_color_in_all_modes();

	test_rgb_truecolor_packing();
	test_rgb_palette_round_trip();
	test_rgb_basic_coarse();

	test_grayscale_truecolor();

	printf("%d tests, %d passed, %d failed\n",
	       tests_run, tests_run - tests_failed, tests_failed);

	return tests_failed ? 1 : 0;
}

/* vim: set ts=8 sw=8 noexpandtab: */
