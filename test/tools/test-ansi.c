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

static int tests_run = 0;
static int tests_failed = 0;

#define ASSERT_EQ(msg, got, expected) do { \
	tests_run++; \
	if ((got) != (expected)) { \
		fprintf(stderr, "FAIL: %s: expected %d, got %d\n", \
			msg, (int)(expected), (int)(got)); \
		tests_failed++; \
	} \
} while (0)

#define ASSERT_STR(msg, got, expected) do { \
	tests_run++; \
	if (strcmp((got), (expected)) != 0) { \
		fprintf(stderr, "FAIL: %s: expected '%s', got '%s'\n", \
			msg, expected, got); \
		tests_failed++; \
	} \
} while (0)

static void
test_plain_text(void)
{
	char stripped[256];
	struct ansi_span spans[16];
	int n;

	n = ansi_parse_line("hello world", stripped, sizeof(stripped), spans, 16);
	ASSERT_EQ("plain: nspans", n, 1);
	ASSERT_STR("plain: stripped", stripped, "hello world");
	ASSERT_EQ("plain: span0.offset", spans[0].offset, 0);
	ASSERT_EQ("plain: span0.length", spans[0].length, 11);
	ASSERT_EQ("plain: span0.fg.type", spans[0].fg.type, ANSI_COLOR_DEFAULT);
	ASSERT_EQ("plain: span0.bg.type", spans[0].bg.type, ANSI_COLOR_DEFAULT);
}

static void
test_basic_fg_color(void)
{
	char stripped[256];
	struct ansi_span spans[16];
	int n;

	/* Red foreground: \x1b[31m */
	n = ansi_parse_line("\x1b[31mhello\x1b[0m", stripped, sizeof(stripped), spans, 16);
	ASSERT_EQ("basic_fg: nspans", n, 1);
	ASSERT_STR("basic_fg: stripped", stripped, "hello");
	ASSERT_EQ("basic_fg: span0.fg.type", spans[0].fg.type, ANSI_COLOR_BASIC);
	ASSERT_EQ("basic_fg: span0.fg.index", spans[0].fg.index, 1); /* red */
	ASSERT_EQ("basic_fg: span0.length", spans[0].length, 5);
}

static void
test_256_color(void)
{
	char stripped[256];
	struct ansi_span spans[16];
	int n;

	/* 256-color fg: \x1b[38;5;149m */
	n = ansi_parse_line("\x1b[38;5;149mgreen\x1b[0m", stripped, sizeof(stripped), spans, 16);
	ASSERT_EQ("256: nspans", n, 1);
	ASSERT_STR("256: stripped", stripped, "green");
	ASSERT_EQ("256: span0.fg.type", spans[0].fg.type, ANSI_COLOR_256);
	ASSERT_EQ("256: span0.fg.index", spans[0].fg.index, 149);
}

static void
test_truecolor(void)
{
	char stripped[256];
	struct ansi_span spans[16];
	int n;

	/* Truecolor fg: \x1b[38;2;255;128;0m */
	n = ansi_parse_line("\x1b[38;2;255;128;0morange\x1b[0m", stripped, sizeof(stripped), spans, 16);
	ASSERT_EQ("truecolor: nspans", n, 1);
	ASSERT_STR("truecolor: stripped", stripped, "orange");
	ASSERT_EQ("truecolor: span0.fg.type", spans[0].fg.type, ANSI_COLOR_RGB);
	ASSERT_EQ("truecolor: span0.fg.r", spans[0].fg.rgb.r, 255);
	ASSERT_EQ("truecolor: span0.fg.g", spans[0].fg.rgb.g, 128);
	ASSERT_EQ("truecolor: span0.fg.b", spans[0].fg.rgb.b, 0);
}

static void
test_multiple_spans(void)
{
	char stripped[256];
	struct ansi_span spans[16];
	int n;

	/* Two color spans: red "he" + green "llo" */
	n = ansi_parse_line("\x1b[31mhe\x1b[32mllo\x1b[0m", stripped, sizeof(stripped), spans, 16);
	ASSERT_EQ("multi: nspans", n, 2);
	ASSERT_STR("multi: stripped", stripped, "hello");
	ASSERT_EQ("multi: span0.fg.index", spans[0].fg.index, 1); /* red */
	ASSERT_EQ("multi: span0.offset", spans[0].offset, 0);
	ASSERT_EQ("multi: span0.length", spans[0].length, 2);
	ASSERT_EQ("multi: span1.fg.index", spans[1].fg.index, 2); /* green */
	ASSERT_EQ("multi: span1.offset", spans[1].offset, 2);
	ASSERT_EQ("multi: span1.length", spans[1].length, 3);
}

