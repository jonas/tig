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

#ifndef TIG_STRING_H
#define TIG_STRING_H

#include "tig/tig.h"
#include "tig/string.h"

/*
 * Strings.
 */

#define prefixcmp(str1, str2) \
	strncmp(str1, str2, STRING_SIZE(str2))

bool string_isnumber(const char *str);
bool iscommit(const char *str);
#define get_graph_indent(str) strspn(str, "*|\\/_ ")

static inline int
ascii_toupper(int c)
{
	if (c >= 'a' && c <= 'z')
		c &= ~0x20;
	return c;
}

static inline int
ascii_tolower(int c)
{
	if (c >= 'A' && c <= 'Z')
		c |= 0x20;
	return c;
}

int suffixcmp(const char *str, int slen, const char *suffix);

void string_ncopy_do(char *dst, size_t dstlen, const char *src, size_t srclen);

/* Shorthands for safely copying into a fixed buffer. */

#define FORMAT_BUFFER(buf, bufsize, fmt, retval, allow_truncate) \
	do { \
		va_list args; \
		va_start(args, fmt); \
		retval = vsnprintf(buf, bufsize, fmt, args); \
		va_end(args); \
		if (retval >= (bufsize) && allow_truncate) { \
			(buf)[(bufsize) - 1] = 0; \
			(buf)[(bufsize) - 2] = '.'; \
			(buf)[(bufsize) - 3] = '.'; \
			(buf)[(bufsize) - 4] = '.'; \
			retval = (bufsize) - 1; \
		} else if (retval < 0 || retval >= (bufsize)) { \
			retval = -1; \
		} \
	} while (0)

#define string_copy(dst, src) \
	string_ncopy_do(dst, sizeof(dst), src, sizeof(src))

#define string_ncopy(dst, src, srclen) \
	string_ncopy_do(dst, sizeof(dst), src, srclen)

void string_copy_rev(char *dst, const char *src);
void string_copy_rev_from_commit_line(char *dst, const char *src);

#define string_rev_is_null(rev) !strncmp(rev, NULL_ID, STRING_SIZE(NULL_ID))

#define string_add(dst, from, src) \
	string_ncopy_do(dst + (from), sizeof(dst) - (from), src, sizeof(src))

#define string_concat_path(dst, path1, path2) \
	string_format(dst, !*path1 || path1[strlen(path1) - 1] == '/' ? "%s%s" : "%s/%s", path1, path2)

size_t string_expand(char *dst, size_t dstlen, const char *src, int srclen, int tabsize);

char *string_trim_end(char *name);
char *string_trim(char *name);

bool PRINTF_LIKE(4, 5) string_nformat(char *buf, size_t bufsize, size_t *bufpos, const char *fmt, ...);

#define string_format(buf, fmt, ...) \
	string_nformat(buf, sizeof(buf), NULL, fmt, ## __VA_ARGS__)

#define string_format_from(buf, from, fmt, ...) \
	string_nformat(buf, sizeof(buf), from, fmt, ## __VA_ARGS__)

int strcmp_null(const char *s1, const char *s2);
int strcmp_numeric(const char *s1, const char *s2);

/*
 * Unicode / UTF-8 handling
 */

int unicode_width(unsigned long c, int tab_size);

unsigned char utf8_char_length(const char *string);

/* Decode UTF-8 multi-byte representation into a Unicode character. */
unsigned long utf8_to_unicode(const char *string, size_t length);

/* Calculates how much of string can be shown within the given maximum width
 * and sets trimmed parameter to non-zero value if all of string could not be
 * shown. If the reserve flag is TRUE, it will reserve at least one
 * trailing character, which can be useful when drawing a delimiter.
 *
 * Returns the number of bytes to output from string to satisfy max_width. */
size_t utf8_length(const char **start, int max_bytes, size_t skip, int *width, size_t max_width, int *trimmed, bool reserve, int tab_size);

int utf8_width_of(const char *text, int max_bytes, int max_width);
#define utf8_width_max(text, width) utf8_width_of(text, -1, width)
#define utf8_width(text) utf8_width_max(text, -1)

bool utf8_string_contains_uppercase(const char *text);

#endif
/* vim: set ts=8 sw=8 noexpandtab: */
