/* Copyright (c) 2006-2014 Jonas Fonseca <jonas.fonseca@gmail.com>
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

#ifndef TIG_H
#define TIG_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "compat/compat.h"

#ifndef TIG_VERSION
#define TIG_VERSION "unknown-version"
#endif

#ifndef DEBUG
#define NDEBUG
#endif

/* necessary on Snow Leopard to use WINDOW struct */
#ifdef NCURSES_OPAQUE
#undef NCURSES_OPAQUE
#endif
#define NCURSES_OPAQUE 0


#include <assert.h>
#include <errno.h>
#include <ctype.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#include <fcntl.h>

#include <regex.h>

#include <locale.h>
#include <langinfo.h>
#include <iconv.h>

/* ncurses(3): Must be defined to have extended wide-character functions. */
#define _XOPEN_SOURCE_EXTENDED

#if defined HAVE_NCURSESW_CURSES_H
#  include <ncursesw/curses.h>
#elif defined HAVE_NCURSESW_H
#  include <ncursesw.h>
#elif defined HAVE_NCURSES_CURSES_H
#  include <ncurses/curses.h>
#elif defined HAVE_NCURSES_H
#  include <ncurses.h>
#elif defined HAVE_CURSES_H
#  include <curses.h>
#else
#ifdef WARN_MISSING_CURSES_CONFIGURATION
#  warning SysV or X/Open-compatible Curses installation is required.
#  warning Will assume Curses is found in default include and library path.
#  warning To fix any build issues please use autotools to configure Curses.
#  warning See INSTALL.adoc file for instructions.
#endif
#  include <curses.h>
#endif

#if __GNUC__ >= 3
#define TIG_NORETURN __attribute__((__noreturn__))
#define PRINTF_LIKE(fmt, args) __attribute__((format (printf, fmt, args)))
#else
#define TIG_NORETURN
#define PRINTF_LIKE(fmt, args)
#endif

#define ABS(x)		((x) >= 0  ? (x) : -(x))
#define MIN(x, y)	((x) < (y) ? (x) :  (y))
#define MAX(x, y)	((x) > (y) ? (x) :  (y))

#define ARRAY_SIZE(x)	(sizeof(x) / sizeof(x[0]))
#define STRING_SIZE(x)	(sizeof(x) - 1)

#define SIZEOF_STR	1024	/* Default string size. */
#define SIZEOF_REF	256	/* Size of symbolic or SHA1 ID. */
#define SIZEOF_REV	41	/* Holds a SHA-1 and an ending NUL. */
#define SIZEOF_ARG	32	/* Default argument array size. */

/* This color name can be used to refer to the default term colors. */
#define COLOR_DEFAULT	(-1)

#define ICONV_NONE	((iconv_t) -1)
#ifndef ICONV_CONST
#define ICONV_CONST	/* nothing */
#endif
#define ICONV_TRANSLIT	"//TRANSLIT//IGNORE"

/* The format and size of the date column in the main view. */
#define DATE_FORMAT	"%Y-%m-%d %H:%M"
#define DATE_WIDTH	STRING_SIZE("2006-04-29 14:21")
#define DATE_SHORT_WIDTH	STRING_SIZE("2006-04-29")

#define MIN_VIEW_HEIGHT 4
#define MIN_VIEW_WIDTH  4
#define VSPLIT_SCALE	0.5

#define NULL_ID		"0000000000000000000000000000000000000000"

#define S_ISGITLINK(mode) (((mode) & S_IFMT) == 0160000)

/* Some ASCII-shorthands fitted into the ncurses namespace. */
#define KEY_CTL(x)	((x) & 0x1f) /* KEY_CTL(A) == ^A == \1 */
#define KEY_TAB		'\t'
#define KEY_RETURN	'\r'
#define KEY_ESC		27

void TIG_NORETURN usage(const char *message);

/*
 * Allocation helpers ... Entering macro hell to never be seen again.
 */

#define DEFINE_ALLOCATOR(name, type, chunk_size)				\
static type *									\
name(type **mem, size_t size, size_t increase)					\
{										\
	size_t num_chunks = (size + chunk_size - 1) / chunk_size;		\
	size_t num_chunks_new = (size + increase + chunk_size - 1) / chunk_size;\
	type *tmp = *mem;							\
										\
	if (mem == NULL || num_chunks != num_chunks_new) {			\
		size_t newsize = num_chunks_new * chunk_size * sizeof(type);	\
										\
		tmp = realloc(tmp, newsize);					\
		if (tmp) {							\
			*mem = tmp;						\
			if (num_chunks_new > num_chunks) {			\
				size_t offset = num_chunks * chunk_size;	\
				size_t oldsize = offset * sizeof(type);		\
										\
				memset(tmp + offset, 0,	newsize - oldsize);	\
			}							\
		}								\
	}									\
										\
	return tmp;								\
}

static inline int
count_digits(unsigned long i)
{
	int digits;

	for (digits = 0; i; digits++)
		i /= 10;
	return digits;
}

static inline int
apply_step(double step, int value)
{
	if (step >= 1)
		return (int) step;
	value *= step + 0.01;
	return value ? value : 1;
}