static void
test_bold_and_color(void)
{
	char stripped[256];
	struct ansi_span spans[16];
	int n;

	/* Bold + blue: \x1b[1;34m */
	n = ansi_parse_line("\x1b[1;34mbold blue\x1b[0m", stripped, sizeof(stripped), spans, 16);
	ASSERT_EQ("bold: nspans", n, 1);
	ASSERT_STR("bold: stripped", stripped, "bold blue");
	ASSERT_EQ("bold: span0.fg.type", spans[0].fg.type, ANSI_COLOR_BASIC);
	ASSERT_EQ("bold: span0.fg.index", spans[0].fg.index, 4); /* blue */
	ASSERT_EQ("bold: span0.attr has A_BOLD", !!(spans[0].attr & A_BOLD), 1);
}

static void
test_reset_mid_line(void)
{
	char stripped[256];
	struct ansi_span spans[16];
	int n;

	/* Color then reset then plain: red "ab" + default "cd" */
	n = ansi_parse_line("\x1b[31mab\x1b[0mcd", stripped, sizeof(stripped), spans, 16);
	ASSERT_EQ("reset: nspans", n, 2);
	ASSERT_STR("reset: stripped", stripped, "abcd");
	ASSERT_EQ("reset: span0.fg.type", spans[0].fg.type, ANSI_COLOR_BASIC);
	ASSERT_EQ("reset: span0.fg.index", spans[0].fg.index, 1);
	ASSERT_EQ("reset: span0.length", spans[0].length, 2);
	ASSERT_EQ("reset: span1.fg.type", spans[1].fg.type, ANSI_COLOR_DEFAULT);
	ASSERT_EQ("reset: span1.length", spans[1].length, 2);
}

static void
test_bg_color(void)
{
	char stripped[256];
	struct ansi_span spans[16];
	int n;

	/* Background: \x1b[48;5;22m */
	n = ansi_parse_line("\x1b[38;5;231;48;5;22mtext\x1b[0m", stripped, sizeof(stripped), spans, 16);
	ASSERT_EQ("bg: nspans", n, 1);
	ASSERT_STR("bg: stripped", stripped, "text");
	ASSERT_EQ("bg: span0.fg.type", spans[0].fg.type, ANSI_COLOR_256);
	ASSERT_EQ("bg: span0.fg.index", spans[0].fg.index, 231);
	ASSERT_EQ("bg: span0.bg.type", spans[0].bg.type, ANSI_COLOR_256);
	ASSERT_EQ("bg: span0.bg.index", spans[0].bg.index, 22);
}

static void
test_bright_colors(void)
{
	char stripped[256];
	struct ansi_span spans[16];
	int n;

	/* Bright red fg: \x1b[91m (should map to 256-color index 9) */
	n = ansi_parse_line("\x1b[91mbright\x1b[0m", stripped, sizeof(stripped), spans, 16);
	ASSERT_EQ("bright: nspans", n, 1);
	ASSERT_EQ("bright: span0.fg.type", spans[0].fg.type, ANSI_COLOR_256);
	ASSERT_EQ("bright: span0.fg.index", spans[0].fg.index, 9); /* bright red = 8+1 */
}

static void
test_reverse_video(void)
{
	char stripped[256];
	struct ansi_span spans[16];
	int n;

	/* Reverse video (used by diff-highlight): \x1b[7m ... \x1b[27m */
	n = ansi_parse_line("ab\x1b[7mcd\x1b[27mef", stripped, sizeof(stripped), spans, 16);
	ASSERT_EQ("reverse: nspans", n, 3);
	ASSERT_STR("reverse: stripped", stripped, "abcdef");
	ASSERT_EQ("reverse: span0.length", spans[0].length, 2);
	ASSERT_EQ("reverse: span0.attr & A_REVERSE", !!(spans[0].attr & A_REVERSE), 0);
	ASSERT_EQ("reverse: span1.length", spans[1].length, 2);
	ASSERT_EQ("reverse: span1.attr & A_REVERSE", !!(spans[1].attr & A_REVERSE), 1);
	ASSERT_EQ("reverse: span2.length", spans[2].length, 2);
	ASSERT_EQ("reverse: span2.attr & A_REVERSE", !!(spans[2].attr & A_REVERSE), 0);
}

