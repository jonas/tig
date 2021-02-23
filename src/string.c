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

#include "tig/tig.h"
#include "tig/string.h"
#include "compat/utf8proc.h"

/*
 * Strings.
 */

bool
string_isnumber(const char *str)
{
	int pos;

	for (pos = 0; str[pos]; pos++) {
		if (!isdigit(str[pos]))
			return false;
	}

	return pos > 0;
}

bool
iscommit(const char *str)
{
	int pos;

	for (pos = 0; str[pos]; pos++) {
		if (!isxdigit(str[pos]))
			return false;
	}

	return 7 <= pos && pos < SIZEOF_REV;
}

int
suffixcmp(const char *str, int slen, const char *suffix)
{
	size_t len = slen >= 0 ? slen : strlen(str);
	size_t suffixlen = strlen(suffix);

	return suffixlen < len ? strcmp(str + len - suffixlen, suffix) : -1;
}

void
string_ncopy_do(char *dst, size_t dstlen, const char *src, size_t srclen)
{
	if (srclen > dstlen - 1)
		srclen = dstlen - 1;

	strncpy(dst, src, srclen);
	dst[srclen] = 0;
}

void
string_copy_rev(char *dst, const char *src)
{
	size_t srclen;

	if (!*src)
		return;

	for (srclen = 0; srclen < SIZEOF_REV; srclen++)
		if (!src[srclen] || isspace(src[srclen]))
			break;

	string_ncopy_do(dst, SIZEOF_REV, src, srclen);
}

void
string_copy_rev_from_commit_line(char *dst, const char *src)
{
	src += STRING_SIZE("commit ");
	while (*src && !isalnum(*src))
		src++;
	string_copy_rev(dst, src);
}

size_t
string_expand(char *dst, size_t dstlen, const char *src, int srclen, int tabsize)
{
	size_t size, pos;

	for (size = pos = 0; size < dstlen - 1 && (srclen == -1 || pos < srclen) && src[pos]; pos++) {
		const char c = src[pos];

		if (c == '\t') {
			size_t expanded = tabsize - (size % tabsize);

			if (expanded + size >= dstlen - 1)
				expanded = dstlen - size - 1;
			memcpy(dst + size, "        ", expanded);
			size += expanded;
		} else if (isspace(c) || iscntrl(c)) {
			dst[size++] = ' ';
		} else {
			dst[size++] = src[pos];
		}
	}

	dst[size] = 0;
	return pos;
}

char *
string_trim_end(char *name)
{
	int namelen = strlen(name) - 1;

	while (namelen > 0 && isspace(name[namelen]))
		name[namelen--] = 0;

	return name;
}

char *
string_trim(char *name)
{
	while (isspace(*name))
		name++;

	return string_trim_end(name);
}

bool PRINTF_LIKE(4, 5)
string_nformat(char *buf, size_t bufsize, size_t *bufpos, const char *fmt, ...)
{
	size_t pos = bufpos ? *bufpos : 0;
	int retval;

	FORMAT_BUFFER(buf + pos, bufsize - pos, fmt, retval, false);
	if (bufpos && retval > 0)
		*bufpos = pos + retval;

	return pos >= bufsize ? false : true;
}

int
strcmp_null(const char *s1, const char *s2)
{
	if (!s1 || !s2) {
		return (!!s1) - (!!s2);
	}

	return strcmp(s1, s2);
}

int
strcmp_numeric(const char *s1, const char *s2)
{
	int number = 0;
	int num1, num2;

	for (; *s1 && *s2 && *s1 == *s2; s1++, s2++) {
		int c = *s1;

		if (isdigit(c)) {
			number = 10 * number + (c - '0');
		} else {
			number = 0;
		}
	}

	num1 = number * 10 + atoi(s1);
	num2 = number * 10 + atoi(s2);

	if (num1 != num2)
		return num2 - num1;

	if (!!*s1 != !!*s2)
		return !!*s2 - !!*s1;
	return *s1 - *s2;
}

/*
 * Unicode / UTF-8 handling
 *
 * NOTE: Much of the following code for dealing with Unicode is derived from
 * ELinks' UTF-8 code developed by Scrool <scroolik@gmail.com>. Origin file is
 * src/intl/charset.c from the UTF-8 branch commit elinks-0.11.0-g31f2c28.
 *
 * unicode_width() is driven by xterm's mk_wcwidth(), which is the work of
 * Markus Kuhn and Thomas Dickey.
 */

