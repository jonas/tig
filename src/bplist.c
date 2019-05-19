/* Copyright (c) 2019 Aurelien Aptel <aurelien.aptel@gmail.com>
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

#include "tig/util.h"
#include "tig/map.h"
#include "tig/repo.h"
#include "tig/io.h"
#include "tig/bplist.h"

/*
 * BP is short for BackPort. When you need to do a lot of backporting it
 * is useful to be able to mark/unmark and load/save lists of commits.
 *
 * Currently tig has one global bplist, but the implementation is generic
 * enough and works on bplist instances so that in the future we could
 * have multiple bplist.
 *
 * - bplist_init(): initialize a bplist
 * - bplist_read(): load a bplist from a file into an initialized bplist
 * - bplist_has_rev(): checks whether a bplist contains a commit rev
 * - bplist_add_rev(): adds a rev to a bplist if it is not already there
 * - bplist_rem_rev(): removes a rev from a bplist if it holds it
 * - bplist_toggle_rev(): adds/remove rev from a bplist
 * - bplist_write(): dump a bplist to a file
 *
 * A bplist file is a plain text file where each line is in the form of
 *
 *     <sha1>[ <text>]
 *
 * Lines who do not match this format will be added as-is and not be
 * considered as commit. When writing a bplist, commits are sorted by
 * commit date.
 *
 * Non-commit lines get a commit date of 0 and they end up at the top of
 * the line as a result.
 */

/*
 * key/value struct for the rev => line hashtable
 */
struct cval {
	char rev[SIZEOF_REV];
	struct line *line;
};

struct line {
	char *s;
	long cdate;
};

struct bplist {
	const char *fn;
	struct string_map commits; /* maps revs to line */
	struct line **lines;
	size_t nlines;
	size_t capacity;
};

/*
 * tig global bplist instance
 */

struct bplist global_bplist = {0};


/*
 * Helpers for string_map
 */

static const char *
commits_key(const void *v)
{
	return ((struct cval *)v)->rev;
}

static string_map_key_t
commits_hash(const void *v)
{
	return string_map_hash_helper(commits_key(v));
}

/*
 * Expand an abbrev rev to a full one
 */
static int
expand_rev(char *dst, const char *rev)
{
	const char *rev_argv[] = { "git", "rev-parse", rev, NULL };
	bool ok;

	ok = io_run_buf(rev_argv, dst, SIZEOF_REV, repo.cdup, true);

	if (!ok)
		die("io_run_buf <%s>", rev);
	return 0;
}

/*
 * Get commit title
 */
static char *
get_title(const char *fullrev)
{
	char buf[1024] = {0};
	char *eol;
	const char *argv[] = { "git", "log", "--oneline", "--format=%B",
			       "-n1", fullrev, NULL };

	if (!io_run_buf(argv, buf, sizeof(buf), repo.cdup, true))
		die("io_run_buf <%s>", fullrev);

	eol = strchr(buf, '\n');
	if (eol)
		*eol = 0;

	return strdup(buf);
}


/*
 * Get commit date
 */
static long
get_cdate(const char *fullrev)
{
	char buf[1024] = {0};
	char *eol;
	const char *argv[] = { "git", "show", "-s", "--format=%ct",
			       fullrev, NULL };

	if (!io_run_buf(argv, buf, sizeof(buf), repo.cdup, true))
		return 0;

	eol = strchr(buf, '\n');
	if (eol)
		*eol = 0;

	return (long)atol(buf);
}

/*
 * Helper for sorting struct lines array
 */
static int
line_cmp(const void *pa, const void *pb)
{
	struct line * const *a = pa;
	struct line * const *b = pb;

	return (*a)->cdate - (*b)->cdate;
}

static void
sort_lines(struct bplist *bpl)
{
	qsort(bpl->lines, bpl->nlines, sizeof(*bpl->lines), line_cmp);
}

static struct line *
_add_line(struct bplist *bpl, char *s, long cdate)
{
	struct line *line;

	line = calloc(1, sizeof(*line));
	if (!line)
		die("OOM");

	line->s = s;
	line->cdate = cdate;

	if (bpl->nlines >= bpl->capacity) {
		struct line **p;
		size_t newcapa = bpl->capacity + 20;
		p = realloc(bpl->lines, sizeof(*p)*newcapa);
		if (!p)
			die("OOM");
		bpl->lines = p;
		bpl->capacity = newcapa;
	}

	bpl->lines[bpl->nlines++] = line;
	return line;
}

const char *
bplist_get_fn(struct bplist *bpl)
{
	return bpl->fn;
}

void
bplist_set_fn(struct bplist *bpl, const char *fn)
{
	bpl->fn = strdup(fn);
	if (!bpl->fn)
		die("OOM");
}

/*
 * Add/remove a commit from a bpline. Returns true if the commit is
 * added, false if removed.
 */
bool
bplist_toggle_rev(struct bplist *bpl, const char *rev)
{
	if (bplist_has_rev(bpl, rev)) {
		bplist_rem_rev(bpl, rev);
		return false;
	} else {
		bplist_add_rev(bpl, rev, NULL);
		return true;
	}
}

/*
 * Checks if a commit is in a bplist
 */
