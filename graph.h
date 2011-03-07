
/* Copyright (c) 2006-2010 Jonas Fonseca <fonseca@diku.dk>
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

#ifndef TIG_GRAPH_H
#define TIG_GRAPH_H

#define GRAPH_COLORS	7

struct graph_symbol {
	unsigned int color:8;
	unsigned int bold:1;

	unsigned int commit:1;
	unsigned int branch:1;

	unsigned int boundary:1;
	unsigned int initial:1;
	unsigned int merge:1;

	unsigned int vbranch:1;
	unsigned int branched:1;
};

struct graph_canvas {
	size_t size;			/* The width of the graph array. */
	struct graph_symbol *symbols;	/* Symbols for this row. */
};

struct graph_column {
	struct graph_symbol symbol;
	char id[SIZEOF_REV];		/* Parent SHA1 ID. */
};

struct graph_row {
	size_t size;
	struct graph_column *columns;
};

struct graph {
	struct graph_row row;
	struct graph_row parents;
	size_t position;
	size_t expanded;
	const char *id;
	struct graph_canvas *canvas;
	size_t colors[GRAPH_COLORS];
	bool has_parents;
	bool is_boundary;
};

void done_graph(struct graph *graph);

bool graph_render_parents(struct graph *graph);
bool graph_add_commit(struct graph *graph, struct graph_canvas *canvas,
		      const char *id, const char *parents, bool is_boundary);
struct graph_column *graph_add_parent(struct graph *graph, const char *parent);

const char *graph_symbol_to_ascii(struct graph_symbol *symbol);
const char *graph_symbol_to_utf8(struct graph_symbol *symbol);
const chtype *graph_symbol_to_chtype(struct graph_symbol *symbol);

#endif
