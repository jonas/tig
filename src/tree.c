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

#include "util.h"
#include "repo.h"
#include "io.h"
#include "parse.h"
#include "options.h"
#include "display.h"
#include "view.h"
#include "draw.h"

/* The top of the path stack. */
static struct view_history tree_view_history = { sizeof(char *) };

static void
pop_tree_stack_entry(struct position *position)
{
	char *path_position = NULL;

	pop_view_history_state(&tree_view_history, position, &path_position);
	path_position[0] = 0;
}

static void
push_tree_stack_entry(struct view *view, const char *name, struct position *position)
{
	size_t pathlen = strlen(view->env->directory);
	char *path_position = view->env->directory + pathlen;
	struct view_state *state = push_view_history_state(&tree_view_history, position, &path_position);

	if (!state)
		return;

	if (!string_format_from(view->env->directory, &pathlen, "%s/", name)) {
		pop_tree_stack_entry(NULL);
		return;
	}

	clear_position(position);
}

/* Parse output from git-ls-tree(1):
 *
 * 100644 blob 95925677ca47beb0b8cce7c0e0011bcc3f61470f  213045	tig.c
 */

#define SIZEOF_TREE_ATTR \
	STRING_SIZE("100644 blob f931e1d229c3e185caad4449bf5b66ed72462657\t")

#define SIZEOF_TREE_MODE \
	STRING_SIZE("100644 ")

#define TREE_ID_OFFSET \
	STRING_SIZE("100644 blob ")

#define tree_path_is_parent(path)	(!strcmp("..", (path)))

struct tree_entry {
	char id[SIZEOF_REV];
	char commit[SIZEOF_REV];
	mode_t mode;
	struct time time;		/* Date from the author ident. */
	const struct ident *author;	/* Author of the commit. */
	unsigned long size;
	char name[1];
};

struct tree_state {
	char commit[SIZEOF_REV];
	const struct ident *author;
	struct time author_time;
	int size_width;
	bool read_date;
};

static const char *
tree_path(const struct line *line)
{
	return ((struct tree_entry *) line->data)->name;
}

static int
tree_compare_entry(const struct line *line1, const struct line *line2)
{
	if (line1->type != line2->type)
		return line1->type == LINE_TREE_DIR ? -1 : 1;
	return strcmp(tree_path(line1), tree_path(line2));
}

static const enum sort_field tree_sort_fields[] = {
	ORDERBY_NAME, ORDERBY_DATE, ORDERBY_AUTHOR
};
static struct sort_state tree_sort_state = SORT_STATE(tree_sort_fields);

static int
tree_compare(const void *l1, const void *l2)
{
	const struct line *line1 = (const struct line *) l1;
	const struct line *line2 = (const struct line *) l2;
	const struct tree_entry *entry1 = ((const struct line *) l1)->data;
	const struct tree_entry *entry2 = ((const struct line *) l2)->data;

	if (line1->type == LINE_TREE_HEAD)
		return -1;
	if (line2->type == LINE_TREE_HEAD)
		return 1;

	switch (get_sort_field(tree_sort_state)) {
	case ORDERBY_DATE:
		return sort_order(tree_sort_state, timecmp(&entry1->time, &entry2->time));

	case ORDERBY_AUTHOR:
		return sort_order(tree_sort_state, ident_compare(entry1->author, entry2->author));

	case ORDERBY_NAME:
	default:
		return sort_order(tree_sort_state, tree_compare_entry(line1, line2));
	}
}


static struct line *
tree_entry(struct view *view, enum line_type type, const char *path,
	   const char *mode, const char *id, unsigned long size)
{
	bool custom = type == LINE_TREE_HEAD || tree_path_is_parent(path);
	struct tree_entry *entry;
	struct line *line = add_line_alloc(view, &entry, type, strlen(path), custom);

	if (!line)
		return NULL;

	strncpy(entry->name, path, strlen(path));
	if (mode)
		entry->mode = strtoul(mode, NULL, 8);
	if (id)
		string_copy_rev(entry->id, id);
	entry->size = size;

	return line;
}

