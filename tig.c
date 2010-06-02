/* Copyright (c) 2006-2010 Jonas Fonseca <fonseca@diku.dk>
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

#ifndef TIG_VERSION
#define TIG_VERSION "unknown-version"
#endif

#ifndef DEBUG
#define NDEBUG
#endif

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

#ifdef HAVE_NCURSESW_NCURSES_H
#include <ncursesw/ncurses.h>
#else
#ifdef HAVE_NCURSES_NCURSES_H
#include <ncurses/ncurses.h>
#else
#include <ncurses.h>
#endif
#endif

#if __GNUC__ >= 3
#define __NORETURN __attribute__((__noreturn__))
#else
#define __NORETURN
#endif

static void __NORETURN die(const char *err, ...);
static void warn(const char *msg, ...);
static void report(const char *msg, ...);

#define ABS(x)		((x) >= 0  ? (x) : -(x))
#define MIN(x, y)	((x) < (y) ? (x) :  (y))
#define MAX(x, y)	((x) > (y) ? (x) :  (y))

#define ARRAY_SIZE(x)	(sizeof(x) / sizeof(x[0]))
#define STRING_SIZE(x)	(sizeof(x) - 1)

#define SIZEOF_STR	1024	/* Default string size. */
#define SIZEOF_REF	256	/* Size of symbolic or SHA1 ID. */
#define SIZEOF_REV	41	/* Holds a SHA-1 and an ending NUL. */
#define SIZEOF_ARG	32	/* Default argument array size. */

/* Revision graph */

#define REVGRAPH_INIT	'I'
#define REVGRAPH_MERGE	'M'
#define REVGRAPH_BRANCH	'+'
#define REVGRAPH_COMMIT	'*'
#define REVGRAPH_BOUND	'^'

#define SIZEOF_REVGRAPH	19	/* Size of revision ancestry graphics. */

/* This color name can be used to refer to the default term colors. */
#define COLOR_DEFAULT	(-1)

#define ICONV_NONE	((iconv_t) -1)
#ifndef ICONV_CONST
#define ICONV_CONST	/* nothing */
#endif

/* The format and size of the date column in the main view. */
#define DATE_FORMAT	"%Y-%m-%d %H:%M"
#define DATE_COLS	STRING_SIZE("2006-04-29 14:21 ")
#define DATE_SHORT_COLS	STRING_SIZE("2006-04-29 ")

#define ID_COLS		8
#define AUTHOR_COLS	19

#define MIN_VIEW_HEIGHT 4

#define NULL_ID		"0000000000000000000000000000000000000000"

#define S_ISGITLINK(mode) (((mode) & S_IFMT) == 0160000)

/* Some ASCII-shorthands fitted into the ncurses namespace. */
#define KEY_TAB		'\t'
#define KEY_RETURN	'\r'
#define KEY_ESC		27


struct ref {
	char id[SIZEOF_REV];	/* Commit SHA1 ID */
	unsigned int head:1;	/* Is it the current HEAD? */
	unsigned int tag:1;	/* Is it a tag? */
	unsigned int ltag:1;	/* If so, is the tag local? */
	unsigned int remote:1;	/* Is it a remote ref? */
	unsigned int tracked:1;	/* Is it the remote for the current HEAD? */
	char name[1];		/* Ref name; tag or head names are shortened. */
};

struct ref_list {
	char id[SIZEOF_REV];	/* Commit SHA1 ID */
	size_t size;		/* Number of refs. */
	struct ref **refs;	/* References for this ID. */
};

static struct ref *get_ref_head();
static struct ref_list *get_ref_list(const char *id);
static void foreach_ref(bool (*visitor)(void *data, const struct ref *ref), void *data);
static int load_refs(void);

enum input_status {
	INPUT_OK,
	INPUT_SKIP,
	INPUT_STOP,
	INPUT_CANCEL
};

typedef enum input_status (*input_handler)(void *data, char *buf, int c);

static char *prompt_input(const char *prompt, input_handler handler, void *data);
static bool prompt_yesno(const char *prompt);

struct menu_item {
	int hotkey;
	const char *text;
	void *data;
};

static bool prompt_menu(const char *prompt, const struct menu_item *items, int *selected);

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
		tmp = realloc(tmp, num_chunks_new * chunk_size * sizeof(type));	\
		if (tmp)							\
			*mem = tmp;						\
	}									\
										\
	return tmp;								\
}

/*
 * String helpers
 */

static inline void
string_ncopy_do(char *dst, size_t dstlen, const char *src, size_t srclen)
{
	if (srclen > dstlen - 1)
		srclen = dstlen - 1;

	strncpy(dst, src, srclen);
	dst[srclen] = 0;
}

/* Shorthands for safely copying into a fixed buffer. */

#define string_copy(dst, src) \
	string_ncopy_do(dst, sizeof(dst), src, sizeof(src))

#define string_ncopy(dst, src, srclen) \
	string_ncopy_do(dst, sizeof(dst), src, srclen)

#define string_copy_rev(dst, src) \
	string_ncopy_do(dst, SIZEOF_REV, src, SIZEOF_REV - 1)

#define string_add(dst, from, src) \
	string_ncopy_do(dst + (from), sizeof(dst) - (from), src, sizeof(src))

static void
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
}

static char *
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

static bool
string_nformat(char *buf, size_t bufsize, size_t *bufpos, const char *fmt, ...)
{
	va_list args;
	size_t pos = bufpos ? *bufpos : 0;

	va_start(args, fmt);
	pos += vsnprintf(buf + pos, bufsize - pos, fmt, args);
	va_end(args);

	if (bufpos)
		*bufpos = pos;

	return pos >= bufsize ? FALSE : TRUE;
}

#define string_format(buf, fmt, args...) \
	string_nformat(buf, sizeof(buf), NULL, fmt, args)

#define string_format_from(buf, from, fmt, args...) \
	string_nformat(buf, sizeof(buf), from, fmt, args)

static int
string_enum_compare(const char *str1, const char *str2, int len)
{
	size_t i;

#define string_enum_sep(x) ((x) == '-' || (x) == '_' || (x) == '.')

	/* Diff-Header == DIFF_HEADER */
	for (i = 0; i < len; i++) {
		if (toupper(str1[i]) == toupper(str2[i]))
			continue;

		if (string_enum_sep(str1[i]) &&
		    string_enum_sep(str2[i]))
			continue;

		return str1[i] - str2[i];
	}

	return 0;
}

#define enum_equals(entry, str, len) \
	((entry).namelen == (len) && !string_enum_compare((entry).name, str, len))

struct enum_map {
	const char *name;
	int namelen;
	int value;
};

#define ENUM_MAP(name, value) { name, STRING_SIZE(name), value }

static char *
enum_map_name(const char *name, size_t namelen)
{
	static char buf[SIZEOF_STR];
	int bufpos;

	for (bufpos = 0; bufpos <= namelen; bufpos++) {
		buf[bufpos] = tolower(name[bufpos]);
		if (buf[bufpos] == '_')
			buf[bufpos] = '-';
	}

	buf[bufpos] = 0;
	return buf;
}

#define enum_name(entry) enum_map_name((entry).name, (entry).namelen)

static bool
map_enum_do(const struct enum_map *map, size_t map_size, int *value, const char *name)
{
	size_t namelen = strlen(name);
	int i;

	for (i = 0; i < map_size; i++)
		if (enum_equals(map[i], name, namelen)) {
			*value = map[i].value;
			return TRUE;
		}

	return FALSE;
}

#define map_enum(attr, map, name) \
	map_enum_do(map, ARRAY_SIZE(map), attr, name)

#define prefixcmp(str1, str2) \
	strncmp(str1, str2, STRING_SIZE(str2))

static inline int
suffixcmp(const char *str, int slen, const char *suffix)
{
	size_t len = slen >= 0 ? slen : strlen(str);
	size_t suffixlen = strlen(suffix);

	return suffixlen < len ? strcmp(str + len - suffixlen, suffix) : -1;
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
static size_t
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
		last_bytes = ucwidth ? bytes : 0;
		last_ucwidth = ucwidth;
	}

	return string - *start;
}


#define DATE_INFO \
	DATE_(NO), \
	DATE_(DEFAULT), \
	DATE_(LOCAL), \
	DATE_(RELATIVE), \
	DATE_(SHORT)

enum date {
#define DATE_(name) DATE_##name
	DATE_INFO
#undef	DATE_
};

static const struct enum_map date_map[] = {
#define DATE_(name) ENUM_MAP(#name, DATE_##name)
	DATE_INFO
#undef	DATE_
};

struct time {
	time_t sec;
	int tz;
};

static inline int timecmp(const struct time *t1, const struct time *t2)
{
	return t1->sec - t2->sec;
}

static const char *
mkdate(const struct time *time, enum date date)
{
	static char buf[DATE_COLS + 1];
	static const struct enum_map reldate[] = {
		{ "second", 1,			60 * 2 },
		{ "minute", 60,			60 * 60 * 2 },
		{ "hour",   60 * 60,		60 * 60 * 24 * 2 },
		{ "day",    60 * 60 * 24,	60 * 60 * 24 * 7 * 2 },
		{ "week",   60 * 60 * 24 * 7,	60 * 60 * 24 * 7 * 5 },
		{ "month",  60 * 60 * 24 * 30,	60 * 60 * 24 * 30 * 12 },
	};
	struct tm tm;

	if (!date || !time || !time->sec)
		return "";

	if (date == DATE_RELATIVE) {
		struct timeval now;
		time_t date = time->sec + time->tz;
		time_t seconds;
		int i;

		gettimeofday(&now, NULL);
		seconds = now.tv_sec < date ? date - now.tv_sec : now.tv_sec - date;
		for (i = 0; i < ARRAY_SIZE(reldate); i++) {
			if (seconds >= reldate[i].value)
				continue;

			seconds /= reldate[i].namelen;
			if (!string_format(buf, "%ld %s%s %s",
					   seconds, reldate[i].name,
					   seconds > 1 ? "s" : "",
					   now.tv_sec >= date ? "ago" : "ahead"))
				break;
			return buf;
		}
	}

	if (date == DATE_LOCAL) {
		time_t date = time->sec + time->tz;
		localtime_r(&date, &tm);
	}
	else {
		gmtime_r(&time->sec, &tm);
	}
	return strftime(buf, sizeof(buf), DATE_FORMAT, &tm) ? buf : NULL;
}


#define AUTHOR_VALUES \
	AUTHOR_(NO), \
	AUTHOR_(FULL), \
	AUTHOR_(ABBREVIATED)

enum author {
#define AUTHOR_(name) AUTHOR_##name
	AUTHOR_VALUES,
#undef	AUTHOR_
	AUTHOR_DEFAULT = AUTHOR_FULL
};

static const struct enum_map author_map[] = {
#define AUTHOR_(name) ENUM_MAP(#name, AUTHOR_##name)
	AUTHOR_VALUES
#undef	AUTHOR_
};

static const char *
get_author_initials(const char *author)
{
	static char initials[AUTHOR_COLS * 6 + 1];
	size_t pos = 0;
	const char *end = strchr(author, '\0');

#define is_initial_sep(c) (isspace(c) || ispunct(c) || (c) == '@' || (c) == '-')

	memset(initials, 0, sizeof(initials));
	while (author < end) {
		unsigned char bytes;
		size_t i;

		while (is_initial_sep(*author))
			author++;

		bytes = utf8_char_length(author, end);
		if (bytes < sizeof(initials) - 1 - pos) {
			while (bytes--) {
				initials[pos++] = *author++;
			}
		}

		for (i = pos; author < end && !is_initial_sep(*author); author++) {
			if (i < sizeof(initials) - 1)
				initials[i++] = *author;
		}

		initials[i++] = 0;
	}

	return initials;
}


static bool
argv_from_string(const char *argv[SIZEOF_ARG], int *argc, char *cmd)
{
	int valuelen;

	while (*cmd && *argc < SIZEOF_ARG && (valuelen = strcspn(cmd, " \t"))) {
		bool advance = cmd[valuelen] != 0;

		cmd[valuelen] = 0;
		argv[(*argc)++] = chomp_string(cmd);
		cmd = chomp_string(cmd + valuelen + advance);
	}

	if (*argc < SIZEOF_ARG)
		argv[*argc] = NULL;
	return *argc < SIZEOF_ARG;
}

static bool
argv_from_env(const char **argv, const char *name)
{
	char *env = argv ? getenv(name) : NULL;
	int argc = 0;

	if (env && *env)
		env = strdup(env);
	return !env || argv_from_string(argv, &argc, env);
}

static void
argv_free(const char *argv[])
{
	int argc;

	for (argc = 0; argv[argc]; argc++)
		free((void *) argv[argc]);
	argv[0] = NULL;
}

static bool
argv_copy(const char *dst[], const char *src[])
{
	int argc;

	for (argc = 0; src[argc]; argc++)
		if (!(dst[argc] = strdup(src[argc])))
			return FALSE;
	return TRUE;
}


/*
 * Executing external commands.
 */

enum io_type {
	IO_FD,			/* File descriptor based IO. */
	IO_BG,			/* Execute command in the background. */
	IO_FG,			/* Execute command with same std{in,out,err}. */
	IO_RD,			/* Read only fork+exec IO. */
	IO_WR,			/* Write only fork+exec IO. */
	IO_AP,			/* Append fork+exec output to file. */
};

struct io {
	pid_t pid;		/* PID of spawned process. */
	int pipe;		/* Pipe end for reading or writing. */
	int error;		/* Error status. */
	char *buf;		/* Read buffer. */
	size_t bufalloc;	/* Allocated buffer size. */
	size_t bufsize;		/* Buffer content size. */
	char *bufpos;		/* Current buffer position. */
	unsigned int eof:1;	/* Has end of file been reached. */
};

static void
io_reset(struct io *io)
{
	io->pipe = -1;
	io->pid = 0;
	io->buf = io->bufpos = NULL;
	io->bufalloc = io->bufsize = 0;
	io->error = 0;
	io->eof = 0;
}

static void
io_init(struct io *io)
{
	io_reset(io);
}

static bool
io_open(struct io *io, const char *fmt, ...)
{
	char name[SIZEOF_STR] = "";
	bool fits;
	va_list args;

	io_init(io);

	va_start(args, fmt);
	fits = vsnprintf(name, sizeof(name), fmt, args) < sizeof(name);
	va_end(args);

	if (!fits) {
		io->error = ENAMETOOLONG;
		return FALSE;
	}
	io->pipe = *name ? open(name, O_RDONLY) : STDIN_FILENO;
	if (io->pipe == -1)
		io->error = errno;
	return io->pipe != -1;
}

static bool
io_kill(struct io *io)
{
	return io->pid == 0 || kill(io->pid, SIGKILL) != -1;
}

static bool
io_done(struct io *io)
{
	pid_t pid = io->pid;

	if (io->pipe != -1)
		close(io->pipe);
	free(io->buf);
	io_reset(io);

	while (pid > 0) {
		int status;
		pid_t waiting = waitpid(pid, &status, 0);

		if (waiting < 0) {
			if (errno == EINTR)
				continue;
			io->error = errno;
			return FALSE;
		}

		return waiting == pid &&
		       !WIFSIGNALED(status) &&
		       WIFEXITED(status) &&
		       !WEXITSTATUS(status);
	}

	return TRUE;
}

static bool
io_start(struct io *io, enum io_type type, const char *dir, const char *argv[])
{
	int pipefds[2] = { -1, -1 };

	if ((type == IO_RD || type == IO_WR) && pipe(pipefds) < 0) {
		io->error = errno;
		return FALSE;
	} else if (type == IO_AP) {
		pipefds[1] = io->pipe;
	}

	if ((io->pid = fork())) {
		if (io->pid == -1)
			io->error = errno;
		if (pipefds[!(type == IO_WR)] != -1)
			close(pipefds[!(type == IO_WR)]);
		if (io->pid != -1) {
			io->pipe = pipefds[!!(type == IO_WR)];
			return TRUE;
		}

	} else {
		if (type != IO_FG) {
			int devnull = open("/dev/null", O_RDWR);
			int readfd  = type == IO_WR ? pipefds[0] : devnull;
			int writefd = (type == IO_RD || type == IO_AP)
							? pipefds[1] : devnull;

			dup2(readfd,  STDIN_FILENO);
			dup2(writefd, STDOUT_FILENO);
			dup2(devnull, STDERR_FILENO);

			close(devnull);
			if (pipefds[0] != -1)
				close(pipefds[0]);
			if (pipefds[1] != -1)
				close(pipefds[1]);
		}

		if (dir && *dir && chdir(dir) == -1)
			exit(errno);

		execvp(argv[0], (char *const*) argv);
		exit(errno);
	}

	if (pipefds[!!(type == IO_WR)] != -1)
		close(pipefds[!!(type == IO_WR)]);
	return FALSE;
}

static bool
io_run(struct io *io, const char **argv, const char *dir, enum io_type type)
{
	io_init(io);
	return io_start(io, type, dir, argv);
}

static bool
io_complete(enum io_type type, const char **argv, const char *dir, int fd)
{
	struct io io = {};

	io_init(&io);
	io.pipe = fd;
	return io_start(&io, type, dir, argv) && io_done(&io);
}

static bool
io_run_bg(const char **argv)
{
	return io_complete(IO_BG, argv, NULL, -1);
}

static bool
io_run_fg(const char **argv, const char *dir)
{
	return io_complete(IO_FG, argv, dir, -1);
}

static bool
io_run_append(const char **argv, int fd)
{
	return io_complete(IO_AP, argv, NULL, -1);
}

static bool
io_eof(struct io *io)
{
	return io->eof;
}

static int
io_error(struct io *io)
{
	return io->error;
}

static char *
io_strerror(struct io *io)
{
	return strerror(io->error);
}

static bool
io_can_read(struct io *io)
{
	struct timeval tv = { 0, 500 };
	fd_set fds;

	FD_ZERO(&fds);
	FD_SET(io->pipe, &fds);

	return select(io->pipe + 1, &fds, NULL, NULL, &tv) > 0;
}

static ssize_t
io_read(struct io *io, void *buf, size_t bufsize)
{
	do {
		ssize_t readsize = read(io->pipe, buf, bufsize);

		if (readsize < 0 && (errno == EAGAIN || errno == EINTR))
			continue;
		else if (readsize == -1)
			io->error = errno;
		else if (readsize == 0)
			io->eof = 1;
		return readsize;
	} while (1);
}

DEFINE_ALLOCATOR(io_realloc_buf, char, BUFSIZ)

static char *
io_get(struct io *io, int c, bool can_read)
{
	char *eol;
	ssize_t readsize;

	while (TRUE) {
		if (io->bufsize > 0) {
			eol = memchr(io->bufpos, c, io->bufsize);
			if (eol) {
				char *line = io->bufpos;

				*eol = 0;
				io->bufpos = eol + 1;
				io->bufsize -= io->bufpos - line;
				return line;
			}
		}

		if (io_eof(io)) {
			if (io->bufsize) {
				io->bufpos[io->bufsize] = 0;
				io->bufsize = 0;
				return io->bufpos;
			}
			return NULL;
		}

		if (!can_read)
			return NULL;

		if (io->bufsize > 0 && io->bufpos > io->buf)
			memmove(io->buf, io->bufpos, io->bufsize);

		if (io->bufalloc == io->bufsize) {
			if (!io_realloc_buf(&io->buf, io->bufalloc, BUFSIZ))
				return NULL;
			io->bufalloc += BUFSIZ;
		}

		io->bufpos = io->buf;
		readsize = io_read(io, io->buf + io->bufsize, io->bufalloc - io->bufsize);
		if (io_error(io))
			return NULL;
		io->bufsize += readsize;
	}
}

static bool
io_write(struct io *io, const void *buf, size_t bufsize)
{
	size_t written = 0;

	while (!io_error(io) && written < bufsize) {
		ssize_t size;

		size = write(io->pipe, buf + written, bufsize - written);
		if (size < 0 && (errno == EAGAIN || errno == EINTR))
			continue;
		else if (size == -1)
			io->error = errno;
		else
			written += size;
	}

	return written == bufsize;
}

static bool
io_read_buf(struct io *io, char buf[], size_t bufsize)
{
	char *result = io_get(io, '\n', TRUE);

	if (result) {
		result = chomp_string(result);
		string_ncopy_do(buf, bufsize, result, strlen(result));
	}

	return io_done(io) && result;
}

static bool
io_run_buf(const char **argv, char buf[], size_t bufsize)
{
	struct io io = {};

	io_init(&io);
	return io_start(&io, IO_RD, NULL, argv) && io_read_buf(&io, buf, bufsize);
}

static int
io_load(struct io *io, const char *separators,
	int (*read_property)(char *, size_t, char *, size_t))
{
	char *name;
	int state = OK;

	while (state == OK && (name = io_get(io, '\n', TRUE))) {
		char *value;
		size_t namelen;
		size_t valuelen;

		name = chomp_string(name);
		namelen = strcspn(name, separators);

		if (name[namelen]) {
			name[namelen] = 0;
			value = chomp_string(name + namelen + 1);
			valuelen = strlen(value);

		} else {
			value = "";
			valuelen = 0;
		}

		state = read_property(name, namelen, value, valuelen);
	}

	if (state != ERR && io_error(io))
		state = ERR;
	io_done(io);

	return state;
}

static int
io_run_load(const char **argv, const char *separators,
	    int (*read_property)(char *, size_t, char *, size_t))
{
	struct io io = {};

	io_init(&io);
	if (!io_start(&io, IO_RD, NULL, argv))
		return ERR;
	return io_load(&io, separators, read_property);
}


/*
 * User requests
 */

#define REQ_INFO \
	/* XXX: Keep the view request first and in sync with views[]. */ \
	REQ_GROUP("View switching") \
	REQ_(VIEW_MAIN,		"Show main view"), \
	REQ_(VIEW_DIFF,		"Show diff view"), \
	REQ_(VIEW_LOG,		"Show log view"), \
	REQ_(VIEW_TREE,		"Show tree view"), \
	REQ_(VIEW_BLOB,		"Show blob view"), \
	REQ_(VIEW_BLAME,	"Show blame view"), \
	REQ_(VIEW_BRANCH,	"Show branch view"), \
	REQ_(VIEW_HELP,		"Show help page"), \
	REQ_(VIEW_PAGER,	"Show pager view"), \
	REQ_(VIEW_STATUS,	"Show status view"), \
	REQ_(VIEW_STAGE,	"Show stage view"), \
	\
	REQ_GROUP("View manipulation") \
	REQ_(ENTER,		"Enter current line and scroll"), \
	REQ_(NEXT,		"Move to next"), \
	REQ_(PREVIOUS,		"Move to previous"), \
	REQ_(PARENT,		"Move to parent"), \
	REQ_(VIEW_NEXT,		"Move focus to next view"), \
	REQ_(REFRESH,		"Reload and refresh"), \
	REQ_(MAXIMIZE,		"Maximize the current view"), \
	REQ_(VIEW_CLOSE,	"Close the current view"), \
	REQ_(QUIT,		"Close all views and quit"), \
	\
	REQ_GROUP("View specific requests") \
	REQ_(STATUS_UPDATE,	"Update file status"), \
	REQ_(STATUS_REVERT,	"Revert file changes"), \
	REQ_(STATUS_MERGE,	"Merge file using external tool"), \
	REQ_(STAGE_NEXT,	"Find next chunk to stage"), \
	\
	REQ_GROUP("Cursor navigation") \
	REQ_(MOVE_UP,		"Move cursor one line up"), \
	REQ_(MOVE_DOWN,		"Move cursor one line down"), \
	REQ_(MOVE_PAGE_DOWN,	"Move cursor one page down"), \
	REQ_(MOVE_PAGE_UP,	"Move cursor one page up"), \
	REQ_(MOVE_FIRST_LINE,	"Move cursor to first line"), \
	REQ_(MOVE_LAST_LINE,	"Move cursor to last line"), \
	\
	REQ_GROUP("Scrolling") \
	REQ_(SCROLL_LEFT,	"Scroll two columns left"), \
	REQ_(SCROLL_RIGHT,	"Scroll two columns right"), \
	REQ_(SCROLL_LINE_UP,	"Scroll one line up"), \
	REQ_(SCROLL_LINE_DOWN,	"Scroll one line down"), \
	REQ_(SCROLL_PAGE_UP,	"Scroll one page up"), \
	REQ_(SCROLL_PAGE_DOWN,	"Scroll one page down"), \
	\
	REQ_GROUP("Searching") \
	REQ_(SEARCH,		"Search the view"), \
	REQ_(SEARCH_BACK,	"Search backwards in the view"), \
	REQ_(FIND_NEXT,		"Find next search match"), \
	REQ_(FIND_PREV,		"Find previous search match"), \
	\
	REQ_GROUP("Option manipulation") \
	REQ_(OPTIONS,		"Open option menu"), \
	REQ_(TOGGLE_LINENO,	"Toggle line numbers"), \
	REQ_(TOGGLE_DATE,	"Toggle date display"), \
	REQ_(TOGGLE_DATE_SHORT, "Toggle short (date-only) dates"), \
	REQ_(TOGGLE_AUTHOR,	"Toggle author display"), \
	REQ_(TOGGLE_REV_GRAPH,	"Toggle revision graph visualization"), \
	REQ_(TOGGLE_REFS,	"Toggle reference display (tags/branches)"), \
	REQ_(TOGGLE_SORT_ORDER,	"Toggle ascending/descending sort order"), \
	REQ_(TOGGLE_SORT_FIELD,	"Toggle field to sort by"), \
	\
	REQ_GROUP("Misc") \
	REQ_(PROMPT,		"Bring up the prompt"), \
	REQ_(SCREEN_REDRAW,	"Redraw the screen"), \
	REQ_(SHOW_VERSION,	"Show version information"), \
	REQ_(STOP_LOADING,	"Stop all loading views"), \
	REQ_(EDIT,		"Open in editor"), \
	REQ_(NONE,		"Do nothing")


/* User action requests. */
enum request {
#define REQ_GROUP(help)
#define REQ_(req, help) REQ_##req

	/* Offset all requests to avoid conflicts with ncurses getch values. */
	REQ_UNKNOWN = KEY_MAX + 1,
	REQ_OFFSET,
	REQ_INFO

#undef	REQ_GROUP
#undef	REQ_
};

