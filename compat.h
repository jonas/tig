/* Copyright (c) 2006-2013 Jonas Fonseca <fonseca@diku.dk>
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

#include "tig.h"

/* Needed for mkstemps workaround */
#include <stdint.h>

/*
 * Compatibility: no mkstemps()
 * Adapted from libiberty via Git
 */

#ifndef HAVE_MKSTEMPS
#define mkstemps tigmkstemps
#endif

int tigmkstemps(char *, int);
int tig_mkstemps_mode(char *pattern, int suffix_len, int mode);

#endif

/* vim: set ts=8 sw=8 noexpandtab: */
