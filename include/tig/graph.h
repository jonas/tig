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

#ifndef TIG_GRAPH_H
#define TIG_GRAPH_H

#define GRAPH_COMMIT_COLOR	(-1)
#define GRAPH_COLORS		14

struct graph_symbol;

struct graph_canvas {
	size_t size;			/* The width of the graph array. */
	struct graph_symbol *symbols;	/* Symbols for this row. */
};

struct graph;

typedef bool (*graph_symbol_iterator_fn)(void *, const struct graph *graph, const struct graph_symbol *, int color_id, bool);
void graph_foreach_symbol(const struct graph *graph, const struct graph_canvas *canvas, graph_symbol_iterator_fn fn, void *data);

struct graph *init_graph(void);
void done_graph(struct graph *graph);
void done_graph_rendering(struct graph *graph);

bool graph_render_parents(struct graph *graph, struct graph_canvas *canvas);
bool graph_add_commit(struct graph *graph, struct graph_canvas *canvas,
		      const char *id, const char *parents, bool is_boundary);
bool graph_add_parent(struct graph *graph, const char *parent);

const char *graph_symbol_to_ascii(const struct graph_symbol *symbol);
const char *graph_symbol_to_utf8(const struct graph_symbol *symbol);
const chtype *graph_symbol_to_chtype(const struct graph_symbol *symbol);

#endif

/* vim: set ts=8 sw=8 noexpandtab: */