static void
test_empty_string(void)
{
	char stripped[256];
	struct ansi_span spans[16];
	int n;

	n = ansi_parse_line("", stripped, sizeof(stripped), spans, 16);
	ASSERT_EQ("empty: nspans", n, 0);
	ASSERT_STR("empty: stripped", stripped, "");
}

static void
test_only_escapes(void)
{
	char stripped[256];
	struct ansi_span spans[16];
	int n;

	/* Just a color set and reset, no visible text */
	n = ansi_parse_line("\x1b[31m\x1b[0m", stripped, sizeof(stripped), spans, 16);
	ASSERT_EQ("only_esc: nspans", n, 0);
	ASSERT_STR("only_esc: stripped", stripped, "");
}

static void
test_bat_real_output(void)
{
	char stripped[256];
	struct ansi_span spans[16];
	int n;

	/* Real bat output for C: #include <stdio.h>
	 * \x1b[38;5;203m#include\x1b[0m\x1b[38;5;141m \x1b[0m\x1b[38;5;186m<stdio.h>\x1b[0m */
	n = ansi_parse_line(
		"\x1b[38;5;203m#include\x1b[0m\x1b[38;5;141m \x1b[0m\x1b[38;5;186m<stdio.h>\x1b[0m",
		stripped, sizeof(stripped), spans, 16);
	ASSERT_EQ("bat: nspans", n, 3);
	ASSERT_STR("bat: stripped", stripped, "#include <stdio.h>");

	ASSERT_EQ("bat: span0.fg.type", spans[0].fg.type, ANSI_COLOR_256);
	ASSERT_EQ("bat: span0.fg.index", spans[0].fg.index, 203);
	ASSERT_EQ("bat: span0.length", spans[0].length, 8); /* #include */

	ASSERT_EQ("bat: span1.fg.index", spans[1].fg.index, 141);
	ASSERT_EQ("bat: span1.length", spans[1].length, 1); /* space */

	ASSERT_EQ("bat: span2.fg.index", spans[2].fg.index, 186);
	ASSERT_EQ("bat: span2.length", spans[2].length, 9); /* <stdio.h> */
}

/*
 * Integration tests: simulate the four setting combos for diff views.
 *
 * These test the ANSI parse + merge pipeline that diff_syntax_highlight_line
 * uses, without needing ncurses or bat.
 */

/* Helper: simulate diff-highlight stripping + emphasis detection.
 * Input: line with reverse-video codes from diff-highlight.
 * Output: clean text + emphasis bitmap. */
static void
strip_diff_highlight(const char *input, char *clean, size_t clean_size,
		     char *emph_map, size_t emph_size)
{
	struct ansi_span dh_spans[64];
	int n, i;
	size_t j;

	memset(emph_map, 0, emph_size);
	n = ansi_parse_line(input, clean, clean_size, dh_spans, 64);
	for (i = 0; i < n; i++) {
		if (!(dh_spans[i].attr & A_REVERSE))
			continue;
		for (j = dh_spans[i].offset;
		     j < dh_spans[i].offset + dh_spans[i].length && j < emph_size;
		     j++)
			emph_map[j] = 1;
	}
}

/* Helper: simulate bat syntax highlighting.
 * Input: clean text. Output: ANSI with 256-color fg spans.
 * For testing, we fake bat output with known colors. */
static int
fake_bat_highlight(const char *text, struct ansi_span *spans, int max_spans)
{
	/* Simulate: keyword "int" gets color 203, rest gets color 231 */
	const char *p = strstr(text, "int");
	int n = 0;

	if (p && p == text && max_spans >= 2) {
		spans[0].fg.type = ANSI_COLOR_256;
		spans[0].fg.index = 203;
		spans[0].bg.type = ANSI_COLOR_DEFAULT;
		spans[0].attr = 0;
		spans[0].offset = 0;
		spans[0].length = 3;

		spans[1].fg.type = ANSI_COLOR_256;
		spans[1].fg.index = 231;
		spans[1].bg.type = ANSI_COLOR_DEFAULT;
		spans[1].attr = 0;
		spans[1].offset = 3;
		spans[1].length = strlen(text) - 3;
		n = 2;
	} else if (max_spans >= 1) {
		spans[0].fg.type = ANSI_COLOR_256;
		spans[0].fg.index = 231;
		spans[0].bg.type = ANSI_COLOR_DEFAULT;
		spans[0].attr = 0;
		spans[0].offset = 0;
		spans[0].length = strlen(text);
		n = 1;
	}
	return n;
}

