/* Copyright (c) 2006-2024 Jonas Fonseca <jonas.fonseca@gmail.com>
 * Copyright (c) 2013 Drew Northup <n1xim.email@gmail.com>
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

#ifndef TIG_COMPAT_H
#define TIG_COMPAT_H

#ifndef HAVE_CONFIG_H
/*
 * Enable inclusion of header files checked by configure.
 */
#define HAVE_STDINT_H
#define HAVE_STDLIB_H
#define HAVE_STRING_H
#define HAVE_SYS_TIME_H
#define HAVE_UNISTD_H
#define HAVE_WORDEXP_H
#endif

/*
 * XXX: Compatibility code must never be enabled by default.
 */

#ifdef NO_MKSTEMPS
#define mkstemps compat_mkstemps
int compat_mkstemps(char *pattern, int suffix_len);
#endif

#ifdef NO_SETENV
#define setenv compat_setenv
int compat_setenv(const char *name, const char *value, int replace);
#endif

#ifdef NO_STRNDUP
#include <stddef.h>
#define strndup compat_strndup
char *compat_strndup(const char *s, size_t n);
#endif

#ifdef NO_WORDEXP
#define wordexp compat_wordexp
#define wordfree compat_wordfree
#define WRDE_NOCMD 4
typedef struct
{
	char **we_wordv;
} wordexp_t;
int compat_wordexp (const char *words, wordexp_t *pwordexp, int flags);
void compat_wordfree (wordexp_t *pwordexp);
#else
#include <wordexp.h>
#endif

#endif

/* vim: set ts=8 sw=8 noexpandtab: */