/*
 * Strings.
 */

#define prefixcmp(str1, str2) \
	strncmp(str1, str2, STRING_SIZE(str2))

static inline bool
string_isnumber(const char *str)
{
	int pos;

	for (pos = 0; str[pos]; pos++) {
		if (!isdigit(str[pos]))
			return FALSE;
	}

	return pos > 0;
}

static inline bool
iscommit(char *str)
{
	int pos;

	for (pos = 0; str[pos]; pos++) {
		if (!isxdigit(str[pos]))
			return FALSE;
	}

	return 7 <= pos && pos < SIZEOF_REV;
}

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

static inline int
suffixcmp(const char *str, int slen, const char *suffix)
{
	size_t len = slen >= 0 ? slen : strlen(str);
	size_t suffixlen = strlen(suffix);

	return suffixlen < len ? strcmp(str + len - suffixlen, suffix) : -1;
}

static inline void
string_ncopy_do(char *dst, size_t dstlen, const char *src, size_t srclen)
{
	if (srclen > dstlen - 1)
		srclen = dstlen - 1;

	strncpy(dst, src, srclen);
	dst[srclen] = 0;
}

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

static inline void
string_copy_rev(char *dst, const char *src)
{
	size_t srclen;

	if (!*src)
		return;

	for (srclen = 0; srclen < SIZEOF_REV; srclen++)
		if (isspace(src[srclen]))
			break;

	string_ncopy_do(dst, SIZEOF_REV, src, srclen);
}

static inline void
string_copy_rev_from_commit_line(char *dst, const char *src)
{
	string_copy_rev(dst, src + STRING_SIZE("commit "));
}

#define string_rev_is_null(rev) !strncmp(rev, NULL_ID, STRING_SIZE(NULL_ID))

#define string_add(dst, from, src) \
	string_ncopy_do(dst + (from), sizeof(dst) - (from), src, sizeof(src))

static inline size_t
string_expanded_length(const char *src, size_t srclen, size_t tabsize, size_t max_size)
{
	size_t size, pos;

	for (size = pos = 0; pos < srclen && size < max_size; pos++) {
		if (src[pos] == '\t') {
			size_t expanded = tabsize - (size % tabsize);

			size += expanded;
		} else {
			size++;
		}
	}

	return pos;
}

static inline size_t
string_expand(char *dst, size_t dstlen, const char *src, int tabsize)
{
	size_t size, pos;

	for (size = pos = 0; size < dstlen - 1 && src[pos]; pos++) {
		if (src[pos] == '\t') {
			size_t expanded = tabsize - (size % tabsize);

			if (expanded + size >= dstlen - 1)
				expanded = dstlen - size - 1;
			memcpy(dst + size, "        ", expanded);
			size += expanded;
		} else {
			dst[size++] = src[pos];
		}
	}

	dst[size] = 0;
	return pos;
}

static inline char *
chomp_string(char *name)
{
	int namelen;

	while (isspace(*name))
		name++;

	namelen = strlen(name) - 1;
	while (namelen > 0 && isspace(name[namelen]))
		name[namelen--] = 0;

	return name;
}

static inline bool PRINTF_LIKE(4, 5)
string_nformat(char *buf, size_t bufsize, size_t *bufpos, const char *fmt, ...)
{
	size_t pos = bufpos ? *bufpos : 0;
	int retval;

	FORMAT_BUFFER(buf + pos, bufsize - pos, fmt, retval, FALSE);
	if (bufpos && retval > 0)
		*bufpos = pos + retval;

	return pos >= bufsize ? FALSE : TRUE;
}

#define string_format(buf, fmt, args...) \
	string_nformat(buf, sizeof(buf), NULL, fmt, args)

#define string_format_size(buf, size, fmt, args...) \
	string_nformat(buf, size, NULL, fmt, args)

#define string_format_from(buf, from, fmt, args...) \
	string_nformat(buf, sizeof(buf), from, fmt, args)

static inline int
strcmp_null(const char *s1, const char *s2)
{
	if (!s1 || !s2) {
		return (!!s1) - (!!s2);
	}

	return strcmp(s1, s2);
}

/*
 * Unicode / UTF-8 handling
 *
 * NOTE: Much of the following code for dealing with Unicode is derived from
 * ELinks' UTF-8 code developed by Scrool <scroolik@gmail.com>. Origin file is
 * src/intl/charset.c from the UTF-8 branch commit elinks-0.11.0-g31f2c28.
 */