static bool
tree_read_date(struct view *view, char *text, struct tree_state *state)
{
	if (!text && state->read_date) {
		state->read_date = FALSE;
		return TRUE;

	} else if (!text) {
		/* Find next entry to process */
		const char *log_file[] = {
			"git", "log", encoding_arg, "--no-color", "--pretty=raw",
				"--cc", "--raw", view->ops->id, "--", "%(directory)", NULL
		};

		if (!view->lines) {
			tree_entry(view, LINE_TREE_HEAD, view->env->directory, NULL, NULL, 0);
			tree_entry(view, LINE_TREE_DIR, "..", "040000", view->ref, 0);
			report("Tree is empty");
			return TRUE;
		}

		if (!begin_update(view, repo.cdup, log_file, OPEN_EXTRA)) {
			report("Failed to load tree data");
			return TRUE;
		}

		state->read_date = TRUE;
		return FALSE;

	} else if (*text == 'c' && get_line_type(text) == LINE_COMMIT) {
		string_copy_rev_from_commit_line(state->commit, text);

	} else if (*text == 'a' && get_line_type(text) == LINE_AUTHOR) {
		parse_author_line(text + STRING_SIZE("author "),
				  &state->author, &state->author_time);

	} else if (*text == ':') {
		char *pos;
		size_t annotated = 1;
		size_t i;

		pos = strrchr(text, '\t');
		if (!pos)
			return TRUE;
		text = pos + 1;
		if (*view->env->directory && !strncmp(text, view->env->directory, strlen(view->env->directory)))
			text += strlen(view->env->directory);
		pos = strchr(text, '/');
		if (pos)
			*pos = 0;

		for (i = 1; i < view->lines; i++) {
			struct line *line = &view->line[i];
			struct tree_entry *entry = line->data;

			annotated += !!entry->author;
			if (entry->author || strcmp(entry->name, text))
				continue;

			string_copy_rev(entry->commit, state->commit);
			entry->author = state->author;
			entry->time = state->author_time;
			line->dirty = 1;
			break;
		}

		if (annotated == view->lines)
			io_kill(view->pipe);
	}
	return TRUE;
}

static bool
tree_read(struct view *view, char *text)
{
	struct tree_state *state = view->private;
	struct tree_entry *data;
	struct line *entry, *line;
	enum line_type type;
	size_t textlen = text ? strlen(text) : 0;
	const char *attr_offset = text + SIZEOF_TREE_ATTR;
	char *path;
	size_t size;

	if (state->read_date || !text)
		return tree_read_date(view, text, state);

	if (textlen <= SIZEOF_TREE_ATTR)
		return FALSE;
	if (view->lines == 0 &&
	    !tree_entry(view, LINE_TREE_HEAD, view->env->directory, NULL, NULL, 0))
		return FALSE;

	size = parse_size(attr_offset, &state->size_width);
	path = strchr(attr_offset, '\t');
	if (!path)
		return FALSE;
	path++;

	/* Strip the path part ... */
	if (*view->env->directory) {
		size_t pathlen = strlen(path);
		size_t striplen = strlen(view->env->directory);

		if (pathlen > striplen)
			memmove(path, path + striplen,
				pathlen - striplen + 1);

		/* Insert "link" to parent directory. */
		if (view->lines == 1 &&
		    !tree_entry(view, LINE_TREE_DIR, "..", "040000", view->ref, 0))
			return FALSE;
	}

	type = text[SIZEOF_TREE_MODE] == 't' ? LINE_TREE_DIR : LINE_TREE_FILE;
	entry = tree_entry(view, type, path, text, text + TREE_ID_OFFSET, size);
	if (!entry)
		return FALSE;
	data = entry->data;

	/* Skip "Directory ..." and ".." line. */
	for (line = &view->line[1 + !!*view->env->directory]; line < entry; line++) {
		if (tree_compare_entry(line, entry) <= 0)
			continue;

		memmove(line + 1, line, (entry - line) * sizeof(*entry));

		line->data = data;
		line->type = type;
		for (; line <= entry; line++)
			line->dirty = line->cleareol = 1;
		return TRUE;
	}

	/* Move the current line to the first tree entry. */
	if (!check_position(&view->prev_pos) && !check_position(&view->pos))
		goto_view_line(view, 0, 1);

	return TRUE;
}

static bool
tree_draw(struct view *view, struct line *line, unsigned int lineno)
{
	struct tree_state *state = view->private;
	struct tree_entry *entry = line->data;

	if (line->type == LINE_TREE_HEAD) {
		if (draw_text(view, line->type, "Directory path /"))
			return TRUE;
	} else {
		if (draw_mode(view, entry->mode))
			return TRUE;

		if (draw_author(view, entry->author))
			return TRUE;

		if (draw_file_size(view, entry->size, state->size_width,
				   line->type != LINE_TREE_FILE))
			return TRUE;

		if (draw_date(view, &entry->time))
			return TRUE;

		if (draw_id(view, entry->commit))
			return TRUE;
	}

	draw_text(view, line->type, entry->name);
	return TRUE;
}

void
open_blob_editor(const char *id, const char *name, unsigned int lineno)
{
	const char *blob_argv[] = { "git", "cat-file", "blob", id, NULL };
	char file[SIZEOF_STR];
	int fd;

	if (!name)
		name = "unknown";

	if (!string_format(file, "%s/tigblob.XXXXXX.%s", get_temp_dir(), name)) {
		report("Temporary file name is too long");
		return;
	}

	fd = mkstemps(file, strlen(name) + 1);

	if (fd == -1)
		report("Failed to create temporary file");
	else if (!io_run_append(blob_argv, fd))
		report("Failed to save blob data to file");
	else
		open_editor(file, lineno);
	if (fd != -1)
		unlink(file);
}