struct request_info {
	enum request request;
	const char *name;
	int namelen;
	const char *help;
};

static const struct request_info req_info[] = {
#define REQ_GROUP(help)	{ 0, NULL, 0, (help) },
#define REQ_(req, help)	{ REQ_##req, (#req), STRING_SIZE(#req), (help) }
	REQ_INFO
#undef	REQ_GROUP
#undef	REQ_
};

static enum request
get_request(const char *name)
{
	int namelen = strlen(name);
	int i;

	for (i = 0; i < ARRAY_SIZE(req_info); i++)
		if (enum_equals(req_info[i], name, namelen))
			return req_info[i].request;

	return REQ_UNKNOWN;
}


/*
 * Options
 */

/* Option and state variables. */
static enum date opt_date		= DATE_DEFAULT;
static enum author opt_author		= AUTHOR_DEFAULT;
static bool opt_line_number		= FALSE;
static bool opt_line_graphics		= TRUE;
static bool opt_rev_graph		= FALSE;
static bool opt_show_refs		= TRUE;
static int opt_num_interval		= 5;
static double opt_hscroll		= 0.50;
static double opt_scale_split_view	= 2.0 / 3.0;
static int opt_tab_size			= 8;
static int opt_author_cols		= AUTHOR_COLS;
static char opt_path[SIZEOF_STR]	= "";
static char opt_file[SIZEOF_STR]	= "";
static char opt_ref[SIZEOF_REF]		= "";
static char opt_head[SIZEOF_REF]	= "";
static char opt_remote[SIZEOF_REF]	= "";
static char opt_encoding[20]		= "UTF-8";
static iconv_t opt_iconv_in		= ICONV_NONE;
static iconv_t opt_iconv_out		= ICONV_NONE;
static char opt_search[SIZEOF_STR]	= "";
static char opt_cdup[SIZEOF_STR]	= "";
static char opt_prefix[SIZEOF_STR]	= "";
static char opt_git_dir[SIZEOF_STR]	= "";
static signed char opt_is_inside_work_tree	= -1; /* set to TRUE or FALSE */
static char opt_editor[SIZEOF_STR]	= "";
static FILE *opt_tty			= NULL;

#define is_initial_commit()	(!get_ref_head())
#define is_head_commit(rev)	(!strcmp((rev), "HEAD") || (get_ref_head() && !strcmp(rev, get_ref_head()->id)))


/*
 * Line-oriented content detection.
 */

