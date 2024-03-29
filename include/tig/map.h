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

#ifndef TIG_MAP_H
#define TIG_MAP_H

#include "tig/tig.h"

/*
 * String map.
 */

typedef unsigned int string_map_key_t;
typedef string_map_key_t (*string_map_hash_fn)(const void *value);
typedef const char *(*string_map_key_fn)(const void *value);
typedef bool (*string_map_iterator_fn)(void *data, void *value);

struct string_map {
	string_map_hash_fn hash_fn;
	string_map_key_fn key_fn;
	size_t init_size;
	void *htab;
	const char *key;
};

extern string_map_hash_fn string_map_hash_helper;
void *string_map_get(struct string_map *map, const char *key);
void **string_map_get_at(struct string_map *map, const char *key);
void *string_map_put(struct string_map *map, const char *key, void *value);
void **string_map_put_to(struct string_map *map, const char *key);
void *string_map_remove(struct string_map *map, const char *key);
void string_map_clear(struct string_map *map);
void string_map_foreach(struct string_map *map, string_map_iterator_fn iterator, void *data);

#define DEFINE_STRING_MAP(name, type, key_member, init_size) \
	static const char * \
	name ## _key(const void *value) \
	{ \
		return ((type) value)->key_member; \
	} \
	static string_map_key_t \
	name ## _hash(const void *value) \
	{ \
		return string_map_hash_helper(name ## _key(value)); \
	} \
	static struct string_map name = { \
		name ## _hash, \
		name ## _key, \
		init_size, \
	};

#endif
/* vim: set ts=8 sw=8 noexpandtab: */
