/* Copyright (c) 2006-2017 Jonas Fonseca <jonas.fonseca@gmail.com>
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

#include "tig/registers.h"
#include "tig/argv.h"

bool
register_set(const char key, const char *value)
{
	unsigned int idx = register_key_to_index(key);

	if (!idx)
		return false;
	if (!argv_env.registers[idx])
		argv_env.registers[idx] = calloc(1, SIZEOF_STR);
	if (!argv_env.registers[idx])
		return false;

	string_ncopy_do(argv_env.registers[idx], SIZEOF_STR - 1, value, strlen(value));
	return true;
}

const char *
register_get(const char key)
{
	unsigned int idx = register_key_to_index(key);

	if (!idx)
		return NULL;

	return argv_env.registers[idx];
}

/* vim: set ts=8 sw=8 noexpandtab: */