#define LINE_INFO \
LINE(DIFF_HEADER,  "diff --git ",	COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(DIFF_CHUNK,   "@@",		COLOR_MAGENTA,	COLOR_DEFAULT,	0), \
LINE(DIFF_ADD,	   "+",			COLOR_GREEN,	COLOR_DEFAULT,	0), \
LINE(DIFF_DEL,	   "-",			COLOR_RED,	COLOR_DEFAULT,	0), \
LINE(DIFF_INDEX,	"index ",	  COLOR_BLUE,	COLOR_DEFAULT,	0), \
LINE(DIFF_OLDMODE,	"old file mode ", COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(DIFF_NEWMODE,	"new file mode ", COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(DIFF_COPY_FROM,	"copy from",	  COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(DIFF_COPY_TO,	"copy to",	  COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(DIFF_RENAME_FROM,	"rename from",	  COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(DIFF_RENAME_TO,	"rename to",	  COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(DIFF_SIMILARITY,   "similarity ",	  COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(DIFF_DISSIMILARITY,"dissimilarity ", COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(DIFF_TREE,		"diff-tree ",	  COLOR_BLUE,	COLOR_DEFAULT,	0), \
LINE(PP_AUTHOR,	   "Author: ",		COLOR_CYAN,	COLOR_DEFAULT,	0), \
LINE(PP_COMMIT,	   "Commit: ",		COLOR_MAGENTA,	COLOR_DEFAULT,	0), \
LINE(PP_MERGE,	   "Merge: ",		COLOR_BLUE,	COLOR_DEFAULT,	0), \
LINE(PP_DATE,	   "Date:   ",		COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(PP_ADATE,	   "AuthorDate: ",	COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(PP_CDATE,	   "CommitDate: ",	COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(PP_REFS,	   "Refs: ",		COLOR_RED,	COLOR_DEFAULT,	0), \
LINE(COMMIT,	   "commit ",		COLOR_GREEN,	COLOR_DEFAULT,	0), \
LINE(PARENT,	   "parent ",		COLOR_BLUE,	COLOR_DEFAULT,	0), \
LINE(TREE,	   "tree ",		COLOR_BLUE,	COLOR_DEFAULT,	0), \
LINE(AUTHOR,	   "author ",		COLOR_GREEN,	COLOR_DEFAULT,	0), \
LINE(COMMITTER,	   "committer ",	COLOR_MAGENTA,	COLOR_DEFAULT,	0), \
LINE(SIGNOFF,	   "    Signed-off-by", COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(ACKED,	   "    Acked-by",	COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(TESTED,	   "    Tested-by",	COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(REVIEWED,	   "    Reviewed-by",	COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(DEFAULT,	   "",			COLOR_DEFAULT,	COLOR_DEFAULT,	A_NORMAL), \
LINE(CURSOR,	   "",			COLOR_WHITE,	COLOR_GREEN,	A_BOLD), \
LINE(STATUS,	   "",			COLOR_GREEN,	COLOR_DEFAULT,	0), \
LINE(DELIMITER,	   "",			COLOR_MAGENTA,	COLOR_DEFAULT,	0), \
LINE(DATE,         "",			COLOR_BLUE,	COLOR_DEFAULT,	0), \
LINE(MODE,         "",			COLOR_CYAN,	COLOR_DEFAULT,	0), \
LINE(LINE_NUMBER,  "",			COLOR_CYAN,	COLOR_DEFAULT,	0), \
LINE(TITLE_BLUR,   "",			COLOR_WHITE,	COLOR_BLUE,	0), \
LINE(TITLE_FOCUS,  "",			COLOR_WHITE,	COLOR_BLUE,	A_BOLD), \
LINE(MAIN_COMMIT,  "",			COLOR_DEFAULT,	COLOR_DEFAULT,	0), \
LINE(MAIN_TAG,     "",			COLOR_MAGENTA,	COLOR_DEFAULT,	A_BOLD), \
LINE(MAIN_LOCAL_TAG,"",			COLOR_MAGENTA,	COLOR_DEFAULT,	0), \
LINE(MAIN_REMOTE,  "",			COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(MAIN_TRACKED, "",			COLOR_YELLOW,	COLOR_DEFAULT,	A_BOLD), \
LINE(MAIN_REF,     "",			COLOR_CYAN,	COLOR_DEFAULT,	0), \
LINE(MAIN_HEAD,    "",			COLOR_CYAN,	COLOR_DEFAULT,	A_BOLD), \
LINE(MAIN_REVGRAPH,"",			COLOR_MAGENTA,	COLOR_DEFAULT,	0), \
LINE(TREE_HEAD,    "",			COLOR_DEFAULT,	COLOR_DEFAULT,	A_BOLD), \
LINE(TREE_DIR,     "",			COLOR_YELLOW,	COLOR_DEFAULT,	A_NORMAL), \
LINE(TREE_FILE,    "",			COLOR_DEFAULT,	COLOR_DEFAULT,	A_NORMAL), \
LINE(STAT_HEAD,    "",			COLOR_YELLOW,	COLOR_DEFAULT,	0), \
LINE(STAT_SECTION, "",			COLOR_CYAN,	COLOR_DEFAULT,	0), \
LINE(STAT_NONE,    "",			COLOR_DEFAULT,	COLOR_DEFAULT,	0), \
LINE(STAT_STAGED,  "",			COLOR_MAGENTA,	COLOR_DEFAULT,	0), \
LINE(STAT_UNSTAGED,"",			COLOR_MAGENTA,	COLOR_DEFAULT,	0), \
LINE(STAT_UNTRACKED,"",			COLOR_MAGENTA,	COLOR_DEFAULT,	0), \
LINE(HELP_KEYMAP,  "",			COLOR_CYAN,	COLOR_DEFAULT,	0), \
LINE(HELP_GROUP,   "",			COLOR_BLUE,	COLOR_DEFAULT,	0), \
LINE(BLAME_ID,     "",			COLOR_MAGENTA,	COLOR_DEFAULT,	0)

enum line_type {
#define LINE(type, line, fg, bg, attr) \
	LINE_##type
	LINE_INFO,
	LINE_NONE
#undef	LINE
};

struct line_info {
	const char *name;	/* Option name. */
	int namelen;		/* Size of option name. */
	const char *line;	/* The start of line to match. */
	int linelen;		/* Size of string to match. */
	int fg, bg, attr;	/* Color and text attributes for the lines. */
};

static struct line_info line_info[] = {
#define LINE(type, line, fg, bg, attr) \
	{ #type, STRING_SIZE(#type), (line), STRING_SIZE(line), (fg), (bg), (attr) }
	LINE_INFO
#undef	LINE
};

static enum line_type
get_line_type(const char *line)
{
	int linelen = strlen(line);
	enum line_type type;

	for (type = 0; type < ARRAY_SIZE(line_info); type++)
		/* Case insensitive search matches Signed-off-by lines better. */
		if (linelen >= line_info[type].linelen &&
		    !strncasecmp(line_info[type].line, line, line_info[type].linelen))
			return type;

	return LINE_DEFAULT;
}

static inline int
get_line_attr(enum line_type type)
{
	assert(type < ARRAY_SIZE(line_info));
	return COLOR_PAIR(type) | line_info[type].attr;
}

static struct line_info *
get_line_info(const char *name)
{
	size_t namelen = strlen(name);
	enum line_type type;

	for (type = 0; type < ARRAY_SIZE(line_info); type++)
		if (enum_equals(line_info[type], name, namelen))
			return &line_info[type];

	return NULL;
}

static void
init_colors(void)
{
	int default_bg = line_info[LINE_DEFAULT].bg;
	int default_fg = line_info[LINE_DEFAULT].fg;
	enum line_type type;

	start_color();

	if (assume_default_colors(default_fg, default_bg) == ERR) {
		default_bg = COLOR_BLACK;
		default_fg = COLOR_WHITE;
	}

	for (type = 0; type < ARRAY_SIZE(line_info); type++) {
		struct line_info *info = &line_info[type];
		int bg = info->bg == COLOR_DEFAULT ? default_bg : info->bg;
		int fg = info->fg == COLOR_DEFAULT ? default_fg : info->fg;

		init_pair(type, fg, bg);
	}
}

struct line {
	enum line_type type;

	/* State flags */
	unsigned int selected:1;
	unsigned int dirty:1;
	unsigned int cleareol:1;
	unsigned int other:16;

	void *data;		/* User data */
};


/*
 * Keys
 */

struct keybinding {
	int alias;
	enum request request;
};

static struct keybinding default_keybindings[] = {
	/* View switching */
	{ 'm',		REQ_VIEW_MAIN },
	{ 'd',		REQ_VIEW_DIFF },
	{ 'l',		REQ_VIEW_LOG },
	{ 't',		REQ_VIEW_TREE },
	{ 'f',		REQ_VIEW_BLOB },
	{ 'B',		REQ_VIEW_BLAME },
	{ 'H',		REQ_VIEW_BRANCH },
	{ 'p',		REQ_VIEW_PAGER },
	{ 'h',		REQ_VIEW_HELP },
	{ 'S',		REQ_VIEW_STATUS },
	{ 'c',		REQ_VIEW_STAGE },

	/* View manipulation */
	{ 'q',		REQ_VIEW_CLOSE },
	{ KEY_TAB,	REQ_VIEW_NEXT },
	{ KEY_RETURN,	REQ_ENTER },
	{ KEY_UP,	REQ_PREVIOUS },
	{ KEY_DOWN,	REQ_NEXT },
	{ 'R',		REQ_REFRESH },
	{ KEY_F(5),	REQ_REFRESH },
	{ 'O',		REQ_MAXIMIZE },

	/* Cursor navigation */
	{ 'k',		REQ_MOVE_UP },
	{ 'j',		REQ_MOVE_DOWN },
	{ KEY_HOME,	REQ_MOVE_FIRST_LINE },
	{ KEY_END,	REQ_MOVE_LAST_LINE },
	{ KEY_NPAGE,	REQ_MOVE_PAGE_DOWN },
	{ ' ',		REQ_MOVE_PAGE_DOWN },
	{ KEY_PPAGE,	REQ_MOVE_PAGE_UP },
	{ 'b',		REQ_MOVE_PAGE_UP },
	{ '-',		REQ_MOVE_PAGE_UP },

	/* Scrolling */
	{ KEY_LEFT,	REQ_SCROLL_LEFT },
	{ KEY_RIGHT,	REQ_SCROLL_RIGHT },
	{ KEY_IC,	REQ_SCROLL_LINE_UP },
	{ KEY_DC,	REQ_SCROLL_LINE_DOWN },
	{ 'w',		REQ_SCROLL_PAGE_UP },
	{ 's',		REQ_SCROLL_PAGE_DOWN },

	/* Searching */
	{ '/',		REQ_SEARCH },
	{ '?',		REQ_SEARCH_BACK },
	{ 'n',		REQ_FIND_NEXT },
	{ 'N',		REQ_FIND_PREV },

	/* Misc */
	{ 'Q',		REQ_QUIT },
	{ 'z',		REQ_STOP_LOADING },
	{ 'v',		REQ_SHOW_VERSION },
	{ 'r',		REQ_SCREEN_REDRAW },
	{ 'o',		REQ_OPTIONS },
	{ '.',		REQ_TOGGLE_LINENO },
	{ 'D',		REQ_TOGGLE_DATE },
	{ 'A',		REQ_TOGGLE_AUTHOR },
	{ 'g',		REQ_TOGGLE_REV_GRAPH },
	{ 'F',		REQ_TOGGLE_REFS },
	{ 'I',		REQ_TOGGLE_SORT_ORDER },
	{ 'i',		REQ_TOGGLE_SORT_FIELD },
	{ ':',		REQ_PROMPT },
	{ 'u',		REQ_STATUS_UPDATE },
	{ '!',		REQ_STATUS_REVERT },
	{ 'M',		REQ_STATUS_MERGE },
	{ '@',		REQ_STAGE_NEXT },
	{ ',',		REQ_PARENT },
	{ 'e',		REQ_EDIT },
};

#define KEYMAP_INFO \
	KEYMAP_(GENERIC), \
	KEYMAP_(MAIN), \
	KEYMAP_(DIFF), \
	KEYMAP_(LOG), \
	KEYMAP_(TREE), \
	KEYMAP_(BLOB), \
	KEYMAP_(BLAME), \
	KEYMAP_(BRANCH), \
	KEYMAP_(PAGER), \
	KEYMAP_(HELP), \
	KEYMAP_(STATUS), \
	KEYMAP_(STAGE)

enum keymap {
#define KEYMAP_(name) KEYMAP_##name
	KEYMAP_INFO
#undef	KEYMAP_
};

static const struct enum_map keymap_table[] = {
#define KEYMAP_(name) ENUM_MAP(#name, KEYMAP_##name)
	KEYMAP_INFO
#undef	KEYMAP_
};

#define set_keymap(map, name) map_enum(map, keymap_table, name)

struct keybinding_table {
	struct keybinding *data;
	size_t size;
};

static struct keybinding_table keybindings[ARRAY_SIZE(keymap_table)];

static void
add_keybinding(enum keymap keymap, enum request request, int key)
{
	struct keybinding_table *table = &keybindings[keymap];
	size_t i;

	for (i = 0; i < keybindings[keymap].size; i++) {
		if (keybindings[keymap].data[i].alias == key) {
			keybindings[keymap].data[i].request = request;
			return;
		}
	}

	table->data = realloc(table->data, (table->size + 1) * sizeof(*table->data));
	if (!table->data)
		die("Failed to allocate keybinding");
	table->data[table->size].alias = key;
	table->data[table->size++].request = request;

	if (request == REQ_NONE && keymap == KEYMAP_GENERIC) {
		int i;

		for (i = 0; i < ARRAY_SIZE(default_keybindings); i++)
			if (default_keybindings[i].alias == key)
				default_keybindings[i].request = REQ_NONE;
	}
}

/* Looks for a key binding first in the given map, then in the generic map, and
 * lastly in the default keybindings. */
static enum request
get_keybinding(enum keymap keymap, int key)
{
	size_t i;

	for (i = 0; i < keybindings[keymap].size; i++)
		if (keybindings[keymap].data[i].alias == key)
			return keybindings[keymap].data[i].request;

	for (i = 0; i < keybindings[KEYMAP_GENERIC].size; i++)
		if (keybindings[KEYMAP_GENERIC].data[i].alias == key)
			return keybindings[KEYMAP_GENERIC].data[i].request;

	for (i = 0; i < ARRAY_SIZE(default_keybindings); i++)
		if (default_keybindings[i].alias == key)
			return default_keybindings[i].request;

	return (enum request) key;
}


struct key {
	const char *name;
	int value;
};

static const struct key key_table[] = {
	{ "Enter",	KEY_RETURN },
	{ "Space",	' ' },
	{ "Backspace",	KEY_BACKSPACE },
	{ "Tab",	KEY_TAB },
	{ "Escape",	KEY_ESC },
	{ "Left",	KEY_LEFT },
	{ "Right",	KEY_RIGHT },
	{ "Up",		KEY_UP },
	{ "Down",	KEY_DOWN },
	{ "Insert",	KEY_IC },
	{ "Delete",	KEY_DC },
	{ "Hash",	'#' },
	{ "Home",	KEY_HOME },
	{ "End",	KEY_END },
	{ "PageUp",	KEY_PPAGE },
	{ "PageDown",	KEY_NPAGE },
	{ "F1",		KEY_F(1) },
	{ "F2",		KEY_F(2) },
	{ "F3",		KEY_F(3) },
	{ "F4",		KEY_F(4) },
	{ "F5",		KEY_F(5) },
	{ "F6",		KEY_F(6) },
	{ "F7",		KEY_F(7) },
	{ "F8",		KEY_F(8) },
	{ "F9",		KEY_F(9) },
	{ "F10",	KEY_F(10) },
	{ "F11",	KEY_F(11) },
	{ "F12",	KEY_F(12) },
};

static int
get_key_value(const char *name)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(key_table); i++)
		if (!strcasecmp(key_table[i].name, name))
			return key_table[i].value;

	if (strlen(name) == 1 && isprint(*name))
		return (int) *name;

	return ERR;
}

static const char *
get_key_name(int key_value)
{
	static char key_char[] = "'X'";
	const char *seq = NULL;
	int key;

	for (key = 0; key < ARRAY_SIZE(key_table); key++)
		if (key_table[key].value == key_value)
			seq = key_table[key].name;

	if (seq == NULL &&
	    key_value < 127 &&
	    isprint(key_value)) {
		key_char[1] = (char) key_value;
		seq = key_char;
	}

	return seq ? seq : "(no key)";
}

static bool
append_key(char *buf, size_t *pos, const struct keybinding *keybinding)
{
	const char *sep = *pos > 0 ? ", " : "";
	const char *keyname = get_key_name(keybinding->alias);

	return string_nformat(buf, BUFSIZ, pos, "%s%s", sep, keyname);
}

static bool
append_keymap_request_keys(char *buf, size_t *pos, enum request request,
			   enum keymap keymap, bool all)
{
	int i;

	for (i = 0; i < keybindings[keymap].size; i++) {
		if (keybindings[keymap].data[i].request == request) {
			if (!append_key(buf, pos, &keybindings[keymap].data[i]))
				return FALSE;
			if (!all)
				break;
		}
	}

	return TRUE;
}

#define get_key(keymap, request) get_keys(keymap, request, FALSE)

static const char *
get_keys(enum keymap keymap, enum request request, bool all)
{
	static char buf[BUFSIZ];
	size_t pos = 0;
	int i;

	buf[pos] = 0;

	if (!append_keymap_request_keys(buf, &pos, request, keymap, all))
		return "Too many keybindings!";
	if (pos > 0 && !all)
		return buf;

	if (keymap != KEYMAP_GENERIC) {
		/* Only the generic keymap includes the default keybindings when
		 * listing all keys. */
		if (all)
			return buf;

		if (!append_keymap_request_keys(buf, &pos, request, KEYMAP_GENERIC, all))
			return "Too many keybindings!";
		if (pos)
			return buf;
	}

	for (i = 0; i < ARRAY_SIZE(default_keybindings); i++) {
		if (default_keybindings[i].request == request) {
			if (!append_key(buf, &pos, &default_keybindings[i]))
				return "Too many keybindings!";
			if (!all)
				return buf;
		}
	}

	return buf;
}

struct run_request {
	enum keymap keymap;
	int key;
	const char *argv[SIZEOF_ARG];
};

static struct run_request *run_request;
static size_t run_requests;

DEFINE_ALLOCATOR(realloc_run_requests, struct run_request, 8)

static enum request
add_run_request(enum keymap keymap, int key, int argc, const char **argv)
{
	struct run_request *req;

	if (argc >= ARRAY_SIZE(req->argv) - 1)
		return REQ_NONE;

	if (!realloc_run_requests(&run_request, run_requests, 1))
		return REQ_NONE;

	req = &run_request[run_requests];
	req->keymap = keymap;
	req->key = key;
	req->argv[0] = NULL;

	if (!argv_copy(req->argv, argv))
		return REQ_NONE;

	return REQ_NONE + ++run_requests;
}

static struct run_request *
get_run_request(enum request request)
{
	if (request <= REQ_NONE)
		return NULL;
	return &run_request[request - REQ_NONE - 1];
}

static void
add_builtin_run_requests(void)
{
	const char *cherry_pick[] = { "git", "cherry-pick", "%(commit)", NULL };
	const char *checkout[] = { "git", "checkout", "%(branch)", NULL };
	const char *commit[] = { "git", "commit", NULL };
	const char *gc[] = { "git", "gc", NULL };
	struct {
		enum keymap keymap;
		int key;
		int argc;
		const char **argv;
	} reqs[] = {
		{ KEYMAP_MAIN,	  'C', ARRAY_SIZE(cherry_pick) - 1, cherry_pick },
		{ KEYMAP_STATUS,  'C', ARRAY_SIZE(commit) - 1, commit },
		{ KEYMAP_BRANCH,  'C', ARRAY_SIZE(checkout) - 1, checkout },
		{ KEYMAP_GENERIC, 'G', ARRAY_SIZE(gc) - 1, gc },
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(reqs); i++) {
		enum request req = get_keybinding(reqs[i].keymap, reqs[i].key);

		if (req != reqs[i].key)
			continue;
		req = add_run_request(reqs[i].keymap, reqs[i].key, reqs[i].argc, reqs[i].argv);
		if (req != REQ_NONE)
			add_keybinding(reqs[i].keymap, req, reqs[i].key);
	}
}

/*
 * User config file handling.
 */

static int   config_lineno;
static bool  config_errors;
static const char *config_msg;

static const struct enum_map color_map[] = {
#define COLOR_MAP(name) ENUM_MAP(#name, COLOR_##name)
	COLOR_MAP(DEFAULT),
	COLOR_MAP(BLACK),
	COLOR_MAP(BLUE),
	COLOR_MAP(CYAN),
	COLOR_MAP(GREEN),
	COLOR_MAP(MAGENTA),
	COLOR_MAP(RED),
	COLOR_MAP(WHITE),
	COLOR_MAP(YELLOW),
};

static const struct enum_map attr_map[] = {
#define ATTR_MAP(name) ENUM_MAP(#name, A_##name)
	ATTR_MAP(NORMAL),
	ATTR_MAP(BLINK),
	ATTR_MAP(BOLD),
	ATTR_MAP(DIM),
	ATTR_MAP(REVERSE),
	ATTR_MAP(STANDOUT),
	ATTR_MAP(UNDERLINE),
};

#define set_attribute(attr, name)	map_enum(attr, attr_map, name)

static int parse_step(double *opt, const char *arg)
{
	*opt = atoi(arg);
	if (!strchr(arg, '%'))
		return OK;

	/* "Shift down" so 100% and 1 does not conflict. */
	*opt = (*opt - 1) / 100;
	if (*opt >= 1.0) {
		*opt = 0.99;
		config_msg = "Step value larger than 100%";
		return ERR;
	}
	if (*opt < 0.0) {
		*opt = 1;
		config_msg = "Invalid step value";
		return ERR;
	}
	return OK;
}

static int
parse_int(int *opt, const char *arg, int min, int max)
{
	int value = atoi(arg);

	if (min <= value && value <= max) {
		*opt = value;
		return OK;
	}

	config_msg = "Integer value out of bound";
	return ERR;
}

static bool
set_color(int *color, const char *name)
{
	if (map_enum(color, color_map, name))
		return TRUE;
	if (!prefixcmp(name, "color"))
		return parse_int(color, name + 5, 0, 255) == OK;
	return FALSE;
}

/* Wants: object fgcolor bgcolor [attribute] */
static int
option_color_command(int argc, const char *argv[])
{
	struct line_info *info;

	if (argc < 3) {
		config_msg = "Wrong number of arguments given to color command";
		return ERR;
	}

	info = get_line_info(argv[0]);
	if (!info) {
		static const struct enum_map obsolete[] = {
			ENUM_MAP("main-delim",	LINE_DELIMITER),
			ENUM_MAP("main-date",	LINE_DATE),
			ENUM_MAP("main-author",	LINE_AUTHOR),
		};
		int index;

		if (!map_enum(&index, obsolete, argv[0])) {
			config_msg = "Unknown color name";
			return ERR;
		}
		info = &line_info[index];
	}

	if (!set_color(&info->fg, argv[1]) ||
	    !set_color(&info->bg, argv[2])) {
		config_msg = "Unknown color";
		return ERR;
	}

	info->attr = 0;
	while (argc-- > 3) {
		int attr;

		if (!set_attribute(&attr, argv[argc])) {
			config_msg = "Unknown attribute";
			return ERR;
		}
		info->attr |= attr;
	}

	return OK;
}

static int parse_bool(bool *opt, const char *arg)
{
	*opt = (!strcmp(arg, "1") || !strcmp(arg, "true") || !strcmp(arg, "yes"))
		? TRUE : FALSE;
	return OK;
}

static int parse_enum_do(unsigned int *opt, const char *arg,
			 const struct enum_map *map, size_t map_size)
{
	bool is_true;

	assert(map_size > 1);

	if (map_enum_do(map, map_size, (int *) opt, arg))
		return OK;

	if (parse_bool(&is_true, arg) != OK)
		return ERR;

	*opt = is_true ? map[1].value : map[0].value;
	return OK;
}

#define parse_enum(opt, arg, map) \
	parse_enum_do(opt, arg, map, ARRAY_SIZE(map))

static int
parse_string(char *opt, const char *arg, size_t optsize)
{
	int arglen = strlen(arg);

	switch (arg[0]) {
	case '\"':
	case '\'':
		if (arglen == 1 || arg[arglen - 1] != arg[0]) {
			config_msg = "Unmatched quotation";
			return ERR;
		}
		arg += 1; arglen -= 2;
	default:
		string_ncopy_do(opt, optsize, arg, arglen);
		return OK;
	}
}

/* Wants: name = value */
static int
option_set_command(int argc, const char *argv[])
{
	if (argc != 3) {
		config_msg = "Wrong number of arguments given to set command";
		return ERR;
	}

	if (strcmp(argv[1], "=")) {
		config_msg = "No value assigned";
		return ERR;
	}

	if (!strcmp(argv[0], "show-author"))
		return parse_enum(&opt_author, argv[2], author_map);

	if (!strcmp(argv[0], "show-date"))
		return parse_enum(&opt_date, argv[2], date_map);

	if (!strcmp(argv[0], "show-rev-graph"))
		return parse_bool(&opt_rev_graph, argv[2]);

	if (!strcmp(argv[0], "show-refs"))
		return parse_bool(&opt_show_refs, argv[2]);

	if (!strcmp(argv[0], "show-line-numbers"))
		return parse_bool(&opt_line_number, argv[2]);

	if (!strcmp(argv[0], "line-graphics"))
		return parse_bool(&opt_line_graphics, argv[2]);

	if (!strcmp(argv[0], "line-number-interval"))
		return parse_int(&opt_num_interval, argv[2], 1, 1024);

	if (!strcmp(argv[0], "author-width"))
		return parse_int(&opt_author_cols, argv[2], 0, 1024);

	if (!strcmp(argv[0], "horizontal-scroll"))
		return parse_step(&opt_hscroll, argv[2]);

	if (!strcmp(argv[0], "split-view-height"))
		return parse_step(&opt_scale_split_view, argv[2]);

	if (!strcmp(argv[0], "tab-size"))
		return parse_int(&opt_tab_size, argv[2], 1, 1024);

	if (!strcmp(argv[0], "commit-encoding"))
		return parse_string(opt_encoding, argv[2], sizeof(opt_encoding));

	config_msg = "Unknown variable name";
	return ERR;
}

/* Wants: mode request key */
static int
option_bind_command(int argc, const char *argv[])
{
	enum request request;
	int keymap = -1;
	int key;

	if (argc < 3) {
		config_msg = "Wrong number of arguments given to bind command";
		return ERR;
	}

	if (!set_keymap(&keymap, argv[0])) {
		config_msg = "Unknown key map";
		return ERR;
	}

	key = get_key_value(argv[1]);
	if (key == ERR) {
		config_msg = "Unknown key";
		return ERR;
	}

	request = get_request(argv[2]);
	if (request == REQ_UNKNOWN) {
		static const struct enum_map obsolete[] = {
			ENUM_MAP("cherry-pick",		REQ_NONE),
			ENUM_MAP("screen-resize",	REQ_NONE),
			ENUM_MAP("tree-parent",		REQ_PARENT),
		};
		int alias;

		if (map_enum(&alias, obsolete, argv[2])) {
			if (alias != REQ_NONE)
				add_keybinding(keymap, alias, key);
			config_msg = "Obsolete request name";
			return ERR;
		}
	}
	if (request == REQ_UNKNOWN && *argv[2]++ == '!')
		request = add_run_request(keymap, key, argc - 2, argv + 2);
	if (request == REQ_UNKNOWN) {
		config_msg = "Unknown request name";
		return ERR;
	}

	add_keybinding(keymap, request, key);

	return OK;
}

static int
set_option(const char *opt, char *value)
{
	const char *argv[SIZEOF_ARG];
	int argc = 0;

	if (!argv_from_string(argv, &argc, value)) {
		config_msg = "Too many option arguments";
		return ERR;
	}

	if (!strcmp(opt, "color"))
		return option_color_command(argc, argv);

	if (!strcmp(opt, "set"))
		return option_set_command(argc, argv);

	if (!strcmp(opt, "bind"))
		return option_bind_command(argc, argv);

	config_msg = "Unknown option command";
	return ERR;
}

static int
read_option(char *opt, size_t optlen, char *value, size_t valuelen)
{
	int status = OK;

	config_lineno++;
	config_msg = "Internal error";

	/* Check for comment markers, since read_properties() will
	 * only ensure opt and value are split at first " \t". */
	optlen = strcspn(opt, "#");
	if (optlen == 0)
		return OK;

	if (opt[optlen] != 0) {
		config_msg = "No option value";
		status = ERR;

	}  else {
		/* Look for comment endings in the value. */
		size_t len = strcspn(value, "#");

		if (len < valuelen) {
			valuelen = len;
			value[valuelen] = 0;
		}

		status = set_option(opt, value);
	}

	if (status == ERR) {
		warn("Error on line %d, near '%.*s': %s",
		     config_lineno, (int) optlen, opt, config_msg);
		config_errors = TRUE;
	}

	/* Always keep going if errors are encountered. */
	return OK;
}

static void
load_option_file(const char *path)
{
	struct io io = {};

	/* It's OK that the file doesn't exist. */
	if (!io_open(&io, "%s", path))
		return;

	config_lineno = 0;
	config_errors = FALSE;

	if (io_load(&io, " \t", read_option) == ERR ||
	    config_errors == TRUE)
		warn("Errors while loading %s.", path);
}

static int
load_options(void)
{
	const char *home = getenv("HOME");
	const char *tigrc_user = getenv("TIGRC_USER");
	const char *tigrc_system = getenv("TIGRC_SYSTEM");
	char buf[SIZEOF_STR];

	if (!tigrc_system)
		tigrc_system = SYSCONFDIR "/tigrc";
	load_option_file(tigrc_system);

	if (!tigrc_user) {
		if (!home || !string_format(buf, "%s/.tigrc", home))
			return ERR;
		tigrc_user = buf;
	}
	load_option_file(tigrc_user);

	/* Add _after_ loading config files to avoid adding run requests
	 * that conflict with keybindings. */
	add_builtin_run_requests();

	return OK;
}


/*
 * The viewer
 */

struct view;
struct view_ops;

/* The display array of active views and the index of the current view. */
static struct view *display[2];
static unsigned int current_view;

#define foreach_displayed_view(view, i) \
	for (i = 0; i < ARRAY_SIZE(display) && (view = display[i]); i++)

#define displayed_views()	(display[1] != NULL ? 2 : 1)

/* Current head and commit ID */
static char ref_blob[SIZEOF_REF]	= "";
static char ref_commit[SIZEOF_REF]	= "HEAD";
static char ref_head[SIZEOF_REF]	= "HEAD";
static char ref_branch[SIZEOF_REF]	= "";

enum view_type {
	VIEW_MAIN,
	VIEW_DIFF,
	VIEW_LOG,
	VIEW_TREE,
	VIEW_BLOB,
	VIEW_BLAME,
	VIEW_BRANCH,
	VIEW_HELP,
	VIEW_PAGER,
	VIEW_STATUS,
	VIEW_STAGE,
};

struct view {
	enum view_type type;	/* View type */
	const char *name;	/* View name */
	const char *cmd_env;	/* Command line set via environment */
	const char *id;		/* Points to either of ref_{head,commit,blob} */

	struct view_ops *ops;	/* View operations */

	enum keymap keymap;	/* What keymap does this view have */
	bool git_dir;		/* Whether the view requires a git directory. */

	char ref[SIZEOF_REF];	/* Hovered commit reference */
	char vid[SIZEOF_REF];	/* View ID. Set to id member when updating. */

	int height, width;	/* The width and height of the main window */
	WINDOW *win;		/* The main window */
	WINDOW *title;		/* The title window living below the main window */

	/* Navigation */
	unsigned long offset;	/* Offset of the window top */
	unsigned long yoffset;	/* Offset from the window side. */
	unsigned long lineno;	/* Current line number */
	unsigned long p_offset;	/* Previous offset of the window top */
	unsigned long p_yoffset;/* Previous offset from the window side */
	unsigned long p_lineno;	/* Previous current line number */
	bool p_restore;		/* Should the previous position be restored. */

	/* Searching */
	char grep[SIZEOF_STR];	/* Search string */
	regex_t *regex;		/* Pre-compiled regexp */

	/* If non-NULL, points to the view that opened this view. If this view
	 * is closed tig will switch back to the parent view. */
	struct view *parent;
	struct view *prev;

	/* Buffering */
	size_t lines;		/* Total number of lines */
	struct line *line;	/* Line index */
	unsigned int digits;	/* Number of digits in the lines member. */

	/* Drawing */
	struct line *curline;	/* Line currently being drawn. */
	enum line_type curtype;	/* Attribute currently used for drawing. */
	unsigned long col;	/* Column when drawing. */
	bool has_scrolled;	/* View was scrolled. */

	/* Loading */
	const char *argv[SIZEOF_ARG];	/* Shell command arguments. */
	const char *dir;	/* Directory from which to execute. */
	struct io io;
	struct io *pipe;
	time_t start_time;
	time_t update_secs;
};

struct view_ops {
	/* What type of content being displayed. Used in the title bar. */
	const char *type;
	/* Default command arguments. */
	const char **argv;
	/* Open and reads in all view content. */
	bool (*open)(struct view *view);
	/* Read one line; updates view->line. */
	bool (*read)(struct view *view, char *data);
	/* Draw one line; @lineno must be < view->height. */
	bool (*draw)(struct view *view, struct line *line, unsigned int lineno);
	/* Depending on view handle a special requests. */
	enum request (*request)(struct view *view, enum request request, struct line *line);
	/* Search for regexp in a line. */
	bool (*grep)(struct view *view, struct line *line);
	/* Select line */
	void (*select)(struct view *view, struct line *line);
	/* Prepare view for loading */
	bool (*prepare)(struct view *view);
};

static struct view_ops blame_ops;
static struct view_ops blob_ops;
static struct view_ops diff_ops;
static struct view_ops help_ops;
static struct view_ops log_ops;
static struct view_ops main_ops;
static struct view_ops pager_ops;
static struct view_ops stage_ops;
static struct view_ops status_ops;
static struct view_ops tree_ops;
static struct view_ops branch_ops;

#define VIEW_STR(type, name, env, ref, ops, map, git) \
	{ type, name, #env, ref, ops, map, git }

#define VIEW_(id, name, ops, git, ref) \
	VIEW_STR(VIEW_##id, name, TIG_##id##_CMD, ref, ops, KEYMAP_##id, git)

static struct view views[] = {
	VIEW_(MAIN,   "main",   &main_ops,   TRUE,  ref_head),
	VIEW_(DIFF,   "diff",   &diff_ops,   TRUE,  ref_commit),
	VIEW_(LOG,    "log",    &log_ops,    TRUE,  ref_head),
	VIEW_(TREE,   "tree",   &tree_ops,   TRUE,  ref_commit),
	VIEW_(BLOB,   "blob",   &blob_ops,   TRUE,  ref_blob),
	VIEW_(BLAME,  "blame",  &blame_ops,  TRUE,  ref_commit),
	VIEW_(BRANCH, "branch",	&branch_ops, TRUE,  ref_head),
	VIEW_(HELP,   "help",   &help_ops,   FALSE, ""),
	VIEW_(PAGER,  "pager",  &pager_ops,  FALSE, "stdin"),
	VIEW_(STATUS, "status", &status_ops, TRUE,  ""),
	VIEW_(STAGE,  "stage",	&stage_ops,  TRUE,  ""),
};

#define VIEW(req) 	(&views[(req) - REQ_OFFSET - 1])

#define foreach_view(view, i) \
	for (i = 0; i < ARRAY_SIZE(views) && (view = &views[i]); i++)

#define view_is_displayed(view) \
	(view == display[0] || view == display[1])

static enum request
view_request(struct view *view, enum request request)
{
	if (!view || !view->lines)
		return request;
	return view->ops->request(view, request, &view->line[view->lineno]);
}


/*
 * View drawing.
 */

static inline void
set_view_attr(struct view *view, enum line_type type)
{
	if (!view->curline->selected && view->curtype != type) {
		(void) wattrset(view->win, get_line_attr(type));
		wchgat(view->win, -1, 0, type, NULL);
		view->curtype = type;
	}
}

static int
draw_chars(struct view *view, enum line_type type, const char *string,
	   int max_len, bool use_tilde)
{
	static char out_buffer[BUFSIZ * 2];
	int len = 0;
	int col = 0;
	int trimmed = FALSE;
	size_t skip = view->yoffset > view->col ? view->yoffset - view->col : 0;

	if (max_len <= 0)
		return 0;

	len = utf8_length(&string, skip, &col, max_len, &trimmed, use_tilde, opt_tab_size);

	set_view_attr(view, type);
	if (len > 0) {
		if (opt_iconv_out != ICONV_NONE) {
			ICONV_CONST char *inbuf = (ICONV_CONST char *) string;
			size_t inlen = len + 1;

			char *outbuf = out_buffer;
			size_t outlen = sizeof(out_buffer);

			size_t ret;

			ret = iconv(opt_iconv_out, &inbuf, &inlen, &outbuf, &outlen);
			if (ret != (size_t) -1) {
				string = out_buffer;
				len = sizeof(out_buffer) - outlen;
			}
		}

		waddnstr(view->win, string, len);
	}
	if (trimmed && use_tilde) {
		set_view_attr(view, LINE_DELIMITER);
		waddch(view->win, '~');
		col++;
	}

	return col;
}

static int
draw_space(struct view *view, enum line_type type, int max, int spaces)
{
	static char space[] = "                    ";
	int col = 0;

	spaces = MIN(max, spaces);

	while (spaces > 0) {
		int len = MIN(spaces, sizeof(space) - 1);

		col += draw_chars(view, type, space, len, FALSE);
		spaces -= len;
	}

	return col;
}

static bool
draw_text(struct view *view, enum line_type type, const char *string, bool trim)
{
	view->col += draw_chars(view, type, string, view->width + view->yoffset - view->col, trim);
	return view->width + view->yoffset <= view->col;
}

static bool
draw_graphic(struct view *view, enum line_type type, chtype graphic[], size_t size)
{
	size_t skip = view->yoffset > view->col ? view->yoffset - view->col : 0;
	int max = view->width + view->yoffset - view->col;
	int i;

	if (max < size)
		size = max;

	set_view_attr(view, type);
	/* Using waddch() instead of waddnstr() ensures that
	 * they'll be rendered correctly for the cursor line. */
	for (i = skip; i < size; i++)
		waddch(view->win, graphic[i]);

	view->col += size;
	if (size < max && skip <= size)
		waddch(view->win, ' ');
	view->col++;

	return view->width + view->yoffset <= view->col;
}

static bool
draw_field(struct view *view, enum line_type type, const char *text, int len, bool trim)
{
	int max = MIN(view->width + view->yoffset - view->col, len);
	int col;

	if (text)
		col = draw_chars(view, type, text, max - 1, trim);
	else
		col = draw_space(view, type, max - 1, max - 1);

	view->col += col;
	view->col += draw_space(view, LINE_DEFAULT, max - col, max - col);
	return view->width + view->yoffset <= view->col;
}

static bool
draw_date(struct view *view, struct time *time)
{
	const char *date = mkdate(time, opt_date);
	int cols = opt_date == DATE_SHORT ? DATE_SHORT_COLS : DATE_COLS;

	return draw_field(view, LINE_DATE, date, cols, FALSE);
}

static bool
draw_author(struct view *view, const char *author)
{
	bool trim = opt_author_cols == 0 || opt_author_cols > 5;
	bool abbreviate = opt_author == AUTHOR_ABBREVIATED || !trim;

	if (abbreviate && author)
		author = get_author_initials(author);

	return draw_field(view, LINE_AUTHOR, author, opt_author_cols, trim);
}

static bool
draw_mode(struct view *view, mode_t mode)
{
	const char *str;

	if (S_ISDIR(mode))
		str = "drwxr-xr-x";
	else if (S_ISLNK(mode))
		str = "lrwxrwxrwx";
	else if (S_ISGITLINK(mode))
		str = "m---------";
	else if (S_ISREG(mode) && mode & S_IXUSR)
		str = "-rwxr-xr-x";
	else if (S_ISREG(mode))
		str = "-rw-r--r--";
	else
		str = "----------";

	return draw_field(view, LINE_MODE, str, STRING_SIZE("-rw-r--r-- "), FALSE);
}

static bool
draw_lineno(struct view *view, unsigned int lineno)
{
	char number[10];
	int digits3 = view->digits < 3 ? 3 : view->digits;
	int max = MIN(view->width + view->yoffset - view->col, digits3);
	char *text = NULL;
	chtype separator = opt_line_graphics ? ACS_VLINE : '|';

	lineno += view->offset + 1;
	if (lineno == 1 || (lineno % opt_num_interval) == 0) {
		static char fmt[] = "%1ld";

		fmt[1] = '0' + (view->digits <= 9 ? digits3 : 1);
		if (string_format(number, fmt, lineno))
			text = number;
	}
	if (text)
		view->col += draw_chars(view, LINE_LINE_NUMBER, text, max, TRUE);
	else
		view->col += draw_space(view, LINE_LINE_NUMBER, max, digits3);
	return draw_graphic(view, LINE_DEFAULT, &separator, 1);
}

static bool
draw_view_line(struct view *view, unsigned int lineno)
{
	struct line *line;
	bool selected = (view->offset + lineno == view->lineno);

	assert(view_is_displayed(view));

	if (view->offset + lineno >= view->lines)
		return FALSE;

	line = &view->line[view->offset + lineno];

	wmove(view->win, lineno, 0);
	if (line->cleareol)
		wclrtoeol(view->win);
	view->col = 0;
	view->curline = line;
	view->curtype = LINE_NONE;
	line->selected = FALSE;
	line->dirty = line->cleareol = 0;

	if (selected) {
		set_view_attr(view, LINE_CURSOR);
		line->selected = TRUE;
		view->ops->select(view, line);
	}

	return view->ops->draw(view, line, lineno);
}

static void
redraw_view_dirty(struct view *view)
{
	bool dirty = FALSE;
	int lineno;

	for (lineno = 0; lineno < view->height; lineno++) {
		if (view->offset + lineno >= view->lines)
			break;
		if (!view->line[view->offset + lineno].dirty)
			continue;
		dirty = TRUE;
		if (!draw_view_line(view, lineno))
			break;
	}

	if (!dirty)
		return;
	wnoutrefresh(view->win);
}

static void
redraw_view_from(struct view *view, int lineno)
{
	assert(0 <= lineno && lineno < view->height);

	for (; lineno < view->height; lineno++) {
		if (!draw_view_line(view, lineno))
			break;
	}

	wnoutrefresh(view->win);
}

static void
redraw_view(struct view *view)
{
	werase(view->win);
	redraw_view_from(view, 0);
}


static void
update_view_title(struct view *view)
{
	char buf[SIZEOF_STR];
	char state[SIZEOF_STR];
	size_t bufpos = 0, statelen = 0;

	assert(view_is_displayed(view));

	if (view->type != VIEW_STATUS && view->lines) {
		unsigned int view_lines = view->offset + view->height;
		unsigned int lines = view->lines
				   ? MIN(view_lines, view->lines) * 100 / view->lines
				   : 0;

		string_format_from(state, &statelen, " - %s %d of %d (%d%%)",
				   view->ops->type,
				   view->lineno + 1,
				   view->lines,
				   lines);

	}

	if (view->pipe) {
		time_t secs = time(NULL) - view->start_time;

		/* Three git seconds are a long time ... */
		if (secs > 2)
			string_format_from(state, &statelen, " loading %lds", secs);
	}

	string_format_from(buf, &bufpos, "[%s]", view->name);
	if (*view->ref && bufpos < view->width) {
		size_t refsize = strlen(view->ref);
		size_t minsize = bufpos + 1 + /* abbrev= */ 7 + 1 + statelen;

		if (minsize < view->width)
			refsize = view->width - minsize + 7;
		string_format_from(buf, &bufpos, " %.*s", (int) refsize, view->ref);
	}

	if (statelen && bufpos < view->width) {
		string_format_from(buf, &bufpos, "%s", state);
	}

	if (view == display[current_view])
		wbkgdset(view->title, get_line_attr(LINE_TITLE_FOCUS));
	else
		wbkgdset(view->title, get_line_attr(LINE_TITLE_BLUR));

	mvwaddnstr(view->title, 0, 0, buf, bufpos);
	wclrtoeol(view->title);
	wnoutrefresh(view->title);
}

static int
apply_step(double step, int value)
{
	if (step >= 1)
		return (int) step;
	value *= step + 0.01;
	return value ? value : 1;
}

static void
resize_display(void)
{
	int offset, i;
	struct view *base = display[0];
	struct view *view = display[1] ? display[1] : display[0];

	/* Setup window dimensions */

	getmaxyx(stdscr, base->height, base->width);

	/* Make room for the status window. */
	base->height -= 1;

	if (view != base) {
		/* Horizontal split. */
		view->width   = base->width;
		view->height  = apply_step(opt_scale_split_view, base->height);
		view->height  = MAX(view->height, MIN_VIEW_HEIGHT);
		view->height  = MIN(view->height, base->height - MIN_VIEW_HEIGHT);
		base->height -= view->height;

		/* Make room for the title bar. */
		view->height -= 1;
	}

	/* Make room for the title bar. */
	base->height -= 1;

	offset = 0;

	foreach_displayed_view (view, i) {
		if (!view->win) {
			view->win = newwin(view->height, 0, offset, 0);
			if (!view->win)
				die("Failed to create %s view", view->name);

			scrollok(view->win, FALSE);

			view->title = newwin(1, 0, offset + view->height, 0);
			if (!view->title)
				die("Failed to create title window");

		} else {
			wresize(view->win, view->height, view->width);
			mvwin(view->win,   offset, 0);
			mvwin(view->title, offset + view->height, 0);
		}

		offset += view->height + 1;
	}
}

static void
redraw_display(bool clear)
{
	struct view *view;
	int i;

	foreach_displayed_view (view, i) {
		if (clear)
			wclear(view->win);
		redraw_view(view);
		update_view_title(view);
	}
}


/*
 * Option management
 */

static void
toggle_enum_option_do(unsigned int *opt, const char *help,
		      const struct enum_map *map, size_t size)
{
	*opt = (*opt + 1) % size;
	redraw_display(FALSE);
	report("Displaying %s %s", enum_name(map[*opt]), help);
}

#define toggle_enum_option(opt, help, map) \
	toggle_enum_option_do(opt, help, map, ARRAY_SIZE(map))

#define toggle_date() toggle_enum_option(&opt_date, "dates", date_map)
#define toggle_author() toggle_enum_option(&opt_author, "author names", author_map)

static void
toggle_view_option(bool *option, const char *help)
{
	*option = !*option;
	redraw_display(FALSE);
	report("%sabling %s", *option ? "En" : "Dis", help);
}

static void
open_option_menu(void)
{
	const struct menu_item menu[] = {
		{ '.', "line numbers", &opt_line_number },
		{ 'D', "date display", &opt_date },
		{ 'A', "author display", &opt_author },
		{ 'g', "revision graph display", &opt_rev_graph },
		{ 'F', "reference display", &opt_show_refs },
		{ 0 }
	};
	int selected = 0;

	if (prompt_menu("Toggle option", menu, &selected)) {
		if (menu[selected].data == &opt_date)
			toggle_date();
		else if (menu[selected].data == &opt_author)
			toggle_author();
		else
			toggle_view_option(menu[selected].data, menu[selected].text);
	}
}

static void
maximize_view(struct view *view)
{
	memset(display, 0, sizeof(display));
	current_view = 0;
	display[current_view] = view;
	resize_display();
	redraw_display(FALSE);
	report("");
}


/*
 * Navigation
 */

static bool
goto_view_line(struct view *view, unsigned long offset, unsigned long lineno)
{
	if (lineno >= view->lines)
		lineno = view->lines > 0 ? view->lines - 1 : 0;

	if (offset > lineno || offset + view->height <= lineno) {
		unsigned long half = view->height / 2;

		if (lineno > half)
			offset = lineno - half;
		else
			offset = 0;
	}

	if (offset != view->offset || lineno != view->lineno) {
		view->offset = offset;
		view->lineno = lineno;
		return TRUE;
	}

	return FALSE;
}

/* Scrolling backend */
static void
do_scroll_view(struct view *view, int lines)
{
	bool redraw_current_line = FALSE;

	/* The rendering expects the new offset. */
	view->offset += lines;

	assert(0 <= view->offset && view->offset < view->lines);
	assert(lines);

	/* Move current line into the view. */
	if (view->lineno < view->offset) {
		view->lineno = view->offset;
		redraw_current_line = TRUE;
	} else if (view->lineno >= view->offset + view->height) {
		view->lineno = view->offset + view->height - 1;
		redraw_current_line = TRUE;
	}

	assert(view->offset <= view->lineno && view->lineno < view->lines);

	/* Redraw the whole screen if scrolling is pointless. */
	if (view->height < ABS(lines)) {
		redraw_view(view);

	} else {
		int line = lines > 0 ? view->height - lines : 0;
		int end = line + ABS(lines);

		scrollok(view->win, TRUE);
		wscrl(view->win, lines);
		scrollok(view->win, FALSE);

		while (line < end && draw_view_line(view, line))
			line++;

		if (redraw_current_line)
			draw_view_line(view, view->lineno - view->offset);
		wnoutrefresh(view->win);
	}

	view->has_scrolled = TRUE;
	report("");
}

/* Scroll frontend */
static void
scroll_view(struct view *view, enum request request)
{
	int lines = 1;

	assert(view_is_displayed(view));

	switch (request) {
	case REQ_SCROLL_LEFT:
		if (view->yoffset == 0) {
			report("Cannot scroll beyond the first column");
			return;
		}
		if (view->yoffset <= apply_step(opt_hscroll, view->width))
			view->yoffset = 0;
		else
			view->yoffset -= apply_step(opt_hscroll, view->width);
		redraw_view_from(view, 0);
		report("");
		return;
	case REQ_SCROLL_RIGHT:
		view->yoffset += apply_step(opt_hscroll, view->width);
		redraw_view(view);
		report("");
		return;
	case REQ_SCROLL_PAGE_DOWN:
		lines = view->height;
	case REQ_SCROLL_LINE_DOWN:
		if (view->offset + lines > view->lines)
			lines = view->lines - view->offset;

		if (lines == 0 || view->offset + view->height >= view->lines) {
			report("Cannot scroll beyond the last line");
			return;
		}
		break;

	case REQ_SCROLL_PAGE_UP:
		lines = view->height;
	case REQ_SCROLL_LINE_UP:
		if (lines > view->offset)
			lines = view->offset;

		if (lines == 0) {
			report("Cannot scroll beyond the first line");
			return;
		}

		lines = -lines;
		break;

	default:
		die("request %d not handled in switch", request);
	}

	do_scroll_view(view, lines);
}

/* Cursor moving */
static void
move_view(struct view *view, enum request request)
{
	int scroll_steps = 0;
	int steps;

	switch (request) {
	case REQ_MOVE_FIRST_LINE:
		steps = -view->lineno;
		break;

	case REQ_MOVE_LAST_LINE:
		steps = view->lines - view->lineno - 1;
		break;

	case REQ_MOVE_PAGE_UP:
		steps = view->height > view->lineno
		      ? -view->lineno : -view->height;
		break;

	case REQ_MOVE_PAGE_DOWN:
		steps = view->lineno + view->height >= view->lines
		      ? view->lines - view->lineno - 1 : view->height;
		break;

	case REQ_MOVE_UP:
		steps = -1;
		break;

	case REQ_MOVE_DOWN:
		steps = 1;
		break;

	default:
		die("request %d not handled in switch", request);
	}

	if (steps <= 0 && view->lineno == 0) {
		report("Cannot move beyond the first line");
		return;

	} else if (steps >= 0 && view->lineno + 1 >= view->lines) {
		report("Cannot move beyond the last line");
		return;
	}

	/* Move the current line */
	view->lineno += steps;
	assert(0 <= view->lineno && view->lineno < view->lines);

	/* Check whether the view needs to be scrolled */
	if (view->lineno < view->offset ||
	    view->lineno >= view->offset + view->height) {
		scroll_steps = steps;
		if (steps < 0 && -steps > view->offset) {
			scroll_steps = -view->offset;

		} else if (steps > 0) {
			if (view->lineno == view->lines - 1 &&
			    view->lines > view->height) {
				scroll_steps = view->lines - view->offset - 1;
				if (scroll_steps >= view->height)
					scroll_steps -= view->height - 1;
			}
		}
	}

	if (!view_is_displayed(view)) {
		view->offset += scroll_steps;
		assert(0 <= view->offset && view->offset < view->lines);
		view->ops->select(view, &view->line[view->lineno]);
		return;
	}

	/* Repaint the old "current" line if we be scrolling */
	if (ABS(steps) < view->height)
		draw_view_line(view, view->lineno - steps - view->offset);

	if (scroll_steps) {
		do_scroll_view(view, scroll_steps);
		return;
	}

	/* Draw the current line */
	draw_view_line(view, view->lineno - view->offset);

	wnoutrefresh(view->win);
	report("");
}


/*
 * Searching
 */

static void search_view(struct view *view, enum request request);

static bool
grep_text(struct view *view, const char *text[])
{
	regmatch_t pmatch;
	size_t i;

	for (i = 0; text[i]; i++)
		if (*text[i] &&
		    regexec(view->regex, text[i], 1, &pmatch, 0) != REG_NOMATCH)
			return TRUE;
	return FALSE;
}

static void
select_view_line(struct view *view, unsigned long lineno)
{
	unsigned long old_lineno = view->lineno;
	unsigned long old_offset = view->offset;

	if (goto_view_line(view, view->offset, lineno)) {
		if (view_is_displayed(view)) {
			if (old_offset != view->offset) {
				redraw_view(view);
			} else {
				draw_view_line(view, old_lineno - view->offset);
				draw_view_line(view, view->lineno - view->offset);
				wnoutrefresh(view->win);
			}
		} else {
			view->ops->select(view, &view->line[view->lineno]);
		}
	}
}

static void
find_next(struct view *view, enum request request)
{
	unsigned long lineno = view->lineno;
	int direction;

	if (!*view->grep) {
		if (!*opt_search)
			report("No previous search");
		else
			search_view(view, request);
		return;
	}

	switch (request) {
	case REQ_SEARCH:
	case REQ_FIND_NEXT:
		direction = 1;
		break;

	case REQ_SEARCH_BACK:
	case REQ_FIND_PREV:
		direction = -1;
		break;

	default:
		return;
	}

	if (request == REQ_FIND_NEXT || request == REQ_FIND_PREV)
		lineno += direction;

	/* Note, lineno is unsigned long so will wrap around in which case it
	 * will become bigger than view->lines. */
	for (; lineno < view->lines; lineno += direction) {
		if (view->ops->grep(view, &view->line[lineno])) {
			select_view_line(view, lineno);
			report("Line %ld matches '%s'", lineno + 1, view->grep);
			return;
		}
	}

	report("No match found for '%s'", view->grep);
}

static void
search_view(struct view *view, enum request request)
{
	int regex_err;

	if (view->regex) {
		regfree(view->regex);
		*view->grep = 0;
	} else {
		view->regex = calloc(1, sizeof(*view->regex));
		if (!view->regex)
			return;
	}

	regex_err = regcomp(view->regex, opt_search, REG_EXTENDED);
	if (regex_err != 0) {
		char buf[SIZEOF_STR] = "unknown error";

		regerror(regex_err, view->regex, buf, sizeof(buf));
		report("Search failed: %s", buf);
		return;
	}

	string_copy(view->grep, opt_search);

	find_next(view, request);
}

/*
 * Incremental updating
 */

static void
reset_view(struct view *view)
{
	int i;

	for (i = 0; i < view->lines; i++)
		free(view->line[i].data);
	free(view->line);

	view->p_offset = view->offset;
	view->p_yoffset = view->yoffset;
	view->p_lineno = view->lineno;

	view->line = NULL;
	view->offset = 0;
	view->yoffset = 0;
	view->lines  = 0;
	view->lineno = 0;
	view->vid[0] = 0;
	view->update_secs = 0;
}

static const char *
format_arg(const char *name)
{
	static struct {
		const char *name;
		size_t namelen;
		const char *value;
		const char *value_if_empty;
	} vars[] = {
#define FORMAT_VAR(name, value, value_if_empty) \
	{ name, STRING_SIZE(name), value, value_if_empty }
		FORMAT_VAR("%(directory)",	opt_path,	""),
		FORMAT_VAR("%(file)",		opt_file,	""),
		FORMAT_VAR("%(ref)",		opt_ref,	"HEAD"),
		FORMAT_VAR("%(head)",		ref_head,	""),
		FORMAT_VAR("%(commit)",		ref_commit,	""),
		FORMAT_VAR("%(blob)",		ref_blob,	""),
		FORMAT_VAR("%(branch)",		ref_branch,	""),
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(vars); i++)
		if (!strncmp(name, vars[i].name, vars[i].namelen))
			return *vars[i].value ? vars[i].value : vars[i].value_if_empty;

	report("Unknown replacement: `%s`", name);
	return NULL;
}

static bool
format_argv(const char *dst_argv[], const char *src_argv[], bool replace)
{
	char buf[SIZEOF_STR];
	int argc;

	argv_free(dst_argv);

	for (argc = 0; src_argv[argc]; argc++) {
		const char *arg = src_argv[argc];
		size_t bufpos = 0;

		while (arg) {
			char *next = strstr(arg, "%(");
			int len = next - arg;
			const char *value;

			if (!next || !replace) {
				len = strlen(arg);
				value = "";

			} else {
				value = format_arg(next);

				if (!value) {
					return FALSE;
				}
			}

			if (!string_format_from(buf, &bufpos, "%.*s%s", len, arg, value))
				return FALSE;

			arg = next && replace ? strchr(next, ')') + 1 : NULL;
		}

		dst_argv[argc] = strdup(buf);
		if (!dst_argv[argc])
			break;
	}

	dst_argv[argc] = NULL;

	return src_argv[argc] == NULL;
}

static bool
restore_view_position(struct view *view)
{
	if (!view->p_restore || (view->pipe && view->lines <= view->p_lineno))
		return FALSE;

	/* Changing the view position cancels the restoring. */
	/* FIXME: Changing back to the first line is not detected. */
	if (view->offset != 0 || view->lineno != 0) {
		view->p_restore = FALSE;
		return FALSE;
	}

	if (goto_view_line(view, view->p_offset, view->p_lineno) &&
	    view_is_displayed(view))
		werase(view->win);

	view->yoffset = view->p_yoffset;
	view->p_restore = FALSE;

	return TRUE;
}

static void
end_update(struct view *view, bool force)
{
	if (!view->pipe)
		return;
	while (!view->ops->read(view, NULL))
		if (!force)
			return;
	if (force)
		io_kill(view->pipe);
	io_done(view->pipe);
	view->pipe = NULL;
}

static void
setup_update(struct view *view, const char *vid)
{
	reset_view(view);
	string_copy_rev(view->vid, vid);
	view->pipe = &view->io;
	view->start_time = time(NULL);
}

static bool
prepare_io(struct view *view, const char *dir, const char *argv[], bool replace)
{
	io_init(&view->io);
	view->dir = dir;
	return format_argv(view->argv, argv, replace);
}

static bool
prepare_update(struct view *view, const char *argv[], const char *dir)
{
	if (view->pipe)
		end_update(view, TRUE);
	return prepare_io(view, dir, argv, FALSE);
}

static bool
start_update(struct view *view, const char **argv, const char *dir)
{
	if (view->pipe)
		io_done(view->pipe);
	return prepare_io(view, dir, argv, FALSE) &&
	       io_start(&view->io, IO_RD, dir, view->argv);
}

static bool
prepare_update_file(struct view *view, const char *name)
{
	if (view->pipe)
		end_update(view, TRUE);
	argv_free(view->argv);
	return io_open(&view->io, "%s/%s", opt_cdup[0] ? opt_cdup : ".", name);
}

static bool
begin_update(struct view *view, bool refresh)
{
	if (view->pipe)
		end_update(view, TRUE);

	if (!refresh) {
		if (view->ops->prepare) {
			if (!view->ops->prepare(view))
				return FALSE;
		} else if (!prepare_io(view, NULL, view->ops->argv, TRUE)) {
			return FALSE;
		}

		/* Put the current ref_* value to the view title ref
		 * member. This is needed by the blob view. Most other
		 * views sets it automatically after loading because the
		 * first line is a commit line. */
		string_copy_rev(view->ref, view->id);
	}

	if (view->argv[0] && !io_start(&view->io, IO_RD, view->dir, view->argv))
		return FALSE;

	setup_update(view, view->id);

	return TRUE;
}

static bool
update_view(struct view *view)
{
	char out_buffer[BUFSIZ * 2];
	char *line;
	/* Clear the view and redraw everything since the tree sorting
	 * might have rearranged things. */
	bool redraw = view->lines == 0;
	bool can_read = TRUE;

	if (!view->pipe)
		return TRUE;

	if (!io_can_read(view->pipe)) {
		if (view->lines == 0 && view_is_displayed(view)) {
			time_t secs = time(NULL) - view->start_time;

			if (secs > 1 && secs > view->update_secs) {
				if (view->update_secs == 0)
					redraw_view(view);
				update_view_title(view);
				view->update_secs = secs;
			}
		}
		return TRUE;
	}

	for (; (line = io_get(view->pipe, '\n', can_read)); can_read = FALSE) {
		if (opt_iconv_in != ICONV_NONE) {
			ICONV_CONST char *inbuf = line;
			size_t inlen = strlen(line) + 1;

			char *outbuf = out_buffer;
			size_t outlen = sizeof(out_buffer);

			size_t ret;

			ret = iconv(opt_iconv_in, &inbuf, &inlen, &outbuf, &outlen);
			if (ret != (size_t) -1)
				line = out_buffer;
		}

		if (!view->ops->read(view, line)) {
			report("Allocation failure");
			end_update(view, TRUE);
			return FALSE;
		}
	}

	{
		unsigned long lines = view->lines;
		int digits;

		for (digits = 0; lines; digits++)
			lines /= 10;

		/* Keep the displayed view in sync with line number scaling. */
		if (digits != view->digits) {
			view->digits = digits;
			if (opt_line_number || view->type == VIEW_BLAME)
				redraw = TRUE;
		}
	}

	if (io_error(view->pipe)) {
		report("Failed to read: %s", io_strerror(view->pipe));
		end_update(view, TRUE);

	} else if (io_eof(view->pipe)) {
		if (view_is_displayed(view))
			report("");
		end_update(view, FALSE);
	}

	if (restore_view_position(view))
		redraw = TRUE;

	if (!view_is_displayed(view))
		return TRUE;

	if (redraw)
		redraw_view_from(view, 0);
	else
		redraw_view_dirty(view);

	/* Update the title _after_ the redraw so that if the redraw picks up a
	 * commit reference in view->ref it'll be available here. */
	update_view_title(view);
	return TRUE;
}

DEFINE_ALLOCATOR(realloc_lines, struct line, 256)

static struct line *
add_line_data(struct view *view, void *data, enum line_type type)
{
	struct line *line;

	if (!realloc_lines(&view->line, view->lines, 1))
		return NULL;

	line = &view->line[view->lines++];
	memset(line, 0, sizeof(*line));
	line->type = type;
	line->data = data;
	line->dirty = 1;

	return line;
}

static struct line *
add_line_text(struct view *view, const char *text, enum line_type type)
{
	char *data = text ? strdup(text) : NULL;

	return data ? add_line_data(view, data, type) : NULL;
}

static struct line *
add_line_format(struct view *view, enum line_type type, const char *fmt, ...)
{
	char buf[SIZEOF_STR];
	va_list args;

	va_start(args, fmt);
	if (vsnprintf(buf, sizeof(buf), fmt, args) >= sizeof(buf))
		buf[0] = 0;
	va_end(args);

	return buf[0] ? add_line_text(view, buf, type) : NULL;
}

/*
 * View opening
 */

enum open_flags {
	OPEN_DEFAULT = 0,	/* Use default view switching. */
	OPEN_SPLIT = 1,		/* Split current view. */
	OPEN_RELOAD = 4,	/* Reload view even if it is the current. */
	OPEN_REFRESH = 16,	/* Refresh view using previous command. */
	OPEN_PREPARED = 32,	/* Open already prepared command. */
};

static void
open_view(struct view *prev, enum request request, enum open_flags flags)
{
	bool split = !!(flags & OPEN_SPLIT);
	bool reload = !!(flags & (OPEN_RELOAD | OPEN_REFRESH | OPEN_PREPARED));
	bool nomaximize = !!(flags & OPEN_REFRESH);
	struct view *view = VIEW(request);
	int nviews = displayed_views();
	struct view *base_view = display[0];

	if (view == prev && nviews == 1 && !reload) {
		report("Already in %s view", view->name);
		return;
	}

	if (view->git_dir && !opt_git_dir[0]) {
		report("The %s view is disabled in pager view", view->name);
		return;
	}

	if (split) {
		display[1] = view;
		current_view = 1;
		view->parent = prev;
	} else if (!nomaximize) {
		/* Maximize the current view. */
		memset(display, 0, sizeof(display));
		current_view = 0;
		display[current_view] = view;
	}

	/* No prev signals that this is the first loaded view. */
	if (prev && view != prev) {
		view->prev = prev;
	}

	/* Resize the view when switching between split- and full-screen,
	 * or when switching between two different full-screen views. */
	if (nviews != displayed_views() ||
	    (nviews == 1 && base_view != display[0]))
		resize_display();

	if (view->ops->open) {
		if (view->pipe)
			end_update(view, TRUE);
		if (!view->ops->open(view)) {
			report("Failed to load %s view", view->name);
			return;
		}
		restore_view_position(view);

	} else if ((reload || strcmp(view->vid, view->id)) &&
		   !begin_update(view, flags & (OPEN_REFRESH | OPEN_PREPARED))) {
		report("Failed to load %s view", view->name);
		return;
	}

	if (split && prev->lineno - prev->offset >= prev->height) {
		/* Take the title line into account. */
		int lines = prev->lineno - prev->offset - prev->height + 1;

		/* Scroll the view that was split if the current line is
		 * outside the new limited view. */
		do_scroll_view(prev, lines);
	}

	if (prev && view != prev && split && view_is_displayed(prev)) {
		/* "Blur" the previous view. */
		update_view_title(prev);
	}

	if (view->pipe && view->lines == 0) {
		/* Clear the old view and let the incremental updating refill
		 * the screen. */
		werase(view->win);
		view->p_restore = flags & (OPEN_RELOAD | OPEN_REFRESH);
		report("");
	} else if (view_is_displayed(view)) {
		redraw_view(view);
		report("");
	}
}

static void
open_external_viewer(const char *argv[], const char *dir)
{
	def_prog_mode();           /* save current tty modes */
	endwin();                  /* restore original tty modes */
	io_run_fg(argv, dir);
	fprintf(stderr, "Press Enter to continue");
	getc(opt_tty);
	reset_prog_mode();
	redraw_display(TRUE);
}

static void
open_mergetool(const char *file)
{
	const char *mergetool_argv[] = { "git", "mergetool", file, NULL };

	open_external_viewer(mergetool_argv, opt_cdup);
}

static void
open_editor(const char *file)
{
	const char *editor_argv[] = { "vi", file, NULL };
	const char *editor;

	editor = getenv("GIT_EDITOR");
	if (!editor && *opt_editor)
		editor = opt_editor;
	if (!editor)
		editor = getenv("VISUAL");
	if (!editor)
		editor = getenv("EDITOR");
	if (!editor)
		editor = "vi";

	editor_argv[0] = editor;
	open_external_viewer(editor_argv, opt_cdup);
}

static void
open_run_request(enum request request)
{
	struct run_request *req = get_run_request(request);
	const char *argv[ARRAY_SIZE(req->argv)] = { NULL };

	if (!req) {
		report("Unknown run request");
		return;
	}

	if (format_argv(argv, req->argv, TRUE))
		open_external_viewer(argv, NULL);
	argv_free(argv);
}

/*
 * User request switch noodle
 */

static int
view_driver(struct view *view, enum request request)
{
	int i;

	if (request == REQ_NONE)
		return TRUE;

	if (request > REQ_NONE) {
		open_run_request(request);
		view_request(view, REQ_REFRESH);
		return TRUE;
	}

	request = view_request(view, request);
	if (request == REQ_NONE)
		return TRUE;

	switch (request) {
	case REQ_MOVE_UP:
	case REQ_MOVE_DOWN:
	case REQ_MOVE_PAGE_UP:
	case REQ_MOVE_PAGE_DOWN:
	case REQ_MOVE_FIRST_LINE:
	case REQ_MOVE_LAST_LINE:
		move_view(view, request);
		break;

	case REQ_SCROLL_LEFT:
	case REQ_SCROLL_RIGHT:
	case REQ_SCROLL_LINE_DOWN:
	case REQ_SCROLL_LINE_UP:
	case REQ_SCROLL_PAGE_DOWN:
	case REQ_SCROLL_PAGE_UP:
		scroll_view(view, request);
		break;

	case REQ_VIEW_BLAME:
		if (!opt_file[0]) {
			report("No file chosen, press %s to open tree view",
			       get_key(view->keymap, REQ_VIEW_TREE));
			break;
		}
		open_view(view, request, OPEN_DEFAULT);
		break;

	case REQ_VIEW_BLOB:
		if (!ref_blob[0]) {
			report("No file chosen, press %s to open tree view",
			       get_key(view->keymap, REQ_VIEW_TREE));
			break;
		}
		open_view(view, request, OPEN_DEFAULT);
		break;

	case REQ_VIEW_PAGER:
		if (!VIEW(REQ_VIEW_PAGER)->pipe && !VIEW(REQ_VIEW_PAGER)->lines) {
			report("No pager content, press %s to run command from prompt",
			       get_key(view->keymap, REQ_PROMPT));
			break;
		}
		open_view(view, request, OPEN_DEFAULT);
		break;

	case REQ_VIEW_STAGE:
		if (!VIEW(REQ_VIEW_STAGE)->lines) {
			report("No stage content, press %s to open the status view and choose file",
			       get_key(view->keymap, REQ_VIEW_STATUS));
			break;
		}
		open_view(view, request, OPEN_DEFAULT);
		break;

	case REQ_VIEW_STATUS:
		if (opt_is_inside_work_tree == FALSE) {
			report("The status view requires a working tree");
			break;
		}
		open_view(view, request, OPEN_DEFAULT);
		break;

	case REQ_VIEW_MAIN:
	case REQ_VIEW_DIFF:
	case REQ_VIEW_LOG:
	case REQ_VIEW_TREE:
	case REQ_VIEW_HELP:
	case REQ_VIEW_BRANCH:
		open_view(view, request, OPEN_DEFAULT);
		break;

	case REQ_NEXT:
	case REQ_PREVIOUS:
		request = request == REQ_NEXT ? REQ_MOVE_DOWN : REQ_MOVE_UP;

		if (view->parent) {
			int line;

			view = view->parent;
			line = view->lineno;
			move_view(view, request);
			if (view_is_displayed(view))
				update_view_title(view);
			if (line != view->lineno)
				view_request(view, REQ_ENTER);
		} else {
			move_view(view, request);
		}
		break;

	case REQ_VIEW_NEXT:
	{
		int nviews = displayed_views();
		int next_view = (current_view + 1) % nviews;

		if (next_view == current_view) {
			report("Only one view is displayed");
			break;
		}

		current_view = next_view;
		/* Blur out the title of the previous view. */
		update_view_title(view);
		report("");
		break;
	}
	case REQ_REFRESH:
		report("Refreshing is not yet supported for the %s view", view->name);
		break;

	case REQ_MAXIMIZE:
		if (displayed_views() == 2)
			maximize_view(view);
		break;

	case REQ_OPTIONS:
		open_option_menu();
		break;

	case REQ_TOGGLE_LINENO:
		toggle_view_option(&opt_line_number, "line numbers");
		break;

	case REQ_TOGGLE_DATE:
		toggle_date();
		break;

	case REQ_TOGGLE_AUTHOR:
		toggle_author();
		break;

	case REQ_TOGGLE_REV_GRAPH:
		toggle_view_option(&opt_rev_graph, "revision graph display");
		break;

	case REQ_TOGGLE_REFS:
		toggle_view_option(&opt_show_refs, "reference display");
		break;

	case REQ_TOGGLE_SORT_FIELD:
	case REQ_TOGGLE_SORT_ORDER:
		report("Sorting is not yet supported for the %s view", view->name);
		break;

	case REQ_SEARCH:
	case REQ_SEARCH_BACK:
		search_view(view, request);
		break;

	case REQ_FIND_NEXT:
	case REQ_FIND_PREV:
		find_next(view, request);
		break;

	case REQ_STOP_LOADING:
		foreach_view(view, i) {
			if (view->pipe)
				report("Stopped loading the %s view", view->name),
			end_update(view, TRUE);
		}
		break;

	case REQ_SHOW_VERSION:
		report("tig-%s (built %s)", TIG_VERSION, __DATE__);
		return TRUE;

	case REQ_SCREEN_REDRAW:
		redraw_display(TRUE);
		break;

	case REQ_EDIT:
		report("Nothing to edit");
		break;

	case REQ_ENTER:
		report("Nothing to enter");
		break;

	case REQ_VIEW_CLOSE:
		/* XXX: Mark closed views by letting view->prev point to the
		 * view itself. Parents to closed view should never be
		 * followed. */
		if (view->prev && view->prev != view) {
			maximize_view(view->prev);
			view->prev = view;
			break;
		}
		/* Fall-through */
	case REQ_QUIT:
		return FALSE;

	default:
		report("Unknown key, press %s for help",
		       get_key(view->keymap, REQ_VIEW_HELP));
		return TRUE;
	}

	return TRUE;
}


/*
 * View backend utilities
 */

enum sort_field {
	ORDERBY_NAME,
	ORDERBY_DATE,
	ORDERBY_AUTHOR,
};

struct sort_state {
	const enum sort_field *fields;
	size_t size, current;
	bool reverse;
};

#define SORT_STATE(fields) { fields, ARRAY_SIZE(fields), 0 }
#define get_sort_field(state) ((state).fields[(state).current])
#define sort_order(state, result) ((state).reverse ? -(result) : (result))

static void
sort_view(struct view *view, enum request request, struct sort_state *state,
	  int (*compare)(const void *, const void *))
{
	switch (request) {
	case REQ_TOGGLE_SORT_FIELD:
		state->current = (state->current + 1) % state->size;
		break;

	case REQ_TOGGLE_SORT_ORDER:
		state->reverse = !state->reverse;
		break;
	default:
		die("Not a sort request");
	}

	qsort(view->line, view->lines, sizeof(*view->line), compare);
	redraw_view(view);
}

DEFINE_ALLOCATOR(realloc_authors, const char *, 256)

/* Small author cache to reduce memory consumption. It uses binary
 * search to lookup or find place to position new entries. No entries
 * are ever freed. */
static const char *
get_author(const char *name)
{
	static const char **authors;
	static size_t authors_size;
	int from = 0, to = authors_size - 1;

	while (from <= to) {
		size_t pos = (to + from) / 2;
		int cmp = strcmp(name, authors[pos]);

		if (!cmp)
			return authors[pos];

		if (cmp < 0)
			to = pos - 1;
		else
			from = pos + 1;
	}

	if (!realloc_authors(&authors, authors_size, 1))
		return NULL;
	name = strdup(name);
	if (!name)
		return NULL;

	memmove(authors + from + 1, authors + from, (authors_size - from) * sizeof(*authors));
	authors[from] = name;
	authors_size++;

	return name;
}

static void
parse_timesec(struct time *time, const char *sec)
{
	time->sec = (time_t) atol(sec);
}

static void
parse_timezone(struct time *time, const char *zone)
{
	long tz;

	tz  = ('0' - zone[1]) * 60 * 60 * 10;
	tz += ('0' - zone[2]) * 60 * 60;
	tz += ('0' - zone[3]) * 60 * 10;
	tz += ('0' - zone[4]) * 60;

	if (zone[0] == '-')
		tz = -tz;

	time->tz = tz;
	time->sec -= tz;
}

/* Parse author lines where the name may be empty:
 *	author  <email@address.tld> 1138474660 +0100
 */
static void
parse_author_line(char *ident, const char **author, struct time *time)
{
	char *nameend = strchr(ident, '<');
	char *emailend = strchr(ident, '>');

	if (nameend && emailend)
		*nameend = *emailend = 0;
	ident = chomp_string(ident);
	if (!*ident) {
		if (nameend)
			ident = chomp_string(nameend + 1);
		if (!*ident)
			ident = "Unknown";
	}

	*author = get_author(ident);

	/* Parse epoch and timezone */
	if (emailend && emailend[1] == ' ') {
		char *secs = emailend + 2;
		char *zone = strchr(secs, ' ');

		parse_timesec(time, secs);

		if (zone && strlen(zone) == STRING_SIZE(" +0700"))
			parse_timezone(time, zone + 1);
	}
}

static bool
open_commit_parent_menu(char buf[SIZEOF_STR], int *parents)
{
	char rev[SIZEOF_REV];
	const char *revlist_argv[] = {
		"git", "log", "--no-color", "-1", "--pretty=format:%s", rev, NULL
	};
	struct menu_item *items;
	char text[SIZEOF_STR];
	bool ok = TRUE;
	int i;

	items = calloc(*parents + 1, sizeof(*items));
	if (!items)
		return FALSE;

	for (i = 0; i < *parents; i++) {
		string_copy_rev(rev, &buf[SIZEOF_REV * i]);
		if (!io_run_buf(revlist_argv, text, sizeof(text)) ||
		    !(items[i].text = strdup(text))) {
			ok = FALSE;
			break;
		}
	}

	if (ok) {
		*parents = 0;
		ok = prompt_menu("Select parent", items, parents);
	}
	for (i = 0; items[i].text; i++)
		free((char *) items[i].text);
	free(items);
	return ok;
}

static bool
select_commit_parent(const char *id, char rev[SIZEOF_REV], const char *path)
{
	char buf[SIZEOF_STR * 4];
	const char *revlist_argv[] = {
		"git", "log", "--no-color", "-1",
			"--pretty=format:%P", id, "--", path, NULL
	};
	int parents;

	if (!io_run_buf(revlist_argv, buf, sizeof(buf)) ||
	    (parents = strlen(buf) / 40) < 0) {
		report("Failed to get parent information");
		return FALSE;

	} else if (parents == 0) {
		if (path)
			report("Path '%s' does not exist in the parent", path);
		else
			report("The selected commit has no parents");
		return FALSE;
	}

	if (parents == 1)
		parents = 0;
	else if (!open_commit_parent_menu(buf, &parents))
		return FALSE;

	string_copy_rev(rev, &buf[41 * parents]);
	return TRUE;
}

/*
 * Pager backend
 */

static bool
pager_draw(struct view *view, struct line *line, unsigned int lineno)
{
	char text[SIZEOF_STR];

	if (opt_line_number && draw_lineno(view, lineno))
		return TRUE;

	string_expand(text, sizeof(text), line->data, opt_tab_size);
	draw_text(view, line->type, text, TRUE);
	return TRUE;
}

static bool
add_describe_ref(char *buf, size_t *bufpos, const char *commit_id, const char *sep)
{
	const char *describe_argv[] = { "git", "describe", commit_id, NULL };
	char ref[SIZEOF_STR];

	if (!io_run_buf(describe_argv, ref, sizeof(ref)) || !*ref)
		return TRUE;

	/* This is the only fatal call, since it can "corrupt" the buffer. */
	if (!string_nformat(buf, SIZEOF_STR, bufpos, "%s%s", sep, ref))
		return FALSE;

	return TRUE;
}

static void
add_pager_refs(struct view *view, struct line *line)
{
	char buf[SIZEOF_STR];
	char *commit_id = (char *)line->data + STRING_SIZE("commit ");
	struct ref_list *list;
	size_t bufpos = 0, i;
	const char *sep = "Refs: ";
	bool is_tag = FALSE;

	assert(line->type == LINE_COMMIT);

	list = get_ref_list(commit_id);
	if (!list) {
		if (view->type == VIEW_DIFF)
			goto try_add_describe_ref;
		return;
	}

	for (i = 0; i < list->size; i++) {
		struct ref *ref = list->refs[i];
		const char *fmt = ref->tag    ? "%s[%s]" :
		                  ref->remote ? "%s<%s>" : "%s%s";

		if (!string_format_from(buf, &bufpos, fmt, sep, ref->name))
			return;
		sep = ", ";
		if (ref->tag)
			is_tag = TRUE;
	}

	if (!is_tag && view->type == VIEW_DIFF) {
try_add_describe_ref:
		/* Add <tag>-g<commit_id> "fake" reference. */
		if (!add_describe_ref(buf, &bufpos, commit_id, sep))
			return;
	}

	if (bufpos == 0)
		return;

	add_line_text(view, buf, LINE_PP_REFS);
}

static bool
pager_read(struct view *view, char *data)
{
	struct line *line;

	if (!data)
		return TRUE;

	line = add_line_text(view, data, get_line_type(data));
	if (!line)
		return FALSE;

	if (line->type == LINE_COMMIT &&
	    (view->type == VIEW_DIFF ||
	     view->type == VIEW_LOG))
		add_pager_refs(view, line);

	return TRUE;
}

static enum request
pager_request(struct view *view, enum request request, struct line *line)
{
	int split = 0;

	if (request != REQ_ENTER)
		return request;

	if (line->type == LINE_COMMIT &&
	   (view->type == VIEW_LOG ||
	    view->type == VIEW_PAGER)) {
		open_view(view, REQ_VIEW_DIFF, OPEN_SPLIT);
		split = 1;
	}

	/* Always scroll the view even if it was split. That way
	 * you can use Enter to scroll through the log view and
	 * split open each commit diff. */
	scroll_view(view, REQ_SCROLL_LINE_DOWN);

	/* FIXME: A minor workaround. Scrolling the view will call report("")
	 * but if we are scrolling a non-current view this won't properly
	 * update the view title. */
	if (split)
		update_view_title(view);

	return REQ_NONE;
}

static bool
pager_grep(struct view *view, struct line *line)
{
	const char *text[] = { line->data, NULL };

	return grep_text(view, text);
}

static void
pager_select(struct view *view, struct line *line)
{
	if (line->type == LINE_COMMIT) {
		char *text = (char *)line->data + STRING_SIZE("commit ");

		if (view->type != VIEW_PAGER)
			string_copy_rev(view->ref, text);
		string_copy_rev(ref_commit, text);
	}
}

static struct view_ops pager_ops = {
	"line",
	NULL,
	NULL,
	pager_read,
	pager_draw,
	pager_request,
	pager_grep,
	pager_select,
};

static const char *log_argv[SIZEOF_ARG] = {
	"git", "log", "--no-color", "--cc", "--stat", "-n100", "%(head)", NULL
};

static enum request
log_request(struct view *view, enum request request, struct line *line)
{
	switch (request) {
	case REQ_REFRESH:
		load_refs();
		open_view(view, REQ_VIEW_LOG, OPEN_REFRESH);
		return REQ_NONE;
	default:
		return pager_request(view, request, line);
	}
}

static struct view_ops log_ops = {
	"line",
	log_argv,
	NULL,
	pager_read,
	pager_draw,
	log_request,
	pager_grep,
	pager_select,
};

static const char *diff_argv[SIZEOF_ARG] = {
	"git", "show", "--pretty=fuller", "--no-color", "--root",
		"--patch-with-stat", "--find-copies-harder", "-C", "%(commit)", NULL
};

static struct view_ops diff_ops = {
	"line",
	diff_argv,
	NULL,
	pager_read,
	pager_draw,
	pager_request,
	pager_grep,
	pager_select,
};

/*
 * Help backend
 */

static bool help_keymap_hidden[ARRAY_SIZE(keymap_table)];

static bool
help_open_keymap_title(struct view *view, enum keymap keymap)
{
	struct line *line;

	line = add_line_format(view, LINE_HELP_KEYMAP, "[%c] %s bindings",
			       help_keymap_hidden[keymap] ? '+' : '-',
			       enum_name(keymap_table[keymap]));
	if (line)
		line->other = keymap;

	return help_keymap_hidden[keymap];
}

static void
help_open_keymap(struct view *view, enum keymap keymap)
{
	const char *group = NULL;
	char buf[SIZEOF_STR];
	size_t bufpos;
	bool add_title = TRUE;
	int i;

	for (i = 0; i < ARRAY_SIZE(req_info); i++) {
		const char *key = NULL;

		if (req_info[i].request == REQ_NONE)
			continue;

		if (!req_info[i].request) {
			group = req_info[i].help;
			continue;
		}

		key = get_keys(keymap, req_info[i].request, TRUE);
		if (!key || !*key)
			continue;

		if (add_title && help_open_keymap_title(view, keymap))
			return;
		add_title = FALSE;

		if (group) {
			add_line_text(view, group, LINE_HELP_GROUP);
			group = NULL;
		}

		add_line_format(view, LINE_DEFAULT, "    %-25s %-20s %s", key,
				enum_name(req_info[i]), req_info[i].help);
	}

	group = "External commands:";

	for (i = 0; i < run_requests; i++) {
		struct run_request *req = get_run_request(REQ_NONE + i + 1);
		const char *key;
		int argc;

		if (!req || req->keymap != keymap)
			continue;

		key = get_key_name(req->key);
		if (!*key)
			key = "(no key defined)";

		if (add_title && help_open_keymap_title(view, keymap))
			return;
		if (group) {
			add_line_text(view, group, LINE_HELP_GROUP);
			group = NULL;
		}

		for (bufpos = 0, argc = 0; req->argv[argc]; argc++)
			if (!string_format_from(buf, &bufpos, "%s%s",
					        argc ? " " : "", req->argv[argc]))
				return;

		add_line_format(view, LINE_DEFAULT, "    %-25s `%s`", key, buf);
	}
}

static bool
help_open(struct view *view)
{
	enum keymap keymap;

	reset_view(view);
	add_line_text(view, "Quick reference for tig keybindings:", LINE_DEFAULT);
	add_line_text(view, "", LINE_DEFAULT);

	for (keymap = 0; keymap < ARRAY_SIZE(keymap_table); keymap++)
		help_open_keymap(view, keymap);

	return TRUE;
}

static enum request
help_request(struct view *view, enum request request, struct line *line)
{
	switch (request) {
	case REQ_ENTER:
		if (line->type == LINE_HELP_KEYMAP) {
			help_keymap_hidden[line->other] =
				!help_keymap_hidden[line->other];
			view->p_restore = TRUE;
			open_view(view, REQ_VIEW_HELP, OPEN_REFRESH);
		}

		return REQ_NONE;
	default:
		return pager_request(view, request, line);
	}
}

static struct view_ops help_ops = {
	"line",
	NULL,
	help_open,
	NULL,
	pager_draw,
	help_request,
	pager_grep,
	pager_select,
};


/*
 * Tree backend
 */

struct tree_stack_entry {
	struct tree_stack_entry *prev;	/* Entry below this in the stack */
	unsigned long lineno;		/* Line number to restore */
	char *name;			/* Position of name in opt_path */
};

/* The top of the path stack. */
static struct tree_stack_entry *tree_stack = NULL;
unsigned long tree_lineno = 0;

static void
pop_tree_stack_entry(void)
{
	struct tree_stack_entry *entry = tree_stack;

	tree_lineno = entry->lineno;
	entry->name[0] = 0;
	tree_stack = entry->prev;
	free(entry);
}

static void
push_tree_stack_entry(const char *name, unsigned long lineno)
{
	struct tree_stack_entry *entry = calloc(1, sizeof(*entry));
	size_t pathlen = strlen(opt_path);

	if (!entry)
		return;

	entry->prev = tree_stack;
	entry->name = opt_path + pathlen;
	tree_stack = entry;

	if (!string_format_from(opt_path, &pathlen, "%s/", name)) {
		pop_tree_stack_entry();
		return;
	}

	/* Move the current line to the first tree entry. */
	tree_lineno = 1;
	entry->lineno = lineno;
}

/* Parse output from git-ls-tree(1):
 *
 * 100644 blob f931e1d229c3e185caad4449bf5b66ed72462657	tig.c
 */

#define SIZEOF_TREE_ATTR \
	STRING_SIZE("100644 blob f931e1d229c3e185caad4449bf5b66ed72462657\t")

#define SIZEOF_TREE_MODE \
	STRING_SIZE("100644 ")

#define TREE_ID_OFFSET \
	STRING_SIZE("100644 blob ")

struct tree_entry {
	char id[SIZEOF_REV];
	mode_t mode;
	struct time time;		/* Date from the author ident. */
	const char *author;		/* Author of the commit. */
	char name[1];
};

static const char *
tree_path(const struct line *line)
{
	return ((struct tree_entry *) line->data)->name;
}

static int
tree_compare_entry(const struct line *line1, const struct line *line2)
{
	if (line1->type != line2->type)
		return line1->type == LINE_TREE_DIR ? -1 : 1;
	return strcmp(tree_path(line1), tree_path(line2));
}

static const enum sort_field tree_sort_fields[] = {
	ORDERBY_NAME, ORDERBY_DATE, ORDERBY_AUTHOR
};
static struct sort_state tree_sort_state = SORT_STATE(tree_sort_fields);

static int
tree_compare(const void *l1, const void *l2)
{
	const struct line *line1 = (const struct line *) l1;
	const struct line *line2 = (const struct line *) l2;
	const struct tree_entry *entry1 = ((const struct line *) l1)->data;
	const struct tree_entry *entry2 = ((const struct line *) l2)->data;

	if (line1->type == LINE_TREE_HEAD)
		return -1;
	if (line2->type == LINE_TREE_HEAD)
		return 1;

	switch (get_sort_field(tree_sort_state)) {
	case ORDERBY_DATE:
		return sort_order(tree_sort_state, timecmp(&entry1->time, &entry2->time));

	case ORDERBY_AUTHOR:
		return sort_order(tree_sort_state, strcmp(entry1->author, entry2->author));

	case ORDERBY_NAME:
	default:
		return sort_order(tree_sort_state, tree_compare_entry(line1, line2));
	}
}


static struct line *
tree_entry(struct view *view, enum line_type type, const char *path,
	   const char *mode, const char *id)
{
	struct tree_entry *entry = calloc(1, sizeof(*entry) + strlen(path));
	struct line *line = entry ? add_line_data(view, entry, type) : NULL;

	if (!entry || !line) {
		free(entry);
		return NULL;
	}

	strncpy(entry->name, path, strlen(path));
	if (mode)
		entry->mode = strtoul(mode, NULL, 8);
	if (id)
		string_copy_rev(entry->id, id);

	return line;
}

static bool
tree_read_date(struct view *view, char *text, bool *read_date)
{
	static const char *author_name;
	static struct time author_time;

	if (!text && *read_date) {
		*read_date = FALSE;
		return TRUE;

	} else if (!text) {
		char *path = *opt_path ? opt_path : ".";
		/* Find next entry to process */
		const char *log_file[] = {
			"git", "log", "--no-color", "--pretty=raw",
				"--cc", "--raw", view->id, "--", path, NULL
		};

		if (!view->lines) {
			tree_entry(view, LINE_TREE_HEAD, opt_path, NULL, NULL);
			report("Tree is empty");
			return TRUE;
		}

		if (!start_update(view, log_file, opt_cdup)) {
			report("Failed to load tree data");
			return TRUE;
		}

		*read_date = TRUE;
		return FALSE;

	} else if (*text == 'a' && get_line_type(text) == LINE_AUTHOR) {
		parse_author_line(text + STRING_SIZE("author "),
				  &author_name, &author_time);

	} else if (*text == ':') {
		char *pos;
		size_t annotated = 1;
		size_t i;

		pos = strchr(text, '\t');
		if (!pos)
			return TRUE;
		text = pos + 1;
		if (*opt_path && !strncmp(text, opt_path, strlen(opt_path)))
			text += strlen(opt_path);
		pos = strchr(text, '/');
		if (pos)
			*pos = 0;

		for (i = 1; i < view->lines; i++) {
			struct line *line = &view->line[i];
			struct tree_entry *entry = line->data;

			annotated += !!entry->author;
			if (entry->author || strcmp(entry->name, text))
				continue;

			entry->author = author_name;
			entry->time = author_time;
			line->dirty = 1;
			break;
		}

		if (annotated == view->lines)
			io_kill(view->pipe);
	}
	return TRUE;
}

static bool
tree_read(struct view *view, char *text)
{
	static bool read_date = FALSE;
	struct tree_entry *data;
	struct line *entry, *line;
	enum line_type type;
	size_t textlen = text ? strlen(text) : 0;
	char *path = text + SIZEOF_TREE_ATTR;

	if (read_date || !text)
		return tree_read_date(view, text, &read_date);

	if (textlen <= SIZEOF_TREE_ATTR)
		return FALSE;
	if (view->lines == 0 &&
	    !tree_entry(view, LINE_TREE_HEAD, opt_path, NULL, NULL))
		return FALSE;

	/* Strip the path part ... */
	if (*opt_path) {
		size_t pathlen = textlen - SIZEOF_TREE_ATTR;
		size_t striplen = strlen(opt_path);

		if (pathlen > striplen)
			memmove(path, path + striplen,
				pathlen - striplen + 1);

		/* Insert "link" to parent directory. */
		if (view->lines == 1 &&
		    !tree_entry(view, LINE_TREE_DIR, "..", "040000", view->ref))
			return FALSE;
	}

	type = text[SIZEOF_TREE_MODE] == 't' ? LINE_TREE_DIR : LINE_TREE_FILE;
	entry = tree_entry(view, type, path, text, text + TREE_ID_OFFSET);
	if (!entry)
		return FALSE;
	data = entry->data;

	/* Skip "Directory ..." and ".." line. */
	for (line = &view->line[1 + !!*opt_path]; line < entry; line++) {
		if (tree_compare_entry(line, entry) <= 0)
			continue;

		memmove(line + 1, line, (entry - line) * sizeof(*entry));

		line->data = data;
		line->type = type;
		for (; line <= entry; line++)
			line->dirty = line->cleareol = 1;
		return TRUE;
	}

	if (tree_lineno > view->lineno) {
		view->lineno = tree_lineno;
		tree_lineno = 0;
	}

	return TRUE;
}

static bool
tree_draw(struct view *view, struct line *line, unsigned int lineno)
{
	struct tree_entry *entry = line->data;

	if (line->type == LINE_TREE_HEAD) {
		if (draw_text(view, line->type, "Directory path /", TRUE))
			return TRUE;
	} else {
		if (draw_mode(view, entry->mode))
			return TRUE;

		if (opt_author && draw_author(view, entry->author))
			return TRUE;

		if (opt_date && draw_date(view, &entry->time))
			return TRUE;
	}
	if (draw_text(view, line->type, entry->name, TRUE))
		return TRUE;
	return TRUE;
}

static void
open_blob_editor(const char *id)
{
	const char *blob_argv[] = { "git", "cat-file", "blob", id, NULL };
	char file[SIZEOF_STR] = "/tmp/tigblob.XXXXXX";
	int fd = mkstemp(file);

	if (fd == -1)
		report("Failed to create temporary file");
	else if (!io_run_append(blob_argv, fd))
		report("Failed to save blob data to file");
	else
		open_editor(file);
	if (fd != -1)
		unlink(file);
}

static enum request
tree_request(struct view *view, enum request request, struct line *line)
{
	enum open_flags flags;
	struct tree_entry *entry = line->data;

	switch (request) {
	case REQ_VIEW_BLAME:
		if (line->type != LINE_TREE_FILE) {
			report("Blame only supported for files");
			return REQ_NONE;
		}

		string_copy(opt_ref, view->vid);
		return request;

	case REQ_EDIT:
		if (line->type != LINE_TREE_FILE) {
			report("Edit only supported for files");
		} else if (!is_head_commit(view->vid)) {
			open_blob_editor(entry->id);
		} else {
			open_editor(opt_file);
		}
		return REQ_NONE;

	case REQ_TOGGLE_SORT_FIELD:
	case REQ_TOGGLE_SORT_ORDER:
		sort_view(view, request, &tree_sort_state, tree_compare);
		return REQ_NONE;

	case REQ_PARENT:
		if (!*opt_path) {
			/* quit view if at top of tree */
			return REQ_VIEW_CLOSE;
		}
		/* fake 'cd  ..' */
		line = &view->line[1];
		break;

	case REQ_ENTER:
		break;

	default:
		return request;
	}

	/* Cleanup the stack if the tree view is at a different tree. */
	while (!*opt_path && tree_stack)
		pop_tree_stack_entry();

	switch (line->type) {
	case LINE_TREE_DIR:
		/* Depending on whether it is a subdirectory or parent link
		 * mangle the path buffer. */
		if (line == &view->line[1] && *opt_path) {
			pop_tree_stack_entry();

		} else {
			const char *basename = tree_path(line);

			push_tree_stack_entry(basename, view->lineno);
		}

		/* Trees and subtrees share the same ID, so they are not not
		 * unique like blobs. */
		flags = OPEN_RELOAD;
		request = REQ_VIEW_TREE;
		break;

	case LINE_TREE_FILE:
		flags = view_is_displayed(view) ? OPEN_SPLIT : OPEN_DEFAULT;
		request = REQ_VIEW_BLOB;
		break;

	default:
		return REQ_NONE;
	}

	open_view(view, request, flags);
	if (request == REQ_VIEW_TREE)
		view->lineno = tree_lineno;

	return REQ_NONE;
}

static bool
tree_grep(struct view *view, struct line *line)
{
	struct tree_entry *entry = line->data;
	const char *text[] = {
		entry->name,
		opt_author ? entry->author : "",
		mkdate(&entry->time, opt_date),
		NULL
	};

	return grep_text(view, text);
}

static void
tree_select(struct view *view, struct line *line)
{
	struct tree_entry *entry = line->data;

	if (line->type == LINE_TREE_FILE) {
		string_copy_rev(ref_blob, entry->id);
		string_format(opt_file, "%s%s", opt_path, tree_path(line));

	} else if (line->type != LINE_TREE_DIR) {
		return;
	}

	string_copy_rev(view->ref, entry->id);
}

static bool
tree_prepare(struct view *view)
{
	if (view->lines == 0 && opt_prefix[0]) {
		char *pos = opt_prefix;

		while (pos && *pos) {
			char *end = strchr(pos, '/');

			if (end)
				*end = 0;
			push_tree_stack_entry(pos, 0);
			pos = end;
			if (end) {
				*end = '/';
				pos++;
			}
		}

	} else if (strcmp(view->vid, view->id)) {
		opt_path[0] = 0;
	}

	return prepare_io(view, opt_cdup, view->ops->argv, TRUE);
}

static const char *tree_argv[SIZEOF_ARG] = {
	"git", "ls-tree", "%(commit)", "%(directory)", NULL
};

static struct view_ops tree_ops = {
	"file",
	tree_argv,
	NULL,
	tree_read,
	tree_draw,
	tree_request,
	tree_grep,
	tree_select,
	tree_prepare,
};

static bool
blob_read(struct view *view, char *line)
{
	if (!line)
		return TRUE;
	return add_line_text(view, line, LINE_DEFAULT) != NULL;
}

static enum request
blob_request(struct view *view, enum request request, struct line *line)
{
	switch (request) {
	case REQ_EDIT:
		open_blob_editor(view->vid);
		return REQ_NONE;
	default:
		return pager_request(view, request, line);
	}
}

static const char *blob_argv[SIZEOF_ARG] = {
	"git", "cat-file", "blob", "%(blob)", NULL
};

static struct view_ops blob_ops = {
	"line",
	blob_argv,
	NULL,
	blob_read,
	pager_draw,
	blob_request,
	pager_grep,
	pager_select,
};

/*
 * Blame backend
 *
 * Loading the blame view is a two phase job:
 *
 *  1. File content is read either using opt_file from the
 *     filesystem or using git-cat-file.
 *  2. Then blame information is incrementally added by
 *     reading output from git-blame.
 */

struct blame_commit {
	char id[SIZEOF_REV];		/* SHA1 ID. */
	char title[128];		/* First line of the commit message. */
	const char *author;		/* Author of the commit. */
	struct time time;		/* Date from the author ident. */
	char filename[128];		/* Name of file. */
	bool has_previous;		/* Was a "previous" line detected. */
};

struct blame {
	struct blame_commit *commit;
	unsigned long lineno;
	char text[1];
};

static bool
blame_open(struct view *view)
{
	char path[SIZEOF_STR];

	if (!view->prev && *opt_prefix) {
		string_copy(path, opt_file);
		if (!string_format(opt_file, "%s%s", opt_prefix, path))
			return FALSE;
	}

	if (*opt_ref || !io_open(&view->io, "%s%s", opt_cdup, opt_file)) {
		const char *blame_cat_file_argv[] = {
			"git", "cat-file", "blob", path, NULL
		};

		if (!string_format(path, "%s:%s", opt_ref, opt_file) ||
		    !start_update(view, blame_cat_file_argv, opt_cdup))
			return FALSE;
	}

	setup_update(view, opt_file);
	string_format(view->ref, "%s ...", opt_file);

	return TRUE;
}

static struct blame_commit *
get_blame_commit(struct view *view, const char *id)
{
	size_t i;

	for (i = 0; i < view->lines; i++) {
		struct blame *blame = view->line[i].data;

		if (!blame->commit)
			continue;

		if (!strncmp(blame->commit->id, id, SIZEOF_REV - 1))
			return blame->commit;
	}

	{
		struct blame_commit *commit = calloc(1, sizeof(*commit));

		if (commit)
			string_ncopy(commit->id, id, SIZEOF_REV);
		return commit;
	}
}

static bool
parse_number(const char **posref, size_t *number, size_t min, size_t max)
{
	const char *pos = *posref;

	*posref = NULL;
	pos = strchr(pos + 1, ' ');
	if (!pos || !isdigit(pos[1]))
		return FALSE;
	*number = atoi(pos + 1);
	if (*number < min || *number > max)
		return FALSE;

	*posref = pos;
	return TRUE;
}

static struct blame_commit *
parse_blame_commit(struct view *view, const char *text, int *blamed)
{
	struct blame_commit *commit;
	struct blame *blame;
	const char *pos = text + SIZEOF_REV - 2;
	size_t orig_lineno = 0;
	size_t lineno;
	size_t group;

	if (strlen(text) <= SIZEOF_REV || pos[1] != ' ')
		return NULL;

	if (!parse_number(&pos, &orig_lineno, 1, 9999999) ||
	    !parse_number(&pos, &lineno, 1, view->lines) ||
	    !parse_number(&pos, &group, 1, view->lines - lineno + 1))
		return NULL;

	commit = get_blame_commit(view, text);
	if (!commit)
		return NULL;

	*blamed += group;
	while (group--) {
		struct line *line = &view->line[lineno + group - 1];

		blame = line->data;
		blame->commit = commit;
		blame->lineno = orig_lineno + group - 1;
		line->dirty = 1;
	}

	return commit;
}

static bool
blame_read_file(struct view *view, const char *line, bool *read_file)
{
	if (!line) {
		const char *blame_argv[] = {
			"git", "blame", "--incremental",
				*opt_ref ? opt_ref : "--incremental", "--", opt_file, NULL
		};

		if (view->lines == 0 && !view->prev)
			die("No blame exist for %s", view->vid);

		if (view->lines == 0 || !start_update(view, blame_argv, opt_cdup)) {
			report("Failed to load blame data");
			return TRUE;
		}

		*read_file = FALSE;
		return FALSE;

	} else {
		size_t linelen = strlen(line);
		struct blame *blame = malloc(sizeof(*blame) + linelen);

		if (!blame)
			return FALSE;

		blame->commit = NULL;
		strncpy(blame->text, line, linelen);
		blame->text[linelen] = 0;
		return add_line_data(view, blame, LINE_BLAME_ID) != NULL;
	}
}

static bool
match_blame_header(const char *name, char **line)
{
	size_t namelen = strlen(name);
	bool matched = !strncmp(name, *line, namelen);

	if (matched)
		*line += namelen;

	return matched;
}

static bool
blame_read(struct view *view, char *line)
{
	static struct blame_commit *commit = NULL;
	static int blamed = 0;
	static bool read_file = TRUE;

	if (read_file)
		return blame_read_file(view, line, &read_file);

	if (!line) {
		/* Reset all! */
		commit = NULL;
		blamed = 0;
		read_file = TRUE;
		string_format(view->ref, "%s", view->vid);
		if (view_is_displayed(view)) {
			update_view_title(view);
			redraw_view_from(view, 0);
		}
		return TRUE;
	}

	if (!commit) {
		commit = parse_blame_commit(view, line, &blamed);
		string_format(view->ref, "%s %2d%%", view->vid,
			      view->lines ? blamed * 100 / view->lines : 0);

	} else if (match_blame_header("author ", &line)) {
		commit->author = get_author(line);

	} else if (match_blame_header("author-time ", &line)) {
		parse_timesec(&commit->time, line);

	} else if (match_blame_header("author-tz ", &line)) {
		parse_timezone(&commit->time, line);

	} else if (match_blame_header("summary ", &line)) {
		string_ncopy(commit->title, line, strlen(line));

	} else if (match_blame_header("previous ", &line)) {
		commit->has_previous = TRUE;

	} else if (match_blame_header("filename ", &line)) {
		string_ncopy(commit->filename, line, strlen(line));
		commit = NULL;
	}

	return TRUE;
}

static bool
blame_draw(struct view *view, struct line *line, unsigned int lineno)
{
	struct blame *blame = line->data;
	struct time *time = NULL;
	const char *id = NULL, *author = NULL;
	char text[SIZEOF_STR];

	if (blame->commit && *blame->commit->filename) {
		id = blame->commit->id;
		author = blame->commit->author;
		time = &blame->commit->time;
	}

	if (opt_date && draw_date(view, time))
		return TRUE;

	if (opt_author && draw_author(view, author))
		return TRUE;

	if (draw_field(view, LINE_BLAME_ID, id, ID_COLS, FALSE))
		return TRUE;

	if (draw_lineno(view, lineno))
		return TRUE;

	string_expand(text, sizeof(text), blame->text, opt_tab_size);
	draw_text(view, LINE_DEFAULT, text, TRUE);
	return TRUE;
}

static bool
check_blame_commit(struct blame *blame, bool check_null_id)
{
	if (!blame->commit)
		report("Commit data not loaded yet");
	else if (check_null_id && !strcmp(blame->commit->id, NULL_ID))
		report("No commit exist for the selected line");
	else
		return TRUE;
	return FALSE;
}

static void
setup_blame_parent_line(struct view *view, struct blame *blame)
{
	const char *diff_tree_argv[] = {
		"git", "diff-tree", "-U0", blame->commit->id,
			"--", blame->commit->filename, NULL
	};
	struct io io = {};
	int parent_lineno = -1;
	int blamed_lineno = -1;
	char *line;

	if (!io_run(&io, diff_tree_argv, NULL, IO_RD))
		return;

	while ((line = io_get(&io, '\n', TRUE))) {
		if (*line == '@') {
			char *pos = strchr(line, '+');

			parent_lineno = atoi(line + 4);
			if (pos)
				blamed_lineno = atoi(pos + 1);

		} else if (*line == '+' && parent_lineno != -1) {
			if (blame->lineno == blamed_lineno - 1 &&
			    !strcmp(blame->text, line + 1)) {
				view->lineno = parent_lineno ? parent_lineno - 1 : 0;
				break;
			}
			blamed_lineno++;
		}
	}

	io_done(&io);
}

static enum request
blame_request(struct view *view, enum request request, struct line *line)
{
	enum open_flags flags = view_is_displayed(view) ? OPEN_SPLIT : OPEN_DEFAULT;
	struct blame *blame = line->data;

	switch (request) {
	case REQ_VIEW_BLAME:
		if (check_blame_commit(blame, TRUE)) {
			string_copy(opt_ref, blame->commit->id);
			string_copy(opt_file, blame->commit->filename);
			if (blame->lineno)
				view->lineno = blame->lineno;
			open_view(view, REQ_VIEW_BLAME, OPEN_REFRESH);
		}
		break;

	case REQ_PARENT:
		if (check_blame_commit(blame, TRUE) &&
		    select_commit_parent(blame->commit->id, opt_ref,
					 blame->commit->filename)) {
			string_copy(opt_file, blame->commit->filename);
			setup_blame_parent_line(view, blame);
			open_view(view, REQ_VIEW_BLAME, OPEN_REFRESH);
		}
		break;

	case REQ_ENTER:
		if (!check_blame_commit(blame, FALSE))
			break;

		if (view_is_displayed(VIEW(REQ_VIEW_DIFF)) &&
		    !strcmp(blame->commit->id, VIEW(REQ_VIEW_DIFF)->ref))
			break;

		if (!strcmp(blame->commit->id, NULL_ID)) {
			struct view *diff = VIEW(REQ_VIEW_DIFF);
			const char *diff_index_argv[] = {
				"git", "diff-index", "--root", "--patch-with-stat",
					"-C", "-M", "HEAD", "--", view->vid, NULL
			};

			if (!blame->commit->has_previous) {
				diff_index_argv[1] = "diff";
				diff_index_argv[2] = "--no-color";
				diff_index_argv[6] = "--";
				diff_index_argv[7] = "/dev/null";
			}

			if (!prepare_update(diff, diff_index_argv, NULL)) {
				report("Failed to allocate diff command");
				break;
			}
			flags |= OPEN_PREPARED;
		}

		open_view(view, REQ_VIEW_DIFF, flags);
		if (VIEW(REQ_VIEW_DIFF)->pipe && !strcmp(blame->commit->id, NULL_ID))
			string_copy_rev(VIEW(REQ_VIEW_DIFF)->ref, NULL_ID);
		break;

	default:
		return request;
	}

	return REQ_NONE;
}

static bool
blame_grep(struct view *view, struct line *line)
{
	struct blame *blame = line->data;
	struct blame_commit *commit = blame->commit;
	const char *text[] = {
		blame->text,
		commit ? commit->title : "",
		commit ? commit->id : "",
		commit && opt_author ? commit->author : "",
		commit ? mkdate(&commit->time, opt_date) : "",
		NULL
	};

	return grep_text(view, text);
}

static void
blame_select(struct view *view, struct line *line)
{
	struct blame *blame = line->data;
	struct blame_commit *commit = blame->commit;

	if (!commit)
		return;

	if (!strcmp(commit->id, NULL_ID))
		string_ncopy(ref_commit, "HEAD", 4);
	else
		string_copy_rev(ref_commit, commit->id);
}

static struct view_ops blame_ops = {
	"line",
	NULL,
	blame_open,
	blame_read,
	blame_draw,
	blame_request,
	blame_grep,
	blame_select,
};

/*
 * Branch backend
 */

struct branch {
	const char *author;		/* Author of the last commit. */
	struct time time;		/* Date of the last activity. */
	const struct ref *ref;		/* Name and commit ID information. */
};

static const struct ref branch_all;

static const enum sort_field branch_sort_fields[] = {
	ORDERBY_NAME, ORDERBY_DATE, ORDERBY_AUTHOR
};
static struct sort_state branch_sort_state = SORT_STATE(branch_sort_fields);

static int
branch_compare(const void *l1, const void *l2)
{
	const struct branch *branch1 = ((const struct line *) l1)->data;
	const struct branch *branch2 = ((const struct line *) l2)->data;

	switch (get_sort_field(branch_sort_state)) {
	case ORDERBY_DATE:
		return sort_order(branch_sort_state, timecmp(&branch1->time, &branch2->time));

	case ORDERBY_AUTHOR:
		return sort_order(branch_sort_state, strcmp(branch1->author, branch2->author));

	case ORDERBY_NAME:
	default:
		return sort_order(branch_sort_state, strcmp(branch1->ref->name, branch2->ref->name));
	}
}

static bool
branch_draw(struct view *view, struct line *line, unsigned int lineno)
{
	struct branch *branch = line->data;
	enum line_type type = branch->ref->head ? LINE_MAIN_HEAD : LINE_DEFAULT;

	if (opt_date && draw_date(view, &branch->time))
		return TRUE;

	if (opt_author && draw_author(view, branch->author))
		return TRUE;

	draw_text(view, type, branch->ref == &branch_all ? "All branches" : branch->ref->name, TRUE);
	return TRUE;
}

static enum request
branch_request(struct view *view, enum request request, struct line *line)
{
	struct branch *branch = line->data;

	switch (request) {
	case REQ_REFRESH:
		load_refs();
		open_view(view, REQ_VIEW_BRANCH, OPEN_REFRESH);
		return REQ_NONE;

	case REQ_TOGGLE_SORT_FIELD:
	case REQ_TOGGLE_SORT_ORDER:
		sort_view(view, request, &branch_sort_state, branch_compare);
		return REQ_NONE;

	case REQ_ENTER:
		if (branch->ref == &branch_all) {
			const char *all_branches_argv[] = {
				"git", "log", "--no-color", "--pretty=raw", "--parents",
				      "--topo-order", "--all", NULL
			};
			struct view *main_view = VIEW(REQ_VIEW_MAIN);

			if (!prepare_update(main_view, all_branches_argv, NULL)) {
				report("Failed to load view of all branches");
				return REQ_NONE;
			}
			open_view(view, REQ_VIEW_MAIN, OPEN_PREPARED | OPEN_SPLIT);
		} else {
			open_view(view, REQ_VIEW_MAIN, OPEN_SPLIT);
		}
		return REQ_NONE;

	default:
		return request;
	}
}

static bool
branch_read(struct view *view, char *line)
{
	static char id[SIZEOF_REV];
	struct branch *reference;
	size_t i;

	if (!line)
		return TRUE;

	switch (get_line_type(line)) {
	case LINE_COMMIT:
		string_copy_rev(id, line + STRING_SIZE("commit "));
		return TRUE;

	case LINE_AUTHOR:
		for (i = 0, reference = NULL; i < view->lines; i++) {
			struct branch *branch = view->line[i].data;

			if (strcmp(branch->ref->id, id))
				continue;

			view->line[i].dirty = TRUE;
			if (reference) {
				branch->author = reference->author;
				branch->time = reference->time;
				continue;
			}

			parse_author_line(line + STRING_SIZE("author "),
					  &branch->author, &branch->time);
			reference = branch;
		}
		return TRUE;

	default:
		return TRUE;
	}

}

static bool
branch_open_visitor(void *data, const struct ref *ref)
{
	struct view *view = data;
	struct branch *branch;

	if (ref->tag || ref->ltag || ref->remote)
		return TRUE;

	branch = calloc(1, sizeof(*branch));
	if (!branch)
		return FALSE;

	branch->ref = ref;
	return !!add_line_data(view, branch, LINE_DEFAULT);
}

static bool
branch_open(struct view *view)
{
	const char *branch_log[] = {
		"git", "log", "--no-color", "--pretty=raw",
			"--simplify-by-decoration", "--all", NULL
	};

	if (!start_update(view, branch_log, NULL)) {
		report("Failed to load branch data");
		return TRUE;
	}

	setup_update(view, view->id);
	branch_open_visitor(view, &branch_all);
	foreach_ref(branch_open_visitor, view);
	view->p_restore = TRUE;

	return TRUE;
}

static bool
branch_grep(struct view *view, struct line *line)
{
	struct branch *branch = line->data;
	const char *text[] = {
		branch->ref->name,
		branch->author,
		NULL
	};

	return grep_text(view, text);
}

static void
branch_select(struct view *view, struct line *line)
{
	struct branch *branch = line->data;

	string_copy_rev(view->ref, branch->ref->id);
	string_copy_rev(ref_commit, branch->ref->id);
	string_copy_rev(ref_head, branch->ref->id);
	string_copy_rev(ref_branch, branch->ref->name);
}

static struct view_ops branch_ops = {
	"branch",
	NULL,
	branch_open,
	branch_read,
	branch_draw,
	branch_request,
	branch_grep,
	branch_select,
};

/*
 * Status backend
 */

struct status {
	char status;
	struct {
		mode_t mode;
		char rev[SIZEOF_REV];
		char name[SIZEOF_STR];
	} old;
	struct {
		mode_t mode;
		char rev[SIZEOF_REV];
		char name[SIZEOF_STR];
	} new;
};

static char status_onbranch[SIZEOF_STR];
static struct status stage_status;
static enum line_type stage_line_type;
static size_t stage_chunks;
static int *stage_chunk;

DEFINE_ALLOCATOR(realloc_ints, int, 32)

/* This should work even for the "On branch" line. */
static inline bool
status_has_none(struct view *view, struct line *line)
{
	return line < view->line + view->lines && !line[1].data;
}

/* Get fields from the diff line:
 * :100644 100644 06a5d6ae9eca55be2e0e585a152e6b1336f2b20e 0000000000000000000000000000000000000000 M
 */
static inline bool
status_get_diff(struct status *file, const char *buf, size_t bufsize)
{
	const char *old_mode = buf +  1;
	const char *new_mode = buf +  8;
	const char *old_rev  = buf + 15;
	const char *new_rev  = buf + 56;
	const char *status   = buf + 97;

	if (bufsize < 98 ||
	    old_mode[-1] != ':' ||
	    new_mode[-1] != ' ' ||
	    old_rev[-1]  != ' ' ||
	    new_rev[-1]  != ' ' ||
	    status[-1]   != ' ')
		return FALSE;

	file->status = *status;

	string_copy_rev(file->old.rev, old_rev);
	string_copy_rev(file->new.rev, new_rev);

	file->old.mode = strtoul(old_mode, NULL, 8);
	file->new.mode = strtoul(new_mode, NULL, 8);

	file->old.name[0] = file->new.name[0] = 0;

	return TRUE;
}

static bool
status_run(struct view *view, const char *argv[], char status, enum line_type type)
{
	struct status *unmerged = NULL;
	char *buf;
	struct io io = {};

	if (!io_run(&io, argv, opt_cdup, IO_RD))
		return FALSE;

	add_line_data(view, NULL, type);

	while ((buf = io_get(&io, 0, TRUE))) {
		struct status *file = unmerged;

		if (!file) {
			file = calloc(1, sizeof(*file));
			if (!file || !add_line_data(view, file, type))
				goto error_out;
		}

		/* Parse diff info part. */
		if (status) {
			file->status = status;
			if (status == 'A')
				string_copy(file->old.rev, NULL_ID);

		} else if (!file->status || file == unmerged) {
			if (!status_get_diff(file, buf, strlen(buf)))
				goto error_out;

			buf = io_get(&io, 0, TRUE);
			if (!buf)
				break;

			/* Collapse all modified entries that follow an
			 * associated unmerged entry. */
			if (unmerged == file) {
				unmerged->status = 'U';
				unmerged = NULL;
			} else if (file->status == 'U') {
				unmerged = file;
			}
		}

		/* Grab the old name for rename/copy. */
		if (!*file->old.name &&
		    (file->status == 'R' || file->status == 'C')) {
			string_ncopy(file->old.name, buf, strlen(buf));

			buf = io_get(&io, 0, TRUE);
			if (!buf)
				break;
		}

		/* git-ls-files just delivers a NUL separated list of
		 * file names similar to the second half of the
		 * git-diff-* output. */
		string_ncopy(file->new.name, buf, strlen(buf));
		if (!*file->old.name)
			string_copy(file->old.name, file->new.name);
		file = NULL;
	}

	if (io_error(&io)) {
error_out:
		io_done(&io);
		return FALSE;
	}

	if (!view->line[view->lines - 1].data)
		add_line_data(view, NULL, LINE_STAT_NONE);

	io_done(&io);
	return TRUE;
}

/* Don't show unmerged entries in the staged section. */
static const char *status_diff_index_argv[] = {
	"git", "diff-index", "-z", "--diff-filter=ACDMRTXB",
			     "--cached", "-M", "HEAD", NULL
};

static const char *status_diff_files_argv[] = {
	"git", "diff-files", "-z", NULL
};

static const char *status_list_other_argv[] = {
	"git", "ls-files", "-z", "--others", "--exclude-standard", opt_prefix, NULL
};

static const char *status_list_no_head_argv[] = {
	"git", "ls-files", "-z", "--cached", "--exclude-standard", NULL
};

static const char *update_index_argv[] = {
	"git", "update-index", "-q", "--unmerged", "--refresh", NULL
};

/* Restore the previous line number to stay in the context or select a
 * line with something that can be updated. */
static void
status_restore(struct view *view)
{
	if (view->p_lineno >= view->lines)
		view->p_lineno = view->lines - 1;
	while (view->p_lineno < view->lines && !view->line[view->p_lineno].data)
		view->p_lineno++;
	while (view->p_lineno > 0 && !view->line[view->p_lineno].data)
		view->p_lineno--;

	/* If the above fails, always skip the "On branch" line. */
	if (view->p_lineno < view->lines)
		view->lineno = view->p_lineno;
	else
		view->lineno = 1;

	if (view->lineno < view->offset)
		view->offset = view->lineno;
	else if (view->offset + view->height <= view->lineno)
		view->offset = view->lineno - view->height + 1;

	view->p_restore = FALSE;
}

static void
status_update_onbranch(void)
{
	static const char *paths[][2] = {
		{ "rebase-apply/rebasing",	"Rebasing" },
		{ "rebase-apply/applying",	"Applying mailbox" },
		{ "rebase-apply/",		"Rebasing mailbox" },
		{ "rebase-merge/interactive",	"Interactive rebase" },
		{ "rebase-merge/",		"Rebase merge" },
		{ "MERGE_HEAD",			"Merging" },
		{ "BISECT_LOG",			"Bisecting" },
		{ "HEAD",			"On branch" },
	};
	char buf[SIZEOF_STR];
	struct stat stat;
	int i;

	if (is_initial_commit()) {
		string_copy(status_onbranch, "Initial commit");
		return;
	}

	for (i = 0; i < ARRAY_SIZE(paths); i++) {
		char *head = opt_head;

		if (!string_format(buf, "%s/%s", opt_git_dir, paths[i][0]) ||
		    lstat(buf, &stat) < 0)
			continue;

		if (!*opt_head) {
			struct io io = {};

			if (io_open(&io, "%s/rebase-merge/head-name", opt_git_dir) &&
			    io_read_buf(&io, buf, sizeof(buf))) {
				head = buf;
				if (!prefixcmp(head, "refs/heads/"))
					head += STRING_SIZE("refs/heads/");
			}
		}

		if (!string_format(status_onbranch, "%s %s", paths[i][1], head))
			string_copy(status_onbranch, opt_head);
		return;
	}

	string_copy(status_onbranch, "Not currently on any branch");
}

/* First parse staged info using git-diff-index(1), then parse unstaged
 * info using git-diff-files(1), and finally untracked files using
 * git-ls-files(1). */
static bool
status_open(struct view *view)
{
	reset_view(view);

	add_line_data(view, NULL, LINE_STAT_HEAD);
	status_update_onbranch();

	io_run_bg(update_index_argv);

	if (is_initial_commit()) {
		if (!status_run(view, status_list_no_head_argv, 'A', LINE_STAT_STAGED))
			return FALSE;
	} else if (!status_run(view, status_diff_index_argv, 0, LINE_STAT_STAGED)) {
		return FALSE;
	}

	if (!status_run(view, status_diff_files_argv, 0, LINE_STAT_UNSTAGED) ||
	    !status_run(view, status_list_other_argv, '?', LINE_STAT_UNTRACKED))
		return FALSE;

	/* Restore the exact position or use the specialized restore
	 * mode? */
	if (!view->p_restore)
		status_restore(view);
	return TRUE;
}

static bool
status_draw(struct view *view, struct line *line, unsigned int lineno)
{
	struct status *status = line->data;
	enum line_type type;
	const char *text;

	if (!status) {
		switch (line->type) {
		case LINE_STAT_STAGED:
			type = LINE_STAT_SECTION;
			text = "Changes to be committed:";
			break;

		case LINE_STAT_UNSTAGED:
			type = LINE_STAT_SECTION;
			text = "Changed but not updated:";
			break;

		case LINE_STAT_UNTRACKED:
			type = LINE_STAT_SECTION;
			text = "Untracked files:";
			break;

		case LINE_STAT_NONE:
			type = LINE_DEFAULT;
			text = "  (no files)";
			break;

		case LINE_STAT_HEAD:
			type = LINE_STAT_HEAD;
			text = status_onbranch;
			break;

		default:
			return FALSE;
		}
	} else {
		static char buf[] = { '?', ' ', ' ', ' ', 0 };

		buf[0] = status->status;
		if (draw_text(view, line->type, buf, TRUE))
			return TRUE;
		type = LINE_DEFAULT;
		text = status->new.name;
	}

	draw_text(view, type, text, TRUE);
	return TRUE;
}

static enum request
status_load_error(struct view *view, struct view *stage, const char *path)
{
	if (displayed_views() == 2 || display[current_view] != view)
		maximize_view(view);
	report("Failed to load '%s': %s", path, io_strerror(&stage->io));
	return REQ_NONE;
}

static enum request
status_enter(struct view *view, struct line *line)
{
	struct status *status = line->data;
	const char *oldpath = status ? status->old.name : NULL;
	/* Diffs for unmerged entries are empty when passing the new
	 * path, so leave it empty. */
	const char *newpath = status && status->status != 'U' ? status->new.name : NULL;
	const char *info;
	enum open_flags split;
	struct view *stage = VIEW(REQ_VIEW_STAGE);

	if (line->type == LINE_STAT_NONE ||
	    (!status && line[1].type == LINE_STAT_NONE)) {
		report("No file to diff");
		return REQ_NONE;
	}

	switch (line->type) {
	case LINE_STAT_STAGED:
		if (is_initial_commit()) {
			const char *no_head_diff_argv[] = {
				"git", "diff", "--no-color", "--patch-with-stat",
					"--", "/dev/null", newpath, NULL
			};

			if (!prepare_update(stage, no_head_diff_argv, opt_cdup))
				return status_load_error(view, stage, newpath);
		} else {
			const char *index_show_argv[] = {
				"git", "diff-index", "--root", "--patch-with-stat",
					"-C", "-M", "--cached", "HEAD", "--",
					oldpath, newpath, NULL
			};

			if (!prepare_update(stage, index_show_argv, opt_cdup))
				return status_load_error(view, stage, newpath);
		}

		if (status)
			info = "Staged changes to %s";
		else
			info = "Staged changes";
		break;

	case LINE_STAT_UNSTAGED:
	{
		const char *files_show_argv[] = {
			"git", "diff-files", "--root", "--patch-with-stat",
				"-C", "-M", "--", oldpath, newpath, NULL
		};

		if (!prepare_update(stage, files_show_argv, opt_cdup))
			return status_load_error(view, stage, newpath);
		if (status)
			info = "Unstaged changes to %s";
		else
			info = "Unstaged changes";
		break;
	}
	case LINE_STAT_UNTRACKED:
		if (!newpath) {
			report("No file to show");
			return REQ_NONE;
		}

	    	if (!suffixcmp(status->new.name, -1, "/")) {
			report("Cannot display a directory");
			return REQ_NONE;
		}

		if (!prepare_update_file(stage, newpath))
			return status_load_error(view, stage, newpath);
		info = "Untracked file %s";
		break;

	case LINE_STAT_HEAD:
		return REQ_NONE;

	default:
		die("line type %d not handled in switch", line->type);
	}

	split = view_is_displayed(view) ? OPEN_SPLIT : OPEN_DEFAULT;
	open_view(view, REQ_VIEW_STAGE, OPEN_PREPARED | split);
	if (view_is_displayed(VIEW(REQ_VIEW_STAGE))) {
		if (status) {
			stage_status = *status;
		} else {
			memset(&stage_status, 0, sizeof(stage_status));
		}

		stage_line_type = line->type;
		stage_chunks = 0;
		string_format(VIEW(REQ_VIEW_STAGE)->ref, info, stage_status.new.name);
	}

	return REQ_NONE;
}

static bool
status_exists(struct status *status, enum line_type type)
{
	struct view *view = VIEW(REQ_VIEW_STATUS);
	unsigned long lineno;

	for (lineno = 0; lineno < view->lines; lineno++) {
		struct line *line = &view->line[lineno];
		struct status *pos = line->data;

		if (line->type != type)
			continue;
		if (!pos && (!status || !status->status) && line[1].data) {
			select_view_line(view, lineno);
			return TRUE;
		}
		if (pos && !strcmp(status->new.name, pos->new.name)) {
			select_view_line(view, lineno);
			return TRUE;
		}
	}

	return FALSE;
}


static bool
status_update_prepare(struct io *io, enum line_type type)
{
	const char *staged_argv[] = {
		"git", "update-index", "-z", "--index-info", NULL
	};
	const char *others_argv[] = {
		"git", "update-index", "-z", "--add", "--remove", "--stdin", NULL
	};

	switch (type) {
	case LINE_STAT_STAGED:
		return io_run(io, staged_argv, opt_cdup, IO_WR);

	case LINE_STAT_UNSTAGED:
	case LINE_STAT_UNTRACKED:
		return io_run(io, others_argv, opt_cdup, IO_WR);

	default:
		die("line type %d not handled in switch", type);
		return FALSE;
	}
}

static bool
status_update_write(struct io *io, struct status *status, enum line_type type)
{
	char buf[SIZEOF_STR];
	size_t bufsize = 0;

	switch (type) {
	case LINE_STAT_STAGED:
		if (!string_format_from(buf, &bufsize, "%06o %s\t%s%c",
					status->old.mode,
					status->old.rev,
					status->old.name, 0))
			return FALSE;
		break;

	case LINE_STAT_UNSTAGED:
	case LINE_STAT_UNTRACKED:
		if (!string_format_from(buf, &bufsize, "%s%c", status->new.name, 0))
			return FALSE;
		break;

	default:
		die("line type %d not handled in switch", type);
	}

	return io_write(io, buf, bufsize);
}

static bool
status_update_file(struct status *status, enum line_type type)
{
	struct io io = {};
	bool result;

	if (!status_update_prepare(&io, type))
		return FALSE;

	result = status_update_write(&io, status, type);
	return io_done(&io) && result;
}

static bool
status_update_files(struct view *view, struct line *line)
{
	char buf[sizeof(view->ref)];
	struct io io = {};
	bool result = TRUE;
	struct line *pos = view->line + view->lines;
	int files = 0;
	int file, done;
	int cursor_y = -1, cursor_x = -1;

	if (!status_update_prepare(&io, line->type))
		return FALSE;

	for (pos = line; pos < view->line + view->lines && pos->data; pos++)
		files++;

	string_copy(buf, view->ref);
	getsyx(cursor_y, cursor_x);
	for (file = 0, done = 5; result && file < files; line++, file++) {
		int almost_done = file * 100 / files;

		if (almost_done > done) {
			done = almost_done;
			string_format(view->ref, "updating file %u of %u (%d%% done)",
				      file, files, done);
			update_view_title(view);
			setsyx(cursor_y, cursor_x);
			doupdate();
		}
		result = status_update_write(&io, line->data, line->type);
	}
	string_copy(view->ref, buf);

	return io_done(&io) && result;
}

static bool
status_update(struct view *view)
{
	struct line *line = &view->line[view->lineno];

	assert(view->lines);

	if (!line->data) {
		/* This should work even for the "On branch" line. */
		if (line < view->line + view->lines && !line[1].data) {
			report("Nothing to update");
			return FALSE;
		}

		if (!status_update_files(view, line + 1)) {
			report("Failed to update file status");
			return FALSE;
		}

	} else if (!status_update_file(line->data, line->type)) {
		report("Failed to update file status");
		return FALSE;
	}

	return TRUE;
}

static bool
status_revert(struct status *status, enum line_type type, bool has_none)
{
	if (!status || type != LINE_STAT_UNSTAGED) {
		if (type == LINE_STAT_STAGED) {
			report("Cannot revert changes to staged files");
		} else if (type == LINE_STAT_UNTRACKED) {
			report("Cannot revert changes to untracked files");
		} else if (has_none) {
			report("Nothing to revert");
		} else {
			report("Cannot revert changes to multiple files");
		}

	} else if (prompt_yesno("Are you sure you want to revert changes?")) {
		char mode[10] = "100644";
		const char *reset_argv[] = {
			"git", "update-index", "--cacheinfo", mode,
				status->old.rev, status->old.name, NULL
		};
		const char *checkout_argv[] = {
			"git", "checkout", "--", status->old.name, NULL
		};

		if (status->status == 'U') {
			string_format(mode, "%5o", status->old.mode);

			if (status->old.mode == 0 && status->new.mode == 0) {
				reset_argv[2] = "--force-remove";
				reset_argv[3] = status->old.name;
				reset_argv[4] = NULL;
			}

			if (!io_run_fg(reset_argv, opt_cdup))
				return FALSE;
			if (status->old.mode == 0 && status->new.mode == 0)
				return TRUE;
		}

		return io_run_fg(checkout_argv, opt_cdup);
	}

	return FALSE;
}

static enum request
status_request(struct view *view, enum request request, struct line *line)
{
	struct status *status = line->data;

	switch (request) {
	case REQ_STATUS_UPDATE:
		if (!status_update(view))
			return REQ_NONE;
		break;

	case REQ_STATUS_REVERT:
		if (!status_revert(status, line->type, status_has_none(view, line)))
			return REQ_NONE;
		break;

	case REQ_STATUS_MERGE:
		if (!status || status->status != 'U') {
			report("Merging only possible for files with unmerged status ('U').");
			return REQ_NONE;
		}
		open_mergetool(status->new.name);
		break;

	case REQ_EDIT:
		if (!status)
			return request;
		if (status->status == 'D') {
			report("File has been deleted.");
			return REQ_NONE;
		}

		open_editor(status->new.name);
		break;

	case REQ_VIEW_BLAME:
		if (status)
			opt_ref[0] = 0;
		return request;

	case REQ_ENTER:
		/* After returning the status view has been split to
		 * show the stage view. No further reloading is
		 * necessary. */
		return status_enter(view, line);

	case REQ_REFRESH:
		/* Simply reload the view. */
		break;

	default:
		return request;
	}

	open_view(view, REQ_VIEW_STATUS, OPEN_RELOAD);

	return REQ_NONE;
}

static void
status_select(struct view *view, struct line *line)
{
	struct status *status = line->data;
	char file[SIZEOF_STR] = "all files";
	const char *text;
	const char *key;

	if (status && !string_format(file, "'%s'", status->new.name))
		return;

	if (!status && line[1].type == LINE_STAT_NONE)
		line++;

	switch (line->type) {
	case LINE_STAT_STAGED:
		text = "Press %s to unstage %s for commit";
		break;

	case LINE_STAT_UNSTAGED:
		text = "Press %s to stage %s for commit";
		break;

	case LINE_STAT_UNTRACKED:
		text = "Press %s to stage %s for addition";
		break;

	case LINE_STAT_HEAD:
	case LINE_STAT_NONE:
		text = "Nothing to update";
		break;

	default:
		die("line type %d not handled in switch", line->type);
	}

	if (status && status->status == 'U') {
		text = "Press %s to resolve conflict in %s";
		key = get_key(KEYMAP_STATUS, REQ_STATUS_MERGE);

	} else {
		key = get_key(KEYMAP_STATUS, REQ_STATUS_UPDATE);
	}

	string_format(view->ref, text, key, file);
	if (status)
		string_copy(opt_file, status->new.name);
}

static bool
status_grep(struct view *view, struct line *line)
{
	struct status *status = line->data;

	if (status) {
		const char buf[2] = { status->status, 0 };
		const char *text[] = { status->new.name, buf, NULL };

		return grep_text(view, text);
	}

	return FALSE;
}

static struct view_ops status_ops = {
	"file",
	NULL,
	status_open,
	NULL,
	status_draw,
	status_request,
	status_grep,
	status_select,
};


static bool
stage_diff_write(struct io *io, struct line *line, struct line *end)
{
	while (line < end) {
		if (!io_write(io, line->data, strlen(line->data)) ||
		    !io_write(io, "\n", 1))
			return FALSE;
		line++;
		if (line->type == LINE_DIFF_CHUNK ||
		    line->type == LINE_DIFF_HEADER)
			break;
	}

	return TRUE;
}

static struct line *
stage_diff_find(struct view *view, struct line *line, enum line_type type)
{
	for (; view->line < line; line--)
		if (line->type == type)
			return line;

	return NULL;
}

static bool
stage_apply_chunk(struct view *view, struct line *chunk, bool revert)
{
	const char *apply_argv[SIZEOF_ARG] = {
		"git", "apply", "--whitespace=nowarn", NULL
	};
	struct line *diff_hdr;
	struct io io = {};
	int argc = 3;

	diff_hdr = stage_diff_find(view, chunk, LINE_DIFF_HEADER);
	if (!diff_hdr)
		return FALSE;

	if (!revert)
		apply_argv[argc++] = "--cached";
	if (revert || stage_line_type == LINE_STAT_STAGED)
		apply_argv[argc++] = "-R";
	apply_argv[argc++] = "-";
	apply_argv[argc++] = NULL;
	if (!io_run(&io, apply_argv, opt_cdup, IO_WR))
		return FALSE;

	if (!stage_diff_write(&io, diff_hdr, chunk) ||
	    !stage_diff_write(&io, chunk, view->line + view->lines))
		chunk = NULL;

	io_done(&io);
	io_run_bg(update_index_argv);

	return chunk ? TRUE : FALSE;
}

static bool
stage_update(struct view *view, struct line *line)
{
	struct line *chunk = NULL;

	if (!is_initial_commit() && stage_line_type != LINE_STAT_UNTRACKED)
		chunk = stage_diff_find(view, line, LINE_DIFF_CHUNK);

	if (chunk) {
		if (!stage_apply_chunk(view, chunk, FALSE)) {
			report("Failed to apply chunk");
			return FALSE;
		}

	} else if (!stage_status.status) {
		view = VIEW(REQ_VIEW_STATUS);

		for (line = view->line; line < view->line + view->lines; line++)
			if (line->type == stage_line_type)
				break;

		if (!status_update_files(view, line + 1)) {
			report("Failed to update files");
			return FALSE;
		}

	} else if (!status_update_file(&stage_status, stage_line_type)) {
		report("Failed to update file");
		return FALSE;
	}

	return TRUE;
}

static bool
stage_revert(struct view *view, struct line *line)
{
	struct line *chunk = NULL;

	if (!is_initial_commit() && stage_line_type == LINE_STAT_UNSTAGED)
		chunk = stage_diff_find(view, line, LINE_DIFF_CHUNK);

	if (chunk) {
		if (!prompt_yesno("Are you sure you want to revert changes?"))
			return FALSE;

		if (!stage_apply_chunk(view, chunk, TRUE)) {
			report("Failed to revert chunk");
			return FALSE;
		}
		return TRUE;

	} else {
		return status_revert(stage_status.status ? &stage_status : NULL,
				     stage_line_type, FALSE);
	}
}


static void
stage_next(struct view *view, struct line *line)
{
	int i;

	if (!stage_chunks) {
		for (line = view->line; line < view->line + view->lines; line++) {
			if (line->type != LINE_DIFF_CHUNK)
				continue;

			if (!realloc_ints(&stage_chunk, stage_chunks, 1)) {
				report("Allocation failure");
				return;
			}

			stage_chunk[stage_chunks++] = line - view->line;
		}
	}

	for (i = 0; i < stage_chunks; i++) {
		if (stage_chunk[i] > view->lineno) {
			do_scroll_view(view, stage_chunk[i] - view->lineno);
			report("Chunk %d of %d", i + 1, stage_chunks);
			return;
		}
	}

	report("No next chunk found");
}

static enum request
stage_request(struct view *view, enum request request, struct line *line)
{
	switch (request) {
	case REQ_STATUS_UPDATE:
		if (!stage_update(view, line))
			return REQ_NONE;
		break;

	case REQ_STATUS_REVERT:
		if (!stage_revert(view, line))
			return REQ_NONE;
		break;

	case REQ_STAGE_NEXT:
		if (stage_line_type == LINE_STAT_UNTRACKED) {
			report("File is untracked; press %s to add",
			       get_key(KEYMAP_STAGE, REQ_STATUS_UPDATE));
			return REQ_NONE;
		}
		stage_next(view, line);
		return REQ_NONE;

	case REQ_EDIT:
		if (!stage_status.new.name[0])
			return request;
		if (stage_status.status == 'D') {
			report("File has been deleted.");
			return REQ_NONE;
		}

		open_editor(stage_status.new.name);
		break;

	case REQ_REFRESH:
		/* Reload everything ... */
		break;

	case REQ_VIEW_BLAME:
		if (stage_status.new.name[0]) {
			string_copy(opt_file, stage_status.new.name);
			opt_ref[0] = 0;
		}
		return request;

	case REQ_ENTER:
		return pager_request(view, request, line);

	default:
		return request;
	}

	VIEW(REQ_VIEW_STATUS)->p_restore = TRUE;
	open_view(view, REQ_VIEW_STATUS, OPEN_REFRESH);

	/* Check whether the staged entry still exists, and close the
	 * stage view if it doesn't. */
	if (!status_exists(&stage_status, stage_line_type)) {
		status_restore(VIEW(REQ_VIEW_STATUS));
		return REQ_VIEW_CLOSE;
	}

	if (stage_line_type == LINE_STAT_UNTRACKED) {
	    	if (!suffixcmp(stage_status.new.name, -1, "/")) {
			report("Cannot display a directory");
			return REQ_NONE;
		}

		if (!prepare_update_file(view, stage_status.new.name)) {
			report("Failed to open file: %s", strerror(errno));
			return REQ_NONE;
		}
	}
	open_view(view, REQ_VIEW_STAGE, OPEN_REFRESH);

	return REQ_NONE;
}

static struct view_ops stage_ops = {
	"line",
	NULL,
	NULL,
	pager_read,
	pager_draw,
	stage_request,
	pager_grep,
	pager_select,
};


/*
 * Revision graph
 */

struct commit {
	char id[SIZEOF_REV];		/* SHA1 ID. */
	char title[128];		/* First line of the commit message. */
	const char *author;		/* Author of the commit. */
	struct time time;		/* Date from the author ident. */
	struct ref_list *refs;		/* Repository references. */
	chtype graph[SIZEOF_REVGRAPH];	/* Ancestry chain graphics. */
	size_t graph_size;		/* The width of the graph array. */
	bool has_parents;		/* Rewritten --parents seen. */
};

/* Size of rev graph with no  "padding" columns */
#define SIZEOF_REVITEMS	(SIZEOF_REVGRAPH - (SIZEOF_REVGRAPH / 2))

struct rev_graph {
	struct rev_graph *prev, *next, *parents;
	char rev[SIZEOF_REVITEMS][SIZEOF_REV];
	size_t size;
	struct commit *commit;
	size_t pos;
	unsigned int boundary:1;
};

/* Parents of the commit being visualized. */
static struct rev_graph graph_parents[4];

/* The current stack of revisions on the graph. */
static struct rev_graph graph_stacks[4] = {
	{ &graph_stacks[3], &graph_stacks[1], &graph_parents[0] },
	{ &graph_stacks[0], &graph_stacks[2], &graph_parents[1] },
	{ &graph_stacks[1], &graph_stacks[3], &graph_parents[2] },
	{ &graph_stacks[2], &graph_stacks[0], &graph_parents[3] },
};

static inline bool
graph_parent_is_merge(struct rev_graph *graph)
{
	return graph->parents->size > 1;
}

static inline void
append_to_rev_graph(struct rev_graph *graph, chtype symbol)
{
	struct commit *commit = graph->commit;

	if (commit->graph_size < ARRAY_SIZE(commit->graph) - 1)
		commit->graph[commit->graph_size++] = symbol;
}

static void
clear_rev_graph(struct rev_graph *graph)
{
	graph->boundary = 0;
	graph->size = graph->pos = 0;
	graph->commit = NULL;
	memset(graph->parents, 0, sizeof(*graph->parents));
}

static void
done_rev_graph(struct rev_graph *graph)
{
	if (graph_parent_is_merge(graph) &&
	    graph->pos < graph->size - 1 &&
	    graph->next->size == graph->size + graph->parents->size - 1) {
		size_t i = graph->pos + graph->parents->size - 1;

		graph->commit->graph_size = i * 2;
		while (i < graph->next->size - 1) {
			append_to_rev_graph(graph, ' ');
			append_to_rev_graph(graph, '\\');
			i++;
		}
	}

	clear_rev_graph(graph);
}

static void
push_rev_graph(struct rev_graph *graph, const char *parent)
{
	int i;

	/* "Collapse" duplicate parents lines.
	 *
	 * FIXME: This needs to also update update the drawn graph but
	 * for now it just serves as a method for pruning graph lines. */
	for (i = 0; i < graph->size; i++)
		if (!strncmp(graph->rev[i], parent, SIZEOF_REV))
			return;

	if (graph->size < SIZEOF_REVITEMS) {
		string_copy_rev(graph->rev[graph->size++], parent);
	}
}

static chtype
get_rev_graph_symbol(struct rev_graph *graph)
{
	chtype symbol;

	if (graph->boundary)
		symbol = REVGRAPH_BOUND;
	else if (graph->parents->size == 0)
		symbol = REVGRAPH_INIT;
	else if (graph_parent_is_merge(graph))
		symbol = REVGRAPH_MERGE;
	else if (graph->pos >= graph->size)
		symbol = REVGRAPH_BRANCH;
	else
		symbol = REVGRAPH_COMMIT;

	return symbol;
}

static void
draw_rev_graph(struct rev_graph *graph)
{
	struct rev_filler {
		chtype separator, line;
	};
	enum { DEFAULT, RSHARP, RDIAG, LDIAG };
	static struct rev_filler fillers[] = {
		{ ' ',	'|' },
		{ '`',	'.' },
		{ '\'',	' ' },
		{ '/',	' ' },
	};
	chtype symbol = get_rev_graph_symbol(graph);
	struct rev_filler *filler;
	size_t i;

	fillers[DEFAULT].line = opt_line_graphics ? ACS_VLINE : '|';
	filler = &fillers[DEFAULT];

	for (i = 0; i < graph->pos; i++) {
		append_to_rev_graph(graph, filler->line);
		if (graph_parent_is_merge(graph->prev) &&
		    graph->prev->pos == i)
			filler = &fillers[RSHARP];

		append_to_rev_graph(graph, filler->separator);
	}

	/* Place the symbol for this revision. */
	append_to_rev_graph(graph, symbol);

	if (graph->prev->size > graph->size)
		filler = &fillers[RDIAG];
	else
		filler = &fillers[DEFAULT];

	i++;

	for (; i < graph->size; i++) {
		append_to_rev_graph(graph, filler->separator);
		append_to_rev_graph(graph, filler->line);
		if (graph_parent_is_merge(graph->prev) &&
		    i < graph->prev->pos + graph->parents->size)
			filler = &fillers[RSHARP];
		if (graph->prev->size > graph->size)
			filler = &fillers[LDIAG];
	}

	if (graph->prev->size > graph->size) {
		append_to_rev_graph(graph, filler->separator);
		if (filler->line != ' ')
			append_to_rev_graph(graph, filler->line);
	}
}

/* Prepare the next rev graph */
static void
prepare_rev_graph(struct rev_graph *graph)
{
	size_t i;

	/* First, traverse all lines of revisions up to the active one. */
	for (graph->pos = 0; graph->pos < graph->size; graph->pos++) {
		if (!strcmp(graph->rev[graph->pos], graph->commit->id))
			break;

		push_rev_graph(graph->next, graph->rev[graph->pos]);
	}

	/* Interleave the new revision parent(s). */
	for (i = 0; !graph->boundary && i < graph->parents->size; i++)
		push_rev_graph(graph->next, graph->parents->rev[i]);

	/* Lastly, put any remaining revisions. */
	for (i = graph->pos + 1; i < graph->size; i++)
		push_rev_graph(graph->next, graph->rev[i]);
}

static void
update_rev_graph(struct view *view, struct rev_graph *graph)
{
	/* If this is the finalizing update ... */
	if (graph->commit)
		prepare_rev_graph(graph);

	/* Graph visualization needs a one rev look-ahead,
	 * so the first update doesn't visualize anything. */
	if (!graph->prev->commit)
		return;

	if (view->lines > 2)
		view->line[view->lines - 3].dirty = 1;
	if (view->lines > 1)
		view->line[view->lines - 2].dirty = 1;
	draw_rev_graph(graph->prev);
	done_rev_graph(graph->prev->prev);
}


/*
 * Main view backend
 */

static const char *main_argv[SIZEOF_ARG] = {
	"git", "log", "--no-color", "--pretty=raw", "--parents",
		      "--topo-order", "%(head)", NULL
};

static bool
main_draw(struct view *view, struct line *line, unsigned int lineno)
{
	struct commit *commit = line->data;

	if (!commit->author)
		return FALSE;

	if (opt_date && draw_date(view, &commit->time))
		return TRUE;

	if (opt_author && draw_author(view, commit->author))
		return TRUE;

	if (opt_rev_graph && commit->graph_size &&
	    draw_graphic(view, LINE_MAIN_REVGRAPH, commit->graph, commit->graph_size))
		return TRUE;

	if (opt_show_refs && commit->refs) {
		size_t i;

		for (i = 0; i < commit->refs->size; i++) {
			struct ref *ref = commit->refs->refs[i];
			enum line_type type;

			if (ref->head)
				type = LINE_MAIN_HEAD;
			else if (ref->ltag)
				type = LINE_MAIN_LOCAL_TAG;
			else if (ref->tag)
				type = LINE_MAIN_TAG;
			else if (ref->tracked)
				type = LINE_MAIN_TRACKED;
			else if (ref->remote)
				type = LINE_MAIN_REMOTE;
			else
				type = LINE_MAIN_REF;

			if (draw_text(view, type, "[", TRUE) ||
			    draw_text(view, type, ref->name, TRUE) ||
			    draw_text(view, type, "]", TRUE))
				return TRUE;

			if (draw_text(view, LINE_DEFAULT, " ", TRUE))
				return TRUE;
		}
	}

	draw_text(view, LINE_DEFAULT, commit->title, TRUE);
	return TRUE;
}

/* Reads git log --pretty=raw output and parses it into the commit struct. */
static bool
main_read(struct view *view, char *line)
{
	static struct rev_graph *graph = graph_stacks;
	enum line_type type;
	struct commit *commit;

	if (!line) {
		int i;

		if (!view->lines && !view->prev)
			die("No revisions match the given arguments.");
		if (view->lines > 0) {
			commit = view->line[view->lines - 1].data;
			view->line[view->lines - 1].dirty = 1;
			if (!commit->author) {
				view->lines--;
				free(commit);
				graph->commit = NULL;
			}
		}
		update_rev_graph(view, graph);

		for (i = 0; i < ARRAY_SIZE(graph_stacks); i++)
			clear_rev_graph(&graph_stacks[i]);
		return TRUE;
	}

	type = get_line_type(line);
	if (type == LINE_COMMIT) {
		commit = calloc(1, sizeof(struct commit));
		if (!commit)
			return FALSE;

		line += STRING_SIZE("commit ");
		if (*line == '-') {
			graph->boundary = 1;
			line++;
		}

		string_copy_rev(commit->id, line);
		commit->refs = get_ref_list(commit->id);
		graph->commit = commit;
		add_line_data(view, commit, LINE_MAIN_COMMIT);

		while ((line = strchr(line, ' '))) {
			line++;
			push_rev_graph(graph->parents, line);
			commit->has_parents = TRUE;
		}
		return TRUE;
	}

	if (!view->lines)
		return TRUE;
	commit = view->line[view->lines - 1].data;

	switch (type) {
	case LINE_PARENT:
		if (commit->has_parents)
			break;
		push_rev_graph(graph->parents, line + STRING_SIZE("parent "));
		break;

	case LINE_AUTHOR:
		parse_author_line(line + STRING_SIZE("author "),
				  &commit->author, &commit->time);
		update_rev_graph(view, graph);
		graph = graph->next;
		break;

	default:
		/* Fill in the commit title if it has not already been set. */
		if (commit->title[0])
			break;

		/* Require titles to start with a non-space character at the
		 * offset used by git log. */
		if (strncmp(line, "    ", 4))
			break;
		line += 4;
		/* Well, if the title starts with a whitespace character,
		 * try to be forgiving.  Otherwise we end up with no title. */
		while (isspace(*line))
			line++;
		if (*line == '\0')
			break;
		/* FIXME: More graceful handling of titles; append "..." to
		 * shortened titles, etc. */

		string_expand(commit->title, sizeof(commit->title), line, 1);
		view->line[view->lines - 1].dirty = 1;
	}

	return TRUE;
}

static enum request
main_request(struct view *view, enum request request, struct line *line)
{
	enum open_flags flags = view_is_displayed(view) ? OPEN_SPLIT : OPEN_DEFAULT;

	switch (request) {
	case REQ_ENTER:
		open_view(view, REQ_VIEW_DIFF, flags);
		break;
	case REQ_REFRESH:
		load_refs();
		open_view(view, REQ_VIEW_MAIN, OPEN_REFRESH);
		break;
	default:
		return request;
	}

	return REQ_NONE;
}

static bool
grep_refs(struct ref_list *list, regex_t *regex)
{
	regmatch_t pmatch;
	size_t i;

	if (!opt_show_refs || !list)
		return FALSE;

	for (i = 0; i < list->size; i++) {
		if (regexec(regex, list->refs[i]->name, 1, &pmatch, 0) != REG_NOMATCH)
			return TRUE;
	}

	return FALSE;
}

static bool
main_grep(struct view *view, struct line *line)
{
	struct commit *commit = line->data;
	const char *text[] = {
		commit->title,
		opt_author ? commit->author : "",
		mkdate(&commit->time, opt_date),
		NULL
	};

	return grep_text(view, text) || grep_refs(commit->refs, view->regex);
}

static void
main_select(struct view *view, struct line *line)
{
	struct commit *commit = line->data;

	string_copy_rev(view->ref, commit->id);
	string_copy_rev(ref_commit, view->ref);
}

static struct view_ops main_ops = {
	"commit",
	main_argv,
	NULL,
	main_read,
	main_draw,
	main_request,
	main_grep,
	main_select,
};


/*
 * Status management
 */

/* Whether or not the curses interface has been initialized. */
static bool cursed = FALSE;

/* Terminal hacks and workarounds. */
static bool use_scroll_redrawwin;
static bool use_scroll_status_wclear;

/* The status window is used for polling keystrokes. */
static WINDOW *status_win;

/* Reading from the prompt? */
static bool input_mode = FALSE;

static bool status_empty = FALSE;

/* Update status and title window. */
static void
report(const char *msg, ...)
{
	struct view *view = display[current_view];

	if (input_mode)
		return;

	if (!view) {
		char buf[SIZEOF_STR];
		va_list args;

		va_start(args, msg);
		if (vsnprintf(buf, sizeof(buf), msg, args) >= sizeof(buf)) {
			buf[sizeof(buf) - 1] = 0;
			buf[sizeof(buf) - 2] = '.';
			buf[sizeof(buf) - 3] = '.';
			buf[sizeof(buf) - 4] = '.';
		}
		va_end(args);
		die("%s", buf);
	}

	if (!status_empty || *msg) {
		va_list args;

		va_start(args, msg);

		wmove(status_win, 0, 0);
		if (view->has_scrolled && use_scroll_status_wclear)
			wclear(status_win);
		if (*msg) {
			vwprintw(status_win, msg, args);
			status_empty = FALSE;
		} else {
			status_empty = TRUE;
		}
		wclrtoeol(status_win);
		wnoutrefresh(status_win);

		va_end(args);
	}

	update_view_title(view);
}

static void
init_display(void)
{
	const char *term;
	int x, y;

	/* Initialize the curses library */
	if (isatty(STDIN_FILENO)) {
		cursed = !!initscr();
		opt_tty = stdin;
	} else {
		/* Leave stdin and stdout alone when acting as a pager. */
		opt_tty = fopen("/dev/tty", "r+");
		if (!opt_tty)
			die("Failed to open /dev/tty");
		cursed = !!newterm(NULL, opt_tty, opt_tty);
	}

	if (!cursed)
		die("Failed to initialize curses");

	nonl();		/* Disable conversion and detect newlines from input. */
	cbreak();       /* Take input chars one at a time, no wait for \n */
	noecho();       /* Don't echo input */
	leaveok(stdscr, FALSE);

	if (has_colors())
		init_colors();

	getmaxyx(stdscr, y, x);
	status_win = newwin(1, 0, y - 1, 0);
	if (!status_win)
		die("Failed to create status window");

	/* Enable keyboard mapping */
	keypad(status_win, TRUE);
	wbkgdset(status_win, get_line_attr(LINE_STATUS));

	TABSIZE = opt_tab_size;

	term = getenv("XTERM_VERSION") ? NULL : getenv("COLORTERM");
	if (term && !strcmp(term, "gnome-terminal")) {
		/* In the gnome-terminal-emulator, the message from
		 * scrolling up one line when impossible followed by
		 * scrolling down one line causes corruption of the
		 * status line. This is fixed by calling wclear. */
		use_scroll_status_wclear = TRUE;
		use_scroll_redrawwin = FALSE;

	} else if (term && !strcmp(term, "xrvt-xpm")) {
		/* No problems with full optimizations in xrvt-(unicode)
		 * and aterm. */
		use_scroll_status_wclear = use_scroll_redrawwin = FALSE;

	} else {
		/* When scrolling in (u)xterm the last line in the
		 * scrolling direction will update slowly. */
		use_scroll_redrawwin = TRUE;
		use_scroll_status_wclear = FALSE;
	}
}

static int
get_input(int prompt_position)
{
	struct view *view;
	int i, key, cursor_y, cursor_x;
	bool loading = FALSE;

	if (prompt_position)
		input_mode = TRUE;

	while (TRUE) {
		foreach_view (view, i) {
			update_view(view);
			if (view_is_displayed(view) && view->has_scrolled &&
			    use_scroll_redrawwin)
				redrawwin(view->win);
			view->has_scrolled = FALSE;
			if (view->pipe)
				loading = TRUE;
		}

		/* Update the cursor position. */
		if (prompt_position) {
			getbegyx(status_win, cursor_y, cursor_x);
			cursor_x = prompt_position;
		} else {
			view = display[current_view];
			getbegyx(view->win, cursor_y, cursor_x);
			cursor_x = view->width - 1;
			cursor_y += view->lineno - view->offset;
		}
		setsyx(cursor_y, cursor_x);

		/* Refresh, accept single keystroke of input */
		doupdate();
		nodelay(status_win, loading);
		key = wgetch(status_win);

		/* wgetch() with nodelay() enabled returns ERR when
		 * there's no input. */
		if (key == ERR) {

		} else if (key == KEY_RESIZE) {
			int height, width;

			getmaxyx(stdscr, height, width);

			wresize(status_win, 1, width);
			mvwin(status_win, height - 1, 0);
			wnoutrefresh(status_win);
			resize_display();
			redraw_display(TRUE);

		} else {
			input_mode = FALSE;
			return key;
		}
	}
}

static char *
prompt_input(const char *prompt, input_handler handler, void *data)
{
	enum input_status status = INPUT_OK;
	static char buf[SIZEOF_STR];
	size_t pos = 0;

	buf[pos] = 0;

	while (status == INPUT_OK || status == INPUT_SKIP) {
		int key;

		mvwprintw(status_win, 0, 0, "%s%.*s", prompt, pos, buf);
		wclrtoeol(status_win);

		key = get_input(pos + 1);
		switch (key) {
		case KEY_RETURN:
		case KEY_ENTER:
		case '\n':
			status = pos ? INPUT_STOP : INPUT_CANCEL;
			break;

		case KEY_BACKSPACE:
			if (pos > 0)
				buf[--pos] = 0;
			else
				status = INPUT_CANCEL;
			break;

		case KEY_ESC:
			status = INPUT_CANCEL;
			break;

		default:
			if (pos >= sizeof(buf)) {
				report("Input string too long");
				return NULL;
			}

			status = handler(data, buf, key);
			if (status == INPUT_OK)
				buf[pos++] = (char) key;
		}
	}

	/* Clear the status window */
	status_empty = FALSE;
	report("");

	if (status == INPUT_CANCEL)
		return NULL;

	buf[pos++] = 0;

	return buf;
}

static enum input_status
prompt_yesno_handler(void *data, char *buf, int c)
{
	if (c == 'y' || c == 'Y')
		return INPUT_STOP;
	if (c == 'n' || c == 'N')
		return INPUT_CANCEL;
	return INPUT_SKIP;
}

static bool
prompt_yesno(const char *prompt)
{
	char prompt2[SIZEOF_STR];

	if (!string_format(prompt2, "%s [Yy/Nn]", prompt))
		return FALSE;

	return !!prompt_input(prompt2, prompt_yesno_handler, NULL);
}

static enum input_status
read_prompt_handler(void *data, char *buf, int c)
{
	return isprint(c) ? INPUT_OK : INPUT_SKIP;
}

static char *
read_prompt(const char *prompt)
{
	return prompt_input(prompt, read_prompt_handler, NULL);
}

static bool prompt_menu(const char *prompt, const struct menu_item *items, int *selected)
{
	enum input_status status = INPUT_OK;
	int size = 0;

	while (items[size].text)
		size++;

	while (status == INPUT_OK) {
		const struct menu_item *item = &items[*selected];
		int key;
		int i;

		mvwprintw(status_win, 0, 0, "%s (%d of %d) ",
			  prompt, *selected + 1, size);
		if (item->hotkey)
			wprintw(status_win, "[%c] ", (char) item->hotkey);
		wprintw(status_win, "%s", item->text);
		wclrtoeol(status_win);

		key = get_input(COLS - 1);
		switch (key) {
		case KEY_RETURN:
		case KEY_ENTER:
		case '\n':
			status = INPUT_STOP;
			break;

		case KEY_LEFT:
		case KEY_UP:
			*selected = *selected - 1;
			if (*selected < 0)
				*selected = size - 1;
			break;

		case KEY_RIGHT:
		case KEY_DOWN:
			*selected = (*selected + 1) % size;
			break;

		case KEY_ESC:
			status = INPUT_CANCEL;
			break;

		default:
			for (i = 0; items[i].text; i++)
				if (items[i].hotkey == key) {
					*selected = i;
					status = INPUT_STOP;
					break;
				}
		}
	}

	/* Clear the status window */
	status_empty = FALSE;
	report("");

	return status != INPUT_CANCEL;
}

/*
 * Repository properties
 */

static struct ref **refs = NULL;
static size_t refs_size = 0;
static struct ref *refs_head = NULL;

static struct ref_list **ref_lists = NULL;
static size_t ref_lists_size = 0;

DEFINE_ALLOCATOR(realloc_refs, struct ref *, 256)
DEFINE_ALLOCATOR(realloc_refs_list, struct ref *, 8)
DEFINE_ALLOCATOR(realloc_ref_lists, struct ref_list *, 8)

static int
compare_refs(const void *ref1_, const void *ref2_)
{
	const struct ref *ref1 = *(const struct ref **)ref1_;
	const struct ref *ref2 = *(const struct ref **)ref2_;

	if (ref1->tag != ref2->tag)
		return ref2->tag - ref1->tag;
	if (ref1->ltag != ref2->ltag)
		return ref2->ltag - ref2->ltag;
	if (ref1->head != ref2->head)
		return ref2->head - ref1->head;
	if (ref1->tracked != ref2->tracked)
		return ref2->tracked - ref1->tracked;
	if (ref1->remote != ref2->remote)
		return ref2->remote - ref1->remote;
	return strcmp(ref1->name, ref2->name);
}

static void
foreach_ref(bool (*visitor)(void *data, const struct ref *ref), void *data)
{
	size_t i;

	for (i = 0; i < refs_size; i++)
		if (!visitor(data, refs[i]))
			break;
}

static struct ref *
get_ref_head()
{
	return refs_head;
}

static struct ref_list *
get_ref_list(const char *id)
{
	struct ref_list *list;
	size_t i;

	for (i = 0; i < ref_lists_size; i++)
		if (!strcmp(id, ref_lists[i]->id))
			return ref_lists[i];

	if (!realloc_ref_lists(&ref_lists, ref_lists_size, 1))
		return NULL;
	list = calloc(1, sizeof(*list));
	if (!list)
		return NULL;

	for (i = 0; i < refs_size; i++) {
		if (!strcmp(id, refs[i]->id) &&
		    realloc_refs_list(&list->refs, list->size, 1))
			list->refs[list->size++] = refs[i];
	}

	if (!list->refs) {
		free(list);
		return NULL;
	}

	qsort(list->refs, list->size, sizeof(*list->refs), compare_refs);
	ref_lists[ref_lists_size++] = list;
	return list;
}

static int
read_ref(char *id, size_t idlen, char *name, size_t namelen)
{
	struct ref *ref = NULL;
	bool tag = FALSE;
	bool ltag = FALSE;
	bool remote = FALSE;
	bool tracked = FALSE;
	bool head = FALSE;
	int from = 0, to = refs_size - 1;

	if (!prefixcmp(name, "refs/tags/")) {
		if (!suffixcmp(name, namelen, "^{}")) {
			namelen -= 3;
			name[namelen] = 0;
		} else {
			ltag = TRUE;
		}

		tag = TRUE;
		namelen -= STRING_SIZE("refs/tags/");
		name	+= STRING_SIZE("refs/tags/");

	} else if (!prefixcmp(name, "refs/remotes/")) {
		remote = TRUE;
		namelen -= STRING_SIZE("refs/remotes/");
		name	+= STRING_SIZE("refs/remotes/");
		tracked  = !strcmp(opt_remote, name);

	} else if (!prefixcmp(name, "refs/heads/")) {
		namelen -= STRING_SIZE("refs/heads/");
		name	+= STRING_SIZE("refs/heads/");
		if (!strncmp(opt_head, name, namelen))
			return OK;

	} else if (!strcmp(name, "HEAD")) {
		head     = TRUE;
		if (*opt_head) {
			namelen  = strlen(opt_head);
			name	 = opt_head;
		}
	}

	/* If we are reloading or it's an annotated tag, replace the
	 * previous SHA1 with the resolved commit id; relies on the fact
	 * git-ls-remote lists the commit id of an annotated tag right
	 * before the commit id it points to. */
	while (from <= to) {
		size_t pos = (to + from) / 2;
		int cmp = strcmp(name, refs[pos]->name);

		if (!cmp) {
			ref = refs[pos];
			break;
		}

		if (cmp < 0)
			to = pos - 1;
		else
			from = pos + 1;
	}

	if (!ref) {
		if (!realloc_refs(&refs, refs_size, 1))
			return ERR;
		ref = calloc(1, sizeof(*ref) + namelen);
		if (!ref)
			return ERR;
		memmove(refs + from + 1, refs + from,
			(refs_size - from) * sizeof(*refs));
		refs[from] = ref;
		strncpy(ref->name, name, namelen);
		refs_size++;
	}

	ref->head = head;
	ref->tag = tag;
	ref->ltag = ltag;
	ref->remote = remote;
	ref->tracked = tracked;
	string_copy_rev(ref->id, id);

	if (head)
		refs_head = ref;
	return OK;
}

static int
load_refs(void)
{
	const char *head_argv[] = {
		"git", "symbolic-ref", "HEAD", NULL
	};
	static const char *ls_remote_argv[SIZEOF_ARG] = {
		"git", "ls-remote", opt_git_dir, NULL
	};
	static bool init = FALSE;
	size_t i;

	if (!init) {
		if (!argv_from_env(ls_remote_argv, "TIG_LS_REMOTE"))
			die("TIG_LS_REMOTE contains too many arguments");
		init = TRUE;
	}

	if (!*opt_git_dir)
		return OK;

	if (io_run_buf(head_argv, opt_head, sizeof(opt_head)) &&
	    !prefixcmp(opt_head, "refs/heads/")) {
		char *offset = opt_head + STRING_SIZE("refs/heads/");

		memmove(opt_head, offset, strlen(offset) + 1);
	}

	refs_head = NULL;
	for (i = 0; i < refs_size; i++)
		refs[i]->id[0] = 0;

	if (io_run_load(ls_remote_argv, "\t", read_ref) == ERR)
		return ERR;

	/* Update the ref lists to reflect changes. */
	for (i = 0; i < ref_lists_size; i++) {
		struct ref_list *list = ref_lists[i];
		size_t old, new;

		for (old = new = 0; old < list->size; old++)
			if (!strcmp(list->id, list->refs[old]->id))
				list->refs[new++] = list->refs[old];
		list->size = new;
	}

	return OK;
}

static void
set_remote_branch(const char *name, const char *value, size_t valuelen)
{
	if (!strcmp(name, ".remote")) {
		string_ncopy(opt_remote, value, valuelen);

	} else if (*opt_remote && !strcmp(name, ".merge")) {
		size_t from = strlen(opt_remote);

		if (!prefixcmp(value, "refs/heads/"))
			value += STRING_SIZE("refs/heads/");

		if (!string_format_from(opt_remote, &from, "/%s", value))
			opt_remote[0] = 0;
	}
}

static void
set_repo_config_option(char *name, char *value, int (*cmd)(int, const char **))
{
	const char *argv[SIZEOF_ARG] = { name, "=" };
	int argc = 1 + (cmd == option_set_command);
	int error = ERR;

	if (!argv_from_string(argv, &argc, value))
		config_msg = "Too many option arguments";
	else
		error = cmd(argc, argv);

	if (error == ERR)
		warn("Option 'tig.%s': %s", name, config_msg);
}

static bool
set_environment_variable(const char *name, const char *value)
{
	size_t len = strlen(name) + 1 + strlen(value) + 1;
	char *env = malloc(len);

	if (env &&
	    string_nformat(env, len, NULL, "%s=%s", name, value) &&
	    putenv(env) == 0)
		return TRUE;
	free(env);
	return FALSE;
}

static void
set_work_tree(const char *value)
{
	char cwd[SIZEOF_STR];

	if (!getcwd(cwd, sizeof(cwd)))
		die("Failed to get cwd path: %s", strerror(errno));
	if (chdir(opt_git_dir) < 0)
		die("Failed to chdir(%s): %s", strerror(errno));
	if (!getcwd(opt_git_dir, sizeof(opt_git_dir)))
		die("Failed to get git path: %s", strerror(errno));
	if (chdir(cwd) < 0)
		die("Failed to chdir(%s): %s", cwd, strerror(errno));
	if (chdir(value) < 0)
		die("Failed to chdir(%s): %s", value, strerror(errno));
	if (!getcwd(cwd, sizeof(cwd)))
		die("Failed to get cwd path: %s", strerror(errno));
	if (!set_environment_variable("GIT_WORK_TREE", cwd))
		die("Failed to set GIT_WORK_TREE to '%s'", cwd);
	if (!set_environment_variable("GIT_DIR", opt_git_dir))
		die("Failed to set GIT_DIR to '%s'", opt_git_dir);
	opt_is_inside_work_tree = TRUE;
}

static int
read_repo_config_option(char *name, size_t namelen, char *value, size_t valuelen)
{
	if (!strcmp(name, "i18n.commitencoding"))
		string_ncopy(opt_encoding, value, valuelen);

	else if (!strcmp(name, "core.editor"))
		string_ncopy(opt_editor, value, valuelen);

	else if (!strcmp(name, "core.worktree"))
		set_work_tree(value);

	else if (!prefixcmp(name, "tig.color."))
		set_repo_config_option(name + 10, value, option_color_command);

	else if (!prefixcmp(name, "tig.bind."))
		set_repo_config_option(name + 9, value, option_bind_command);

	else if (!prefixcmp(name, "tig."))
		set_repo_config_option(name + 4, value, option_set_command);

	else if (*opt_head && !prefixcmp(name, "branch.") &&
		 !strncmp(name + 7, opt_head, strlen(opt_head)))
		set_remote_branch(name + 7 + strlen(opt_head), value, valuelen);

	return OK;
}

static int
load_git_config(void)
{
	const char *config_list_argv[] = { "git", "config", "--list", NULL };

	return io_run_load(config_list_argv, "=", read_repo_config_option);
}

static int
read_repo_info(char *name, size_t namelen, char *value, size_t valuelen)
{
	if (!opt_git_dir[0]) {
		string_ncopy(opt_git_dir, name, namelen);

	} else if (opt_is_inside_work_tree == -1) {
		/* This can be 3 different values depending on the
		 * version of git being used. If git-rev-parse does not
		 * understand --is-inside-work-tree it will simply echo
		 * the option else either "true" or "false" is printed.
		 * Default to true for the unknown case. */
		opt_is_inside_work_tree = strcmp(name, "false") ? TRUE : FALSE;

	} else if (*name == '.') {
		string_ncopy(opt_cdup, name, namelen);

	} else {
		string_ncopy(opt_prefix, name, namelen);
	}

	return OK;
}

static int
load_repo_info(void)
{
	const char *rev_parse_argv[] = {
		"git", "rev-parse", "--git-dir", "--is-inside-work-tree",
			"--show-cdup", "--show-prefix", NULL
	};

	return io_run_load(rev_parse_argv, "=", read_repo_info);
}


/*
 * Main
 */

static const char usage[] =
"tig " TIG_VERSION " (" __DATE__ ")\n"
"\n"
"Usage: tig        [options] [revs] [--] [paths]\n"
"   or: tig show   [options] [revs] [--] [paths]\n"
"   or: tig blame  [rev] path\n"
"   or: tig status\n"
"   or: tig <      [git command output]\n"
"\n"
"Options:\n"
"  -v, --version   Show version and exit\n"
"  -h, --help      Show help message and exit";

static void __NORETURN
quit(int sig)
{
	/* XXX: Restore tty modes and let the OS cleanup the rest! */
	if (cursed)
		endwin();
	exit(0);
}

static void __NORETURN
die(const char *err, ...)
{
	va_list args;

	endwin();

	va_start(args, err);
	fputs("tig: ", stderr);
	vfprintf(stderr, err, args);
	fputs("\n", stderr);
	va_end(args);

	exit(1);
}

static void
warn(const char *msg, ...)
{
	va_list args;

	va_start(args, msg);
	fputs("tig warning: ", stderr);
	vfprintf(stderr, msg, args);
	fputs("\n", stderr);
	va_end(args);
}

static enum request
parse_options(int argc, const char *argv[])
{
	enum request request = REQ_VIEW_MAIN;
	const char *subcommand;
	bool seen_dashdash = FALSE;
	/* XXX: This is vulnerable to the user overriding options
	 * required for the main view parser. */
	const char *custom_argv[SIZEOF_ARG] = {
		"git", "log", "--no-color", "--pretty=raw", "--parents",
			"--topo-order", NULL
	};
	int i, j = 6;

	if (!isatty(STDIN_FILENO)) {
		io_open(&VIEW(REQ_VIEW_PAGER)->io, "");
		return REQ_VIEW_PAGER;
	}

	if (argc <= 1)
		return REQ_NONE;

	subcommand = argv[1];
	if (!strcmp(subcommand, "status")) {
		if (argc > 2)
			warn("ignoring arguments after `%s'", subcommand);
		return REQ_VIEW_STATUS;

	} else if (!strcmp(subcommand, "blame")) {
		if (argc <= 2 || argc > 4)
			die("invalid number of options to blame\n\n%s", usage);

		i = 2;
		if (argc == 4) {
			string_ncopy(opt_ref, argv[i], strlen(argv[i]));
			i++;
		}

		string_ncopy(opt_file, argv[i], strlen(argv[i]));
		return REQ_VIEW_BLAME;

	} else if (!strcmp(subcommand, "show")) {
		request = REQ_VIEW_DIFF;

	} else {
		subcommand = NULL;
	}

	if (subcommand) {
		custom_argv[1] = subcommand;
		j = 2;
	}

	for (i = 1 + !!subcommand; i < argc; i++) {
		const char *opt = argv[i];

		if (seen_dashdash || !strcmp(opt, "--")) {
			seen_dashdash = TRUE;

		} else if (!strcmp(opt, "-v") || !strcmp(opt, "--version")) {
			printf("tig version %s\n", TIG_VERSION);
			quit(0);

		} else if (!strcmp(opt, "-h") || !strcmp(opt, "--help")) {
			printf("%s\n", usage);
			quit(0);
		}

		custom_argv[j++] = opt;
		if (j >= ARRAY_SIZE(custom_argv))
			die("command too long");
	}

	if (!prepare_update(VIEW(request), custom_argv, NULL))
		die("Failed to format arguments");

	return request;
}

int
main(int argc, const char *argv[])
{
	const char *codeset = "UTF-8";
	enum request request = parse_options(argc, argv);
	struct view *view;
	size_t i;

	signal(SIGINT, quit);
	signal(SIGPIPE, SIG_IGN);

	if (setlocale(LC_ALL, "")) {
		codeset = nl_langinfo(CODESET);
	}

	if (load_repo_info() == ERR)
		die("Failed to load repo info.");

	if (load_options() == ERR)
		die("Failed to load user config.");

	if (load_git_config() == ERR)
		die("Failed to load repo config.");

	/* Require a git repository unless when running in pager mode. */
	if (!opt_git_dir[0] && request != REQ_VIEW_PAGER)
		die("Not a git repository");

	if (*opt_encoding && strcmp(codeset, "UTF-8")) {
		opt_iconv_in = iconv_open("UTF-8", opt_encoding);
		if (opt_iconv_in == ICONV_NONE)
			die("Failed to initialize character set conversion");
	}

	if (codeset && strcmp(codeset, "UTF-8")) {
		opt_iconv_out = iconv_open(codeset, "UTF-8");
		if (opt_iconv_out == ICONV_NONE)
			die("Failed to initialize character set conversion");
	}

	if (load_refs() == ERR)
		die("Failed to load refs.");

	foreach_view (view, i)
		if (!argv_from_env(view->ops->argv, view->cmd_env))
			die("Too many arguments in the `%s` environment variable",
			    view->cmd_env);

	init_display();

	if (request != REQ_NONE)
		open_view(NULL, request, OPEN_PREPARED);
	request = request == REQ_NONE ? REQ_VIEW_MAIN : REQ_NONE;

	while (view_driver(display[current_view], request)) {
		int key = get_input(0);

		view = display[current_view];
		request = get_keybinding(view->keymap, key);

		/* Some low-level request handling. This keeps access to
		 * status_win restricted. */
		switch (request) {
		case REQ_NONE:
			report("Unknown key, press %s for help",
			       get_key(view->keymap, REQ_VIEW_HELP));
			break;
		case REQ_PROMPT:
		{
			char *cmd = read_prompt(":");

			if (cmd && isdigit(*cmd)) {
				int lineno = view->lineno + 1;

				if (parse_int(&lineno, cmd, 1, view->lines + 1) == OK) {
					select_view_line(view, lineno - 1);
					report("");
				} else {
					report("Unable to parse '%s' as a line number", cmd);
				}

			} else if (cmd) {
				struct view *next = VIEW(REQ_VIEW_PAGER);
				const char *argv[SIZEOF_ARG] = { "git" };
				int argc = 1;

				/* When running random commands, initially show the
				 * command in the title. However, it maybe later be
				 * overwritten if a commit line is selected. */
				string_ncopy(next->ref, cmd, strlen(cmd));

				if (!argv_from_string(argv, &argc, cmd)) {
					report("Too many arguments");
				} else if (!prepare_update(next, argv, NULL)) {
					report("Failed to format command");
				} else {
					open_view(view, REQ_VIEW_PAGER, OPEN_PREPARED);
				}
			}

			request = REQ_NONE;
			break;
		}
		case REQ_SEARCH:
		case REQ_SEARCH_BACK:
		{
			const char *prompt = request == REQ_SEARCH ? "/" : "?";
			char *search = read_prompt(prompt);

			if (search)
				string_ncopy(opt_search, search, strlen(search));
			else if (*opt_search)
				request = request == REQ_SEARCH ?
					REQ_FIND_NEXT :
					REQ_FIND_PREV;
			else
				request = REQ_NONE;
			break;
		}
		default:
			break;
		}
	}

	quit(0);

	return 0;
}