int
unicode_width(unsigned long c, int tab_size)
{
	if (c == '\0')
		/*
		 * xterm mk_wcwidth() returns 0 for NUL, which causes two tig
		 * tests to fail, which seems like a tig bug.  Return 1 here
		 * as a workaround.
		 */
		return 1;
	if (c == '\t')
		return tab_size;

	/* Guaranteed to return 0 for unmapped codepints. */
	return utf8proc_charwidth((utf8proc_int32_t) c);
}

/* Number of bytes used for encoding a UTF-8 character indexed by first byte.
 * Illegal bytes are set one. */
unsigned char
utf8_char_length(const char *string)
{
	utf8proc_int8_t length = utf8proc_utf8class[*(utf8proc_uint8_t *) string];

	return length ? length : 1;
}

/* Decode UTF-8 multi-byte representation into a Unicode character. */
unsigned long
utf8_to_unicode(const char *string, size_t length)
{
	utf8proc_int32_t unicode;

	utf8proc_iterate((const utf8proc_uint8_t *) string, length, &unicode);

	/* Invalid characters could return the special 0xfffd value but NUL
	 * should be just as good. */
	return unicode < 0 ? 0 : unicode;
}

/* Calculates how much of string can be shown within the given maximum width
 * and sets trimmed parameter to non-zero value if all of string could not be
 * shown. If the reserve flag is true, it will reserve at least one
 * trailing character, which can be useful when drawing a delimiter.
 *
 * Returns the number of bytes to output from string to satisfy max_width. */
size_t
utf8_length(const char **start, int max_chars, size_t skip, int *width, size_t max_width, int *trimmed, bool reserve, int tab_size)
{
	const char *string = *start;
	const char *end = max_chars < 0 ? strchr(string, '\0') : string + max_chars;
	utf8proc_ssize_t last_bytes = 0;
	int last_ucwidth = 0;

	*width = 0;
	*trimmed = 0;

	while (string < end) {
		/* Change representation to figure out whether it is a
		 * single- or double-width character and assume a width
		 * and size of 1 for invalid UTF-8 encoding (can be
		 * ISO-8859-1, Windows-1252, ...). */
		utf8proc_int32_t unicode;
		utf8proc_ssize_t bytes = utf8proc_iterate((const utf8proc_uint8_t *) string,
							  end - string, &unicode);
		int ucwidth;

		if (unicode < 0)
			ucwidth = bytes = 1;
		else
			ucwidth = unicode == '\t' ? tab_size - (*width % tab_size)
						  : utf8proc_charwidth(unicode);
		if (skip > 0) {
			skip -= ucwidth <= skip ? ucwidth : skip;
			*start += bytes;
		}
		*width  += ucwidth;
		if (max_width > 0 && *width > max_width) {
			*trimmed = 1;
			*width -= ucwidth;
			if (reserve && *width == max_width) {
				string -= last_bytes;
				*width -= last_ucwidth;
			}
			break;
		}

		string  += bytes;
		if (ucwidth) {
			last_bytes = bytes;
			last_ucwidth = ucwidth;
		} else {
			last_bytes += bytes;
		}
	}

	return string - *start;
}

int
utf8_width_of(const char *text, int max_bytes, int max_width)
{
	int text_width = 0;
	const char *tmp = text;
	int trimmed = false;

	utf8_length(&tmp, max_bytes, 0, &text_width, max_width, &trimmed, false, 1);
	return text_width;
}

static bool
utf8_string_contains(const char *text, int category)
{
	ssize_t textlen = strlen(text);

	while (textlen > 0) {
		utf8proc_int32_t unicode;
		utf8proc_ssize_t slen = utf8proc_iterate((const utf8proc_uint8_t *) text,
							 textlen, &unicode);
		const utf8proc_property_t *property;

		if (slen <= 0)
			break;

		property = utf8proc_get_property(unicode);
		if (property->category & category)
			return true;

		text += slen;
		textlen -= slen;
	}

	return false;
}

bool
utf8_string_contains_uppercase(const char *search)
{
	return utf8_string_contains(search, UTF8PROC_CATEGORY_LU);
}

/* vim: set ts=8 sw=8 noexpandtab: */
