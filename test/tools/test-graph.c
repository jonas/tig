/* Copyright (c) 2006-2022 Jonas Fonseca <jonas.fonseca@gmail.com>
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

#include "tig/tig.h"
#include "tig/util.h"
#include "tig/io.h"
#include "tig/graph.h"

#define USAGE \
"test-graph [--ascii]\n" \
"\n" \
"Example usage:\n" \
"	# git log --pretty=raw --parents | ./test-graph\n" \
"	# git log --pretty=raw --parents | ./test-graph --ascii"

struct commit {
	char id[SIZEOF_REV];
	struct graph_canvas canvas;
};

DEFINE_ALLOCATOR(realloc_commits, struct commit *, 8)

static const char *(*graph_fn)(const struct graph_symbol *);

static bool
print_symbol(void *__, const struct graph *graph, const struct graph_symbol *symbol, int color_id, bool first)
{
	const char *chars = graph_fn(symbol);

	printf("%s", chars + !!first);
	return false;
}

static void
print_commit(struct graph *graph, struct commit *commit, const char *title)
{
	graph->foreach_symbol(graph, &commit->canvas, print_symbol, NULL);
	printf(" %s\n", title);
}

int
main(int argc, const char *argv[])
{
	struct graph *graph;
	struct io io = {0};
	struct buffer buf;
	struct commit **commits = NULL;
	size_t ncommits = 0;
	struct commit *commit = NULL;
	bool is_boundary;

	if (isatty(STDIN_FILENO)) {
		die(USAGE);
	}

	if (!(graph = init_graph(GRAPH_DISPLAY_V2)))
		die("Failed to allocate graph");

	if (argc > 1 && !strcmp(argv[1], "--ascii"))
		graph_fn = graph->symbol_to_ascii;
	else
		graph_fn = graph->symbol_to_utf8;

	if (!io_open(&io, "%s", ""))
		die("IO");

	while (!io_eof(&io)) {
		for (; io_get(&io, &buf, '\n', true); ) {
			char *line = buf.data;

			if (!prefixcmp(line, "commit ")) {
				line += STRING_SIZE("commit ");
				is_boundary = *line == '-';

				if (is_boundary)
					line++;

				if (!realloc_commits(&commits, ncommits, 1))
					die("Commits");

				commit = calloc(1, sizeof(*commit));
				if (!commit)
					die("Commit");
				commits[ncommits++] = commit;
				string_copy_rev(commit->id, line);
				graph->add_commit(graph, &commit->canvas, commit->id, line, is_boundary);
				graph->render_parents(graph, &commit->canvas);

				if ((line = io_memchr(&buf, line, 0))) {
					print_commit(graph, commit, line);
					commit = NULL;
				}

			} else if (!prefixcmp(line, "    ")) {

				if (!commit)
					continue;

				print_commit(graph, commit, line + 4);

				commit = NULL;
			}
		}
	}

	return 0;
}

/* vim: set ts=8 sw=8 noexpandtab: */
