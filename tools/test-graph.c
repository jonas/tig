/* Copyright (c) 2006-2013 Jonas Fonseca <fonseca@diku.dk>
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

#include "../tig.h"
#include "../util.h"
#include "../io.h"
#include "../graph.h"

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

int
main(int argc, const char *argv[])
{
	struct graph graph = { };
	struct io io = { };
	char *line;
	struct commit **commits = NULL;
	size_t ncommits = 0;
	struct commit *commit = NULL;
	bool is_boundary;
	const char *(*graph_fn)(struct graph_symbol *) = graph_symbol_to_utf8;

	if (argc > 1 && !strcmp(argv[1], "--ascii"))
		graph_fn = graph_symbol_to_ascii;

	if (isatty(STDIN_FILENO)) {
		die(USAGE);
	}

	if (!io_open(&io, "%s", ""))
		die("IO");

	while (!io_eof(&io)) {
		bool can_read = io_can_read(&io, TRUE);

		for (; (line = io_get(&io, '\n', can_read)); can_read = FALSE) {
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
				graph_add_commit(&graph, &commit->canvas, commit->id, line, is_boundary);
				graph_render_parents(&graph);

			} else if (!prefixcmp(line, "    ")) {
				int i;

				if (!commit)
					continue;

				for (i = 0; i < commit->canvas.size; i++) {
					struct graph_symbol *symbol = &commit->canvas.symbols[i];
					const char *chars = graph_fn(symbol);

					printf("%s", chars + (i == 0));
				}
				printf("%s\n", line + 3);

				commit = NULL;
			}
		}
	}

	return 0;
}

/* vim: set ts=8 sw=8 noexpandtab: */
