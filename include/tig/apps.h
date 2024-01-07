/* Copyright (c) 2006-2024 Jonas Fonseca <jonas.fonseca@gmail.com>
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

#ifndef TIG_APPS_H
#define TIG_APPS_H

#include "tig/tig.h"
#include "tig/argv.h"
#include "tig/util.h"

/*
 * general
 */

struct app_external {
	const char *argv[SIZEOF_ARG];
	char * const env[SIZEOF_ARG];
};

/*
 * diff-highlight
 */

struct app_external *app_diff_highlight_load(const char *query);

#endif

/* vim: set ts=8 sw=8 noexpandtab: */
