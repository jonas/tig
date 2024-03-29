/* Copyright (c) 2006-2024 Jonas Fonseca <jonas.fonseca@gmail.com>
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

#include "tig/types.h"

#define GRAPH_COMMIT_COLOR	(-1)
#define GRAPH_COLORS		14

struct graph;
struct graph_symbol;

struct graph_canvas {
	size_t size;			/* The width of the graph array. */
	struct graph_symbol *symbols;	/* Symbols for this row. */
};

typedef bool (*graph_symbol_iterator_fn)(void *, const struct graph *graph, const struct graph_symbol *, int color_id, bool);

struct graph {
	void *private;

	void (*done)(struct graph *graph);
	void (*done_rendering)(struct graph *graph);

	bool (*add_commit)(struct graph *graph, struct graph_canvas *canvas,
			   const char *id, const char *parents, bool is_boundary);
	bool (*add_parent)(struct graph *graph, const char *parent);
	bool (*render_parents)(struct graph *graph, struct graph_canvas *canvas);
	bool (*is_merge)(struct graph_canvas *canvas);

	void (*foreach_symbol)(const struct graph *graph, const struct graph_canvas *canvas, graph_symbol_iterator_fn fn, void *data);

	const char *(*symbol_to_ascii)(const struct graph_symbol *symbol);
	const char *(*symbol_to_utf8)(const struct graph_symbol *symbol);
	const chtype *(*symbol_to_chtype)(const struct graph_symbol *symbol);
};

struct graph *init_graph(enum graph_display display);

#endif

/* vim: set ts=8 sw=8 noexpandtab: */
