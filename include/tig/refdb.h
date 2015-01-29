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

#ifndef TIG_REFDB_H
#define TIG_REFDB_H

#include "tig/tig.h"
#include "tig/types.h"
#include "tig/util.h"

struct argv_env;

struct ref {
	struct ref *next;
	enum reference_type type;
	char id[SIZEOF_REV];	/* Commit SHA1 ID */
	unsigned int valid:1;	/* Is the ref still valid? */
	char name[1];		/* Ref name; tag or head names are shortened. */
};

#define is_initial_commit()	(!get_ref_head())
#define is_head_commit(rev)	(!strcmp((rev), "HEAD") || (get_ref_head() && !strncmp(rev, get_ref_head()->id, SIZEOF_REV - 1)))
#define ref_is_tag(ref)		((ref)->type == REFERENCE_TAG || (ref)->type == REFERENCE_LOCAL_TAG)
#define ref_is_remote(ref)	((ref)->type == REFERENCE_REMOTE || (ref)->type == REFERENCE_TRACKED_REMOTE)

#define foreach_ref_list(ref, id)	for (ref = get_ref_list(id); ref; ref = ref->next)

const struct ref *get_ref_head();
const struct ref *get_ref_list(const char *id);
const struct ref *get_canonical_ref(const char *id);
void foreach_ref(bool (*visitor)(void *data, const struct ref *ref), void *data);
int load_refs(bool force);
int add_ref(const char *id, char *name, const char *remote_name, const char *head);
int ref_compare(const struct ref *ref1, const struct ref *ref2);
void ref_update_env(struct argv_env *env, const struct ref *ref, bool clear);

struct ref_format {
	const char *start;
	const char *end;
};

const struct ref_format *get_ref_format(const struct ref *ref);
enum status_code parse_ref_formats(const char *argv[]);

#endif

/* vim: set ts=8 sw=8 noexpandtab: */