static enum request
tree_request(struct view *view, enum request request, struct line *line)
{
	enum open_flags flags;
	struct tree_entry *entry = line->data;

	switch (request) {
	case REQ_VIEW_BLAME:
		if (line->type != LINE_TREE_FILE) {
			report("Blame only supported for files");
			return REQ_NONE;
		}

		string_copy(view->env->ref, view->vid);
		return request;

	case REQ_EDIT:
		if (line->type != LINE_TREE_FILE) {
			report("Edit only supported for files");
		} else if (!is_head_commit(view->vid)) {
			open_blob_editor(entry->id, entry->name, 0);
		} else {
			open_editor(view->env->file, 0);
		}
		return REQ_NONE;

	case REQ_TOGGLE_SORT_FIELD:
	case REQ_TOGGLE_SORT_ORDER:
		sort_view(view, request, &tree_sort_state, tree_compare);
		return REQ_NONE;

	case REQ_PARENT:
	case REQ_BACK:
		if (!*view->env->directory) {
			/* quit view if at top of tree */
			return REQ_VIEW_CLOSE;
		}
		/* fake 'cd  ..' */
		line = &view->line[1];
		break;

	case REQ_ENTER:
		break;

	default:
		return request;
	}

	/* Cleanup the stack if the tree view is at a different tree. */
	if (!*view->env->directory)
		reset_view_history(&tree_view_history);

	switch (line->type) {
	case LINE_TREE_DIR:
		/* Depending on whether it is a subdirectory or parent link
		 * mangle the path buffer. */
		if (line == &view->line[1] && *view->env->directory) {
			pop_tree_stack_entry(&view->pos);

		} else {
			const char *basename = tree_path(line);

			push_tree_stack_entry(view, basename, &view->pos);
		}

		/* Trees and subtrees share the same ID, so they are not not
		 * unique like blobs. */
		flags = OPEN_RELOAD;
		request = REQ_VIEW_TREE;
		break;

	case LINE_TREE_FILE:
		flags = view_is_displayed(view) ? OPEN_SPLIT : OPEN_DEFAULT;
		request = REQ_VIEW_BLOB;
		break;

	default:
		return REQ_NONE;
	}

	open_view(view, request, flags);

	return REQ_NONE;
}

static bool
tree_grep(struct view *view, struct line *line)
{
	struct tree_entry *entry = line->data;
	const char *text[] = {
		entry->name,
		mkauthor(entry->author, opt_author_width, opt_show_author),
		mkdate(&entry->time, opt_show_date),
		NULL
	};

	return grep_text(view, text);
}

static void
tree_select(struct view *view, struct line *line)
{
	struct tree_entry *entry = line->data;

	if (line->type == LINE_TREE_HEAD) {
		string_format(view->ref, "Files in /%s", view->env->directory);
		return;
	}

	if (line->type == LINE_TREE_DIR && tree_path_is_parent(entry->name)) {
		string_copy(view->ref, "Open parent directory");
		view->env->blob[0] = 0;
		return;
	}

	if (line->type == LINE_TREE_FILE) {
		string_copy_rev(view->env->blob, entry->id);
		string_format(view->env->file, "%s%s", view->env->directory, tree_path(line));
	}

	string_copy_rev(view->ref, entry->id);
}

static bool
tree_open(struct view *view, enum open_flags flags)
{
	static const char *tree_argv[] = {
		"git", "ls-tree", "-l", "%(commit)", "%(directory)", NULL
	};

	if (string_rev_is_null(view->env->commit)) {
		report("No tree exists for this commit");
		return FALSE;
	}

	if (view->lines == 0 && repo.prefix[0]) {
		char *pos = repo.prefix;

		while (pos && *pos) {
			char *end = strchr(pos, '/');

			if (end)
				*end = 0;
			push_tree_stack_entry(view, pos, &view->pos);
			pos = end;
			if (end) {
				*end = '/';
				pos++;
			}
		}

	} else if (strcmp(view->vid, view->ops->id)) {
		view->env->directory[0] = 0;
	}

	return begin_update(view, repo.cdup, tree_argv, flags);
}

struct view_ops tree_ops = {
	"file",
	{ "tree" },
	argv_env.commit,
	VIEW_SEND_CHILD_ENTER,
	sizeof(struct tree_state),
	tree_open,
	tree_read,
	tree_draw,
	tree_request,
	tree_grep,
	tree_select,
};

/* vim: set ts=8 sw=8 noexpandtab: */
