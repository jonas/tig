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
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/file.h>
#include <time.h>
#include <fcntl.h>
#include <libgen.h>
#include <termios.h>

#include <regex.h>

#include <locale.h>
#include <langinfo.h>
#include <iconv.h>

#ifdef HAVE_EXECINFO_H
#include <execinfo.h>
#endif

#ifdef HAVE_PATHS_H
#include <paths.h>
#endif

/* ncurses(3): Must be defined to have extended wide-character functions. */
#define _XOPEN_SOURCE_EXTENDED 1

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

#ifdef TRUE
#undef TRUE
#endif
#ifdef FALSE
#undef FALSE
#endif

#ifndef newscr
/* `newscr` is a ncurses-ism, and doesn't exist in netbsd-curses. (#790) */
#define newscr curscr
#endif

#if __GNUC__ >= 3
#define TIG_NORETURN __attribute__((__noreturn__))
#define PRINTF_LIKE(fmt, args) __attribute__((format (printf, fmt, args)))
#else
#define TIG_NORETURN
#define PRINTF_LIKE(fmt, args)
#endif

#include "tig/string.h"

#define ABS(x)		((x) >= 0  ? (x) : -(x))
#define MIN(x, y)	((x) < (y) ? (x) :  (y))
#define MAX(x, y)	((x) > (y) ? (x) :  (y))

#define ARRAY_SIZE(x)	(sizeof(x) / sizeof(x[0]))
#define STRING_SIZE(x)	(sizeof(x) - 1)

#define SIZEOF_STR	1024	/* Default string size. */
#define SIZEOF_MED_STR	8192	/* Medium string size. */
#define SIZEOF_REF	256	/* Size of symbolic or SHA1 ID. */
#define SIZEOF_REV	41	/* Holds a SHA-1 and an ending NUL. */

/* This color name can be used to refer to the default term colors. */
#define COLOR_DEFAULT	(-1)

#define ICONV_NONE	((iconv_t) -1)
#ifndef ICONV_CONST
#define ICONV_CONST	/* nothing */
#endif
#define ICONV_TRANSLIT	"//TRANSLIT"

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
 * Global view definition.
 */

#define VIEW_INFO(_) \
	_(MAIN,   main), \
	_(DIFF,   diff), \
	_(LOG,    log), \
	_(REFLOG, reflog), \
	_(TREE,   tree), \
	_(BLOB,   blob), \
	_(BLAME,  blame), \
	_(REFS,   refs), \
	_(STATUS, status), \
	_(STAGE,  stage), \
	_(STASH,  stash), \
	_(GREP,   grep), \
	_(PAGER,  pager), \
	_(HELP,   help)

#endif

/* vim: set ts=8 sw=8 noexpandtab: */