bool
bplist_has_rev(struct bplist *bpl, const char *rev)
{
	return string_map_get(&bpl->commits, rev) != NULL;
}

/*
 * Adds a line to a bplist. If the line is a valid commit line the
 * commit is added to the bplist, otherwise it will just get appended
 * to the bplist lines
 */
void
bplist_add_line(struct bplist *bpl, const char *line)
{
	char rev[SIZEOF_REV] = {0};
	char full[SIZEOF_REV] = {0};
	const char *s = line;
	const char *beg, *end;
	size_t len;
	int rc;

	while (*s && isspace(*s))
		s++;

	if (!*s) {
		_add_line(bpl, strdup(line), 0);
		return;
	}

	beg = s;

	while (*s && isxdigit(*s))
		s++;

	end = s;
	len = end - beg;

	if (len < 5 || len > SIZEOF_REV-1) {
		_add_line(bpl, strdup(line), 0);
		return;
	}

	memcpy(rev, beg, len);
	rc = expand_rev(full, rev);
	if (rc) {
		_add_line(bpl, strdup(line), 0);
		return;
	}

	bplist_add_rev(bpl, full, line);
}

/*
 * Adds a commit to a bplist. If line is NULL, a commit line will be
 * generated and added the bplist lines
 */
int
bplist_add_rev(struct bplist *bpl, const char *rev, const char *sline)
{
	struct cval *kv;
	struct line *line;
	char *title;
	char *final;

	kv = string_map_get(&bpl->commits, rev);
	if (kv)
		return 0;


	line = calloc(1, sizeof(*line));
	if (!line)
		die("OOM");

	if (sline) {
		final = strdup(sline);
	} else {
		title = get_title(rev);
		if (!title)
			die("OOM");

		final = calloc(256, 1);
		if (!final)
			die("OOM");

		snprintf(final, 255, "%s %s", rev, title);
		free(title);
	}

	line = _add_line(bpl, final, get_cdate(rev));

	kv = calloc(1, sizeof(*kv));
	if (!kv)
		die("OOM");

	kv->line = line;
	memcpy(kv->rev, rev, SIZEOF_REV);
	if (!string_map_put(&bpl->commits, kv->rev, kv))
		die("string_map_put");

	return 0;
}

/*
 * Removes a commit and its respective line from the bplist
 */
void
bplist_rem_rev(struct bplist *bpl, const char *rev)
{
	struct cval *kv;
	struct line *line;
	size_t i;

	kv = string_map_remove(&bpl->commits, rev);
	if (!kv)
		return;

	for (i = 0; i < bpl->nlines; i++) {
		if (bpl->lines[i] == kv->line) {
			memmove(bpl->lines + i,
				bpl->lines + i+1,
				sizeof(*bpl->lines)*(bpl->nlines-i-1));
			bpl->lines[bpl->nlines-1] = NULL;
			bpl->nlines--;
			break;
		}
	}
	free(kv->line->s);
	kv->line->s = NULL;
	free(kv->line);
	kv->line = NULL;
	free(kv);
}

/*
 * Initialize a bplist. Stores fn as potential file to read/write the
 * bplist but doesn't actually touch it yet.
 */
void
bplist_init(struct bplist *bpl, size_t capacity, const char *fn)
{
	memset(bpl, 0, sizeof(*bpl));
	bpl->fn = fn ? strdup(fn) : NULL;
	bpl->commits = (struct string_map){
		commits_hash,
		commits_key,
		128,
	};
	bpl->capacity = capacity;
	bpl->lines = calloc(bpl->capacity, sizeof(*bpl->lines));
}

/*
 * Load a bplist from a file
 */
int
bplist_read(struct bplist *bpl, const char *fn)
{
	FILE *fh;
	char linebuf[2048] = {0};
	int rc = 0;

	fh = fopen(fn, "r");
	if (!fh) {
		rc = errno;
		errno = 0;
		return rc;
	}

	while (1) {
		char *s;
		s = fgets(linebuf, sizeof(linebuf), fh);
		if (!s && feof(fh)) {
			break;
		}
		bplist_add_line(bpl, s);
	}
	fclose(fh);

	bpl->fn = strdup(fn);
	if (!bpl->fn)
		die("OOM");
	return 0;
}

/*
 * Sort the bplist lines by commit date and dump them to a file
 */
int
bplist_write(struct bplist *bpl, const char *fn)
{
	FILE *fh;
	size_t i;
	int rc;

	fh = fopen(fn ? fn : bpl->fn, "w+");
	if (!fh) {
		rc = errno;
		errno = 0;
		return rc;
	}

	sort_lines(bpl);

	for (i = 0; i < bpl->nlines; i++) {
		const char *s;
		size_t len;

		s = bpl->lines[i]->s;
		s = s ? s : "";
		len = strlen(s);
		fprintf(fh, len > 0 && s[len-1] == '\n' ? "%s" : "%s\n", s);
	}

	rc = fclose(fh);
	if (rc) {
		rc = errno;
		errno = 0;
		return rc;
	}

	return 0;
}

/*
 * Module init function
 */
void
init_bplist(void)
{
	bplist_init(&global_bplist, 10, NULL);
}