/* Helper: merge emphasis bitmap into syntax spans (same logic as diff.c) */
static int
merge_emphasis(struct ansi_span *spans, int nspans,
	       const char *emph_map, size_t emph_size, int emph_bg)
{
	struct ansi_span merged[256];
	int nmerged = 0;
	int i;

	for (i = 0; i < nspans && nmerged < 255; i++) {
		size_t pos = spans[i].offset;
		size_t end = pos + spans[i].length;

		while (pos < end && nmerged < 255) {
			bool emph = pos < emph_size && emph_map[pos];
			size_t run = pos;

			while (run < end && run < emph_size
			       && (emph_map[run] ? 1 : 0) == emph)
				run++;
			if (run >= emph_size)
				run = end;

			merged[nmerged] = spans[i];
			merged[nmerged].offset = pos;
			merged[nmerged].length = run - pos;
			if (emph && emph_bg >= 0) {
				merged[nmerged].bg.type = ANSI_COLOR_256;
				merged[nmerged].bg.index = emph_bg;
			}
			nmerged++;
			pos = run;
		}
	}

	memcpy(spans, merged, sizeof(spans[0]) * nmerged);
	return nmerged;
}

/*
 * Combo 1: Neither syntax-highlight nor diff-highlight.
 * Data is plain text. No ANSI parsing needed. Tig uses its default
 * LINE_DIFF_ADD / LINE_DIFF_DEL coloring.
 */
static void
test_combo_neither(void)
{
	const char *line = "int x = 1;";

	/* No ANSI escapes present */
	ASSERT_EQ("neither: no escapes", ansi_has_escapes(line), 0);
}

/*
 * Combo 2: diff-highlight only (no syntax-highlight).
 * Data has reverse-video codes. Tig's existing diff_common_highlight
 * handles this — we just verify the ANSI parser extracts reverse-video.
 */
static void
test_combo_diff_highlight_only(void)
{
	char stripped[256];
	struct ansi_span spans[16];
	int n;

	/* diff-highlight output: "int x = \x1b[7m1\x1b[27m;" */
	n = ansi_parse_line("int x = \x1b[7m1\x1b[27m;", stripped, sizeof(stripped), spans, 16);
	ASSERT_EQ("dh_only: nspans", n, 3);
	ASSERT_STR("dh_only: stripped", stripped, "int x = 1;");
	ASSERT_EQ("dh_only: span0 no reverse", !!(spans[0].attr & A_REVERSE), 0);
	ASSERT_EQ("dh_only: span0.length", spans[0].length, 8); /* "int x = " */
	ASSERT_EQ("dh_only: span1 has reverse", !!(spans[1].attr & A_REVERSE), 1);
	ASSERT_EQ("dh_only: span1.length", spans[1].length, 1); /* "1" */
	ASSERT_EQ("dh_only: span2 no reverse", !!(spans[2].attr & A_REVERSE), 0);
	ASSERT_EQ("dh_only: span2.length", spans[2].length, 1); /* ";" */
}

/*
 * Combo 3: syntax-highlight only (no diff-highlight).
 * Data is plain text. We highlight via bat (faked here) and get syntax spans.
 * No emphasis merging needed.
 */
