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

#ifndef TIG_GRAPH_H
#define TIG_GRAPH_H

#include "compat/hashtab.h"

#define GRAPH_COLORS	7

struct graph_symbol {
	unsigned int color:8;

	unsigned int commit:1;
	unsigned int boundary:1;
	unsigned int initial:1;
	unsigned int merge:1;

	unsigned int continued_down:1;
	unsigned int continued_up:1;
	unsigned int continued_right:1;
	unsigned int continued_left:1;
	unsigned int continued_up_left:1;

	unsigned int parent_down:1;
	unsigned int parent_right:1;

	unsigned int below_commit:1;
	unsigned int flanked:1;
	unsigned int next_right:1;
	unsigned int matches_commit:1;

	unsigned int shift_left:1;
	unsigned int continue_shift:1;
	unsigned int below_shift:1;

	unsigned int new_column:1;
	unsigned int empty:1;
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

struct colors {
	htab_t id_map;
	size_t count[GRAPH_COLORS];
};

struct graph {
	struct graph_row row;
	struct graph_row parents;
	struct graph_row prev_row;
	struct graph_row next_row;
	size_t position;
	size_t prev_position;
	size_t expanded;
	const char *id;
	struct graph_canvas *canvas;
	struct colors colors;
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

/* vim: set ts=8 sw=8 noexpandtab: */
