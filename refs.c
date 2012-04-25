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

#include "tig.h"
#include "io.h"
#include "refs.h"

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
		return ref2->ltag - ref1->ltag;
	if (ref1->head != ref2->head)
		return ref2->head - ref1->head;
	if (ref1->tracked != ref2->tracked)
		return ref2->tracked - ref1->tracked;
	if (ref1->replace != ref2->replace)
		return ref2->replace - ref1->replace;
	/* Order remotes last. */
	if (ref1->remote != ref2->remote)
		return ref1->remote - ref2->remote;
	return strcmp(ref1->name, ref2->name);
}

void
foreach_ref(bool (*visitor)(void *data, const struct ref *ref), void *data)
{
	size_t i;

	for (i = 0; i < refs_size; i++)
		if (!visitor(data, refs[i]))
			break;
}

struct ref *
get_ref_head()
{
	return refs_head;
}

struct ref_list *
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

struct ref_opt {
	const char *remote;
	const char *head;
};

static int
read_ref(char *id, size_t idlen, char *name, size_t namelen, void *data)
{
	struct ref_opt *opt = data;
	struct ref *ref = NULL;
	bool tag = FALSE;
	bool ltag = FALSE;
	bool remote = FALSE;
	bool replace = FALSE;
	bool tracked = FALSE;
	bool head = FALSE;
	int pos;

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
		tracked  = !strcmp(opt->remote, name);

	} else if (!prefixcmp(name, "refs/replace/")) {
		replace = TRUE;
		id	= name + strlen("refs/replace/");
		idlen	= namelen - strlen("refs/replace/");
		name	= "replaced";
		namelen	= strlen(name);

	} else if (!prefixcmp(name, "refs/heads/")) {
		namelen -= STRING_SIZE("refs/heads/");
		name	+= STRING_SIZE("refs/heads/");
		head	 = strlen(opt->head) == namelen
			   && !strncmp(opt->head, name, namelen);

	} else if (!strcmp(name, "HEAD")) {
		return OK;
	}

	/* If we are reloading or it's an annotated tag, replace the
	 * previous SHA1 with the resolved commit id; relies on the fact
	 * git-ls-remote lists the commit id of an annotated tag right
	 * before the commit id it points to. */
	for (pos = 0; pos < refs_size; pos++) {
		int cmp = replace ? strcmp(id, refs[pos]->id) : strcmp(name, refs[pos]->name);

		if (!cmp) {
			ref = refs[pos];
			break;
		}
	}

	if (!ref) {
		if (!realloc_refs(&refs, refs_size, 1))
			return ERR;
		ref = calloc(1, sizeof(*ref) + namelen);
		if (!ref)
			return ERR;
		refs[refs_size++] = ref;
		strncpy(ref->name, name, namelen);
	}

	ref->head = head;
	ref->tag = tag;
	ref->ltag = ltag;
	ref->remote = remote;
	ref->replace = replace;
	ref->tracked = tracked;
	string_ncopy_do(ref->id, SIZEOF_REV, id, idlen);

	if (head)
		refs_head = ref;
	return OK;
}

int
reload_refs(const char *git_dir, const char *remote_name, char *head, size_t headlen)
{
	const char *head_argv[] = {
		"git", "symbolic-ref", "HEAD", NULL
	};
	const char *ls_remote_argv[SIZEOF_ARG] = {
		"git", "ls-remote", git_dir, NULL
	};
	static bool init = FALSE;
	struct ref_opt opt = { remote_name, head };
	size_t i;

	if (!init) {
		if (!argv_from_env(ls_remote_argv, "TIG_LS_REMOTE"))
			return ERR;
		init = TRUE;
	}

	if (!*git_dir)
		return OK;

	if (io_run_buf(head_argv, head, headlen) &&
	    !prefixcmp(head, "refs/heads/")) {
		char *offset = head + STRING_SIZE("refs/heads/");

		memmove(head, offset, strlen(offset) + 1);
	}

	refs_head = NULL;
	for (i = 0; i < refs_size; i++)
		refs[i]->id[0] = 0;

	if (io_run_load(ls_remote_argv, "\t", read_ref, &opt) == ERR)
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

	qsort(refs, refs_size, sizeof(*refs), compare_refs);

	return OK;
}