static void
test_combo_syntax_only(void)
{
	const char *content = "int x = 1;";
	struct ansi_span spans[16];
	int nspans;

	/* No diff-highlight codes in input */
	ASSERT_EQ("syn_only: no escapes", ansi_has_escapes(content), 0);

	/* Fake bat highlighting */
	nspans = fake_bat_highlight(content, spans, 16);
	ASSERT_EQ("syn_only: nspans", nspans, 2);
	ASSERT_EQ("syn_only: span0 'int' color", spans[0].fg.index, 203);
	ASSERT_EQ("syn_only: span0.length", spans[0].length, 3);
	ASSERT_EQ("syn_only: span1 rest color", spans[1].fg.index, 231);
	ASSERT_EQ("syn_only: span1.length", spans[1].length, 7);

	/* No emphasis to merge — bg stays default */
	ASSERT_EQ("syn_only: span0.bg default", spans[0].bg.type, ANSI_COLOR_DEFAULT);
	ASSERT_EQ("syn_only: span1.bg default", spans[1].bg.type, ANSI_COLOR_DEFAULT);
}

/*
 * Combo 4: Both syntax-highlight AND diff-highlight.
 * Data has reverse-video from diff-highlight. We strip it, highlight via bat,
 * then merge emphasis by splitting spans at emphasis boundaries.
 */
static void
test_combo_both(void)
{
	/* diff-highlight marked "1" as changed: "int x = \x1b[7m1\x1b[27m;" */
	const char *dh_input = "int x = \x1b[7m1\x1b[27m;";
	char clean[256];
	char emph_map[256];
	struct ansi_span spans[32];
	int nspans;
	int emph_bg = 65; /* emphasis background color */

	/* Step 1: Strip diff-highlight, record emphasis */
	strip_diff_highlight(dh_input, clean, sizeof(clean), emph_map, sizeof(emph_map));
	ASSERT_STR("both: clean text", clean, "int x = 1;");
	ASSERT_EQ("both: emph[7] (space)", emph_map[7], 0);
	ASSERT_EQ("both: emph[8] (1)", emph_map[8], 1);  /* '1' emphasized */
	ASSERT_EQ("both: emph[9] (;)", emph_map[9], 0);

	/* Step 2: Fake bat highlighting on clean text */
	nspans = fake_bat_highlight(clean, spans, 32);
	ASSERT_EQ("both: bat nspans", nspans, 2);
	/* span0: "int" fg=203, span1: " x = 1;" fg=231 */

	/* Step 3: Merge emphasis — should split span1 at emphasis boundary */
	nspans = merge_emphasis(spans, nspans, emph_map, sizeof(emph_map), emph_bg);

	/* Expected after merge:
	 * span0: "int" (0-3) fg=203, bg=default (no emphasis)
	 * span1: " x = " (3-8) fg=231, bg=default
	 * span2: "1" (8-9) fg=231, bg=65 (emphasized!)
	 * span3: ";" (9-10) fg=231, bg=default
	 */
	ASSERT_EQ("both: merged nspans", nspans, 4);

	ASSERT_EQ("both: span0 offset", spans[0].offset, 0);
	ASSERT_EQ("both: span0 length", spans[0].length, 3);
	ASSERT_EQ("both: span0 fg", spans[0].fg.index, 203);
	ASSERT_EQ("both: span0 bg default", spans[0].bg.type, ANSI_COLOR_DEFAULT);

	ASSERT_EQ("both: span1 offset", spans[1].offset, 3);
	ASSERT_EQ("both: span1 length", spans[1].length, 5);
	ASSERT_EQ("both: span1 fg", spans[1].fg.index, 231);
	ASSERT_EQ("both: span1 bg default", spans[1].bg.type, ANSI_COLOR_DEFAULT);

	ASSERT_EQ("both: span2 offset", spans[2].offset, 8);
	ASSERT_EQ("both: span2 length", spans[2].length, 1);
	ASSERT_EQ("both: span2 fg", spans[2].fg.index, 231);
	ASSERT_EQ("both: span2 bg emphasis", spans[2].bg.type, ANSI_COLOR_256);
	ASSERT_EQ("both: span2 bg color", spans[2].bg.index, emph_bg);

	ASSERT_EQ("both: span3 offset", spans[3].offset, 9);
	ASSERT_EQ("both: span3 length", spans[3].length, 1);
	ASSERT_EQ("both: span3 bg default", spans[3].bg.type, ANSI_COLOR_DEFAULT);
}

/*
 * Combo 4 variant: emphasis at start and end of syntax span.
 */
