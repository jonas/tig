/* Copyright (c) 2006-2012 Jonas Fonseca <fonseca@diku.dk>
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

#ifndef TIG_REFS_H
#define TIG_REFS_H

#include "tig.h"

struct ref {
	char id[SIZEOF_REV];	/* Commit SHA1 ID */
	unsigned int head:1;	/* Is it the current HEAD? */
	unsigned int tag:1;	/* Is it a tag? */
	unsigned int ltag:1;	/* If so, is the tag local? */
	unsigned int remote:1;	/* Is it a remote ref? */
	unsigned int replace:1;	/* Is it a replace ref? */
	unsigned int tracked:1;	/* Is it the remote for the current HEAD? */
	char name[1];		/* Ref name; tag or head names are shortened. */
};

struct ref_list {
	char id[SIZEOF_REV];	/* Commit SHA1 ID */
	size_t size;		/* Number of refs. */
	struct ref **refs;	/* References for this ID. */
};

struct ref *get_ref_head();
struct ref_list *get_ref_list(const char *id);
void foreach_ref(bool (*visitor)(void *data, const struct ref *ref), void *data);
int reload_refs(const char *git_dir, const char *remote_name, char *head, size_t headlen);

#endif