static inline int
unicode_width(unsigned long c, int tab_size)
{
	if (c >= 0x1100 &&
	   (c <= 0x115f				/* Hangul Jamo */
	    || c == 0x2329
	    || c == 0x232a
	    || (c >= 0x2e80  && c <= 0xa4cf && c != 0x303f)
						/* CJK ... Yi */
	    || (c >= 0xac00  && c <= 0xd7a3)	/* Hangul Syllables */
	    || (c >= 0xf900  && c <= 0xfaff)	/* CJK Compatibility Ideographs */
	    || (c >= 0xfe30  && c <= 0xfe6f)	/* CJK Compatibility Forms */
	    || (c >= 0xff00  && c <= 0xff60)	/* Fullwidth Forms */
	    || (c >= 0xffe0  && c <= 0xffe6)
	    || (c >= 0x20000 && c <= 0x2fffd)
	    || (c >= 0x30000 && c <= 0x3fffd)))
		return 2;
	
	if ((c >= 0x0300 && c <= 0x036f)	/* combining diacretical marks */
	    || (c >= 0x1dc0 && c <= 0x1dff)	/* combining diacretical marks supplement */
	    || (c >= 0x20d0 && c <= 0x20ff)	/* combining diacretical marks for symbols */
	    || (c >= 0xfe20 && c <= 0xfe2f))	/* combining half marks */
		return 0;

	if (c == '\t')
		return tab_size;

	return 1;
}

/* Number of bytes used for encoding a UTF-8 character indexed by first byte.
 * Illegal bytes are set one. */
static const unsigned char utf8_bytes[256] = {
	1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,
	2,2,2,2,2,2,2,2, 2,2,2,2,2,2,2,2, 2,2,2,2,2,2,2,2, 2,2,2,2,2,2,2,2,
	3,3,3,3,3,3,3,3, 3,3,3,3,3,3,3,3, 4,4,4,4,4,4,4,4, 5,5,5,5,6,6,1,1,
};

static inline unsigned char
utf8_char_length(const char *string, const char *end)
{
	int c = *(unsigned char *) string;

	return utf8_bytes[c];
}

/* Decode UTF-8 multi-byte representation into a Unicode character. */
static inline unsigned long
utf8_to_unicode(const char *string, size_t length)
{
	unsigned long unicode;

	switch (length) {
	case 1:
		unicode  =   string[0];
		break;
	case 2:
		unicode  =  (string[0] & 0x1f) << 6;
		unicode +=  (string[1] & 0x3f);
		break;
	case 3:
		unicode  =  (string[0] & 0x0f) << 12;
		unicode += ((string[1] & 0x3f) << 6);
		unicode +=  (string[2] & 0x3f);
		break;
	case 4:
		unicode  =  (string[0] & 0x0f) << 18;
		unicode += ((string[1] & 0x3f) << 12);
		unicode += ((string[2] & 0x3f) << 6);
		unicode +=  (string[3] & 0x3f);
		break;
	case 5:
		unicode  =  (string[0] & 0x0f) << 24;
		unicode += ((string[1] & 0x3f) << 18);
		unicode += ((string[2] & 0x3f) << 12);
		unicode += ((string[3] & 0x3f) << 6);
		unicode +=  (string[4] & 0x3f);
		break;
	case 6:
		unicode  =  (string[0] & 0x01) << 30;
		unicode += ((string[1] & 0x3f) << 24);
		unicode += ((string[2] & 0x3f) << 18);
		unicode += ((string[3] & 0x3f) << 12);
		unicode += ((string[4] & 0x3f) << 6);
		unicode +=  (string[5] & 0x3f);
		break;
	default:
		return 0;
	}

	/* Invalid characters could return the special 0xfffd value but NUL
	 * should be just as good. */
	return unicode > 0xffff ? 0 : unicode;
}

/* Calculates how much of string can be shown within the given maximum width
 * and sets trimmed parameter to non-zero value if all of string could not be
 * shown. If the reserve flag is TRUE, it will reserve at least one
 * trailing character, which can be useful when drawing a delimiter.
 *
 * Returns the number of bytes to output from string to satisfy max_width. */
static inline size_t
utf8_length(const char **start, size_t skip, int *width, size_t max_width, int *trimmed, bool reserve, int tab_size)
{
	const char *string = *start;
	const char *end = strchr(string, '\0');
	unsigned char last_bytes = 0;
	size_t last_ucwidth = 0;

	*width = 0;
	*trimmed = 0;

	while (string < end) {
		unsigned char bytes = utf8_char_length(string, end);
		size_t ucwidth;
		unsigned long unicode;

		if (string + bytes > end)
			break;

		/* Change representation to figure out whether
		 * it is a single- or double-width character. */

		unicode = utf8_to_unicode(string, bytes);
		/* FIXME: Graceful handling of invalid Unicode character. */
		if (!unicode)
			break;

		ucwidth = unicode_width(unicode, tab_size);
		if (skip > 0) {
			skip -= ucwidth <= skip ? ucwidth : skip;
			*start += bytes;
		}
		*width  += ucwidth;
		if (*width > max_width) {
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

/*
 * Global view definition.
 */

#define VIEW_INFO(_) \
	_(MAIN,   main), \
	_(DIFF,   diff), \
	_(LOG,    log), \
	_(TREE,   tree), \
	_(BLOB,   blob), \
	_(BLAME,  blame), \
	_(BRANCH, branch), \
	_(STATUS, status), \
	_(STAGE,  stage), \
	_(STASH,  stash), \
	_(PAGER,  pager), \
	_(HELP,   help)

#endif

/* vim: set ts=8 sw=8 noexpandtab: */