static void
test_combo_both_edge_emphasis(void)
{
	/* "int" is emphasized: "\x1b[7mint\x1b[27m x = 1;" */
	const char *dh_input = "\x1b[7mint\x1b[27m x = 1;";
	char clean[256];
	char emph_map[256];
	struct ansi_span spans[32];
	int nspans;

	strip_diff_highlight(dh_input, clean, sizeof(clean), emph_map, sizeof(emph_map));
	ASSERT_STR("edge: clean text", clean, "int x = 1;");
	ASSERT_EQ("edge: emph[0]", emph_map[0], 1);
	ASSERT_EQ("edge: emph[2]", emph_map[2], 1);
	ASSERT_EQ("edge: emph[3]", emph_map[3], 0);

	nspans = fake_bat_highlight(clean, spans, 32);
	nspans = merge_emphasis(spans, nspans, emph_map, sizeof(emph_map), 65);

	/* Expected:
	 * span0: "int" (0-3) fg=203, bg=65 (keyword AND emphasized)
	 * span1: " x = 1;" (3-10) fg=231, bg=default
	 */
	ASSERT_EQ("edge: nspans", nspans, 2);
	ASSERT_EQ("edge: span0 bg emphasis", spans[0].bg.type, ANSI_COLOR_256);
	ASSERT_EQ("edge: span0 bg color", spans[0].bg.index, 65);
	ASSERT_EQ("edge: span1 bg default", spans[1].bg.type, ANSI_COLOR_DEFAULT);
}

/*
 * Real bat integration test: pipes C code through bat and verifies
 * that the ANSI output parses into meaningful syntax spans.
 * Skipped (not failed) if bat is not on PATH.
 */
static void
test_bat_pipe_integration(void)
{
	FILE *fp;
	char line[SIZEOF_STR * 2];
	char stripped[SIZEOF_STR];
	struct ansi_span spans[64];
	int n;
	bool has_color = false;

	fp = popen("printf '%s' 'int main() { return 0; }' | bat --color=always --style=plain --paging=never --file-name=test.c - 2>/dev/null", "r");
	if (!fp) {
		fprintf(stderr, "SKIP: bat_pipe: popen failed\n");
		return;
	}

	if (!fgets(line, sizeof(line), fp)) {
		pclose(fp);
		fprintf(stderr, "SKIP: bat_pipe: no output (bat not installed?)\n");
		return;
	}
	pclose(fp);

	/* Strip trailing newline */
	{
		size_t len = strlen(line);
		if (len > 0 && line[len - 1] == '\n')
			line[len - 1] = '\0';
	}

	/* Must have ANSI escapes */
	ASSERT_EQ("bat_pipe: has escapes", ansi_has_escapes(line), 1);

	/* Parse the ANSI output */
	n = ansi_parse_line(line, stripped, sizeof(stripped), spans, 64);

	/* Stripped text must match the input */
	ASSERT_STR("bat_pipe: stripped text", stripped, "int main() { return 0; }");

	/* Must produce multiple spans (at minimum "int" gets a different color) */
	ASSERT_EQ("bat_pipe: nspans > 1", n > 1, 1);

	/* All spans should have 256-color or basic fg (not default for everything) */
	{
		int i;
		for (i = 0; i < n; i++) {
			if (spans[i].fg.type != ANSI_COLOR_DEFAULT) {
				has_color = true;
				break;
			}
		}
	}
	ASSERT_EQ("bat_pipe: has syntax colors", has_color, 1);

	/* First span should cover "int" keyword */
	ASSERT_EQ("bat_pipe: span0 is keyword length", spans[0].length <= 4, 1);
	ASSERT_EQ("bat_pipe: span0 has color", spans[0].fg.type != ANSI_COLOR_DEFAULT, 1);
}

int
main(int argc, const char *argv[])
{
	test_plain_text();
	test_basic_fg_color();
	test_256_color();
	test_truecolor();
	test_multiple_spans();
	test_bold_and_color();
	test_reset_mid_line();
	test_bg_color();
	test_bright_colors();
	test_reverse_video();
	test_empty_string();
	test_only_escapes();
	test_bat_real_output();

	/* Real bat integration */
	test_bat_pipe_integration();

	/* Integration: four setting combos */
	test_combo_neither();
	test_combo_diff_highlight_only();
	test_combo_syntax_only();
	test_combo_both();
	test_combo_both_edge_emphasis();

	printf("%d tests, %d passed, %d failed\n",
	       tests_run, tests_run - tests_failed, tests_failed);

	return tests_failed ? 1 : 0;
}
