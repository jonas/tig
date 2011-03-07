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

#include "tig.h"
#include "graph.h"

DEFINE_ALLOCATOR(realloc_graph_columns, struct graph_column, 32)
DEFINE_ALLOCATOR(realloc_graph_symbols, struct graph_symbol, 1)

static size_t get_free_graph_color(struct graph *graph)
{
	size_t i, free_color;

	for (free_color = i = 0; i < ARRAY_SIZE(graph->colors); i++) {
		if (graph->colors[i] < graph->colors[free_color])
			free_color = i;
	}

	graph->colors[free_color]++;
	return free_color;
}

void
done_graph(struct graph *graph)
{
	free(graph->row.columns);
	free(graph->parents.columns);
	memset(graph, 0, sizeof(*graph));
}

#define graph_column_has_commit(col) ((col)->id[0])

static size_t
graph_find_column_by_id(struct graph_row *row, const char *id)
{
	size_t free_column = row->size;
	size_t i;

	for (i = 0; i < row->size; i++) {
		if (!graph_column_has_commit(&row->columns[i]))
			free_column = i;
		else if (!strcmp(row->columns[i].id, id))
			return i;
	}

	return free_column;
}

static struct graph_column *
graph_insert_column(struct graph *graph, struct graph_row *row, size_t pos, const char *id)
{
	struct graph_column *column;

	if (!realloc_graph_columns(&row->columns, row->size, 1))
		return NULL;

	column = &row->columns[pos];
	if (pos < row->size) {
		memmove(column + 1, column, sizeof(*column) * (row->size - pos));
	}

	row->size++;
	memset(column, 0, sizeof(*column));
	string_copy_rev(column->id, id);
	column->symbol.boundary = !!graph->is_boundary;

	return column;
}

struct graph_column *
graph_add_parent(struct graph *graph, const char *parent)
{
	return graph_insert_column(graph, &graph->parents, graph->parents.size, parent);
}

static bool
graph_needs_expansion(struct graph *graph)
{
	if (graph->position + graph->parents.size > graph->row.size)
		return TRUE;
	return graph->parents.size > 1
	    && graph->position < 0
	    && graph->expanded < graph->parents.size;
}

static bool
graph_expand(struct graph *graph)
{
	while (graph_needs_expansion(graph)) {
		if (!graph_insert_column(graph, &graph->row, graph->position + graph->expanded, ""))
			return FALSE;
		graph->expanded++;
	}

	return TRUE;
}

static bool
graph_needs_collapsing(struct graph *graph)
{
	return graph->row.size > 1
	    && !graph_column_has_commit(&graph->row.columns[graph->row.size - 1]);
}

static bool
graph_collapse(struct graph *graph)
{
	while (graph_needs_collapsing(graph)) {
		graph->row.size--;
	}

	return TRUE;
}

static void
graph_reorder_parents(struct graph *graph)
{
	struct graph_row *row = &graph->row;
	struct graph_row *parents = &graph->parents;
	int i;

	if (parents->size == 1)
		return;

	for (i = 0; i < parents->size; i++) {
		struct graph_column *column = &parents->columns[i];
		size_t match = graph_find_column_by_id(row, column->id);

		if (match < graph->position && graph_column_has_commit(&row->columns[match])) {
			//die("Reorder: %s -> %s", graph->commit->id, column->id);
//			row->columns[match].symbol.initial = 1;
		}
	}
}

static void
graph_canvas_append_symbol(struct graph *graph, struct graph_symbol *symbol)
{
	struct graph_canvas *canvas = graph->canvas;

	if (realloc_graph_symbols(&canvas->symbols, canvas->size, 1))
		canvas->symbols[canvas->size++] = *symbol;
}

static bool
graph_insert_parents(struct graph *graph)
{
	struct graph_row *row = &graph->row;
	struct graph_row *parents = &graph->parents;
	size_t orig_size = row->size;
	bool branched = FALSE;
	bool merge = parents->size > 1;
	int pos;

	assert(!graph_needs_expansion(graph));

	for (pos = 0; pos < graph->position; pos++) {
		struct graph_column *column = &row->columns[pos];
		struct graph_symbol symbol = column->symbol;

		if (graph_column_has_commit(column)) {
			size_t match = graph_find_column_by_id(parents, column->id);

			if (match < parents->size) {
				column->symbol.initial = 1;
			}

			symbol.branch = 1;
		}
		symbol.vbranch = !!branched;
		if (!strcmp(column->id, graph->id)) {
			branched = TRUE;
			column->id[0] = 0;
		}

		graph_canvas_append_symbol(graph, &symbol);
	}

	for (; pos < graph->position + parents->size; pos++) {
		struct graph_column *old = &row->columns[pos];
		struct graph_column *new = &parents->columns[pos - graph->position];
		struct graph_symbol symbol = old->symbol;

		symbol.merge = !!merge;

		if (pos == graph->position) {
			symbol.commit = 1;
			/*
			if (new->symbol->boundary) {
				symbol.boundary = 1;
			} else*/
			if (!graph_column_has_commit(new)) {
				symbol.initial = 1;
			}

		} else if (!strcmp(old->id, new->id) && orig_size == row->size) {
			symbol.vbranch = 1;
			symbol.branch = 1;
			//symbol.merge = 1;

		} else if (parents->size > 1) {
			symbol.merge = 1;
			symbol.vbranch = !(pos == graph->position + parents->size - 1);

		} else if (graph_column_has_commit(old)) {
			symbol.branch = 1;
		}

		graph_canvas_append_symbol(graph, &symbol);
		if (!graph_column_has_commit(old))
			new->symbol.color = get_free_graph_color(graph);
		*old = *new;
	}

	for (; pos < row->size; pos++) {
		bool too = !strcmp(row->columns[row->size - 1].id, graph->id);
		struct graph_symbol symbol = row->columns[pos].symbol;

		symbol.vbranch = !!too;
		if (row->columns[pos].id[0]) {
			symbol.branch = 1;
			if (!strcmp(row->columns[pos].id, graph->id)) {
				symbol.branched = 1;
				if (too && pos != row->size - 1) {
					symbol.vbranch = 1;
				} else {
					symbol.vbranch = 0;
				}
				row->columns[pos].id[0] = 0;
			}
		}
		graph_canvas_append_symbol(graph, &symbol);
	}

	graph->parents.size = graph->expanded = graph->position = 0;

	return TRUE;
}

bool
graph_render_parents(struct graph *graph)
{
	if (!graph_expand(graph))
		return FALSE;
	graph_reorder_parents(graph);
	graph_insert_parents(graph);
	if (!graph_collapse(graph))
		return FALSE;

	return TRUE;
}

bool
graph_add_commit(struct graph *graph, struct graph_canvas *canvas,
		 const char *id, const char *parents, bool is_boundary)
{
	graph->position = graph_find_column_by_id(&graph->row, id);
	graph->id = id;
	graph->canvas = canvas;
	graph->is_boundary = is_boundary;

	while ((parents = strchr(parents, ' '))) {
		parents++;
		if (!graph_add_parent(graph, parents))
			return FALSE;
		graph->has_parents = TRUE;
	}

	if (graph->parents.size == 0 &&
	    !graph_add_parent(graph, ""))
		return FALSE;

	return TRUE;
}

const char *
graph_symbol_to_utf8(struct graph_symbol *symbol)
{
	if (symbol->commit) {
		if (symbol->boundary)
			return " ◯";
		else if (symbol->initial)
			return " ◎";
		else if (symbol->merge)
			return " ●";
		return " ●";
	}

	if (symbol->merge) {
		if (symbol->branch) {
			return "━┪";
		}
		if (symbol->vbranch)
			return "━┯";
		return "━┑";
	}

	if (symbol->branch) {
		if (symbol->branched) {
			if (symbol->vbranch)
				return "─┴";
			return "─┘";
		}
		if (symbol->vbranch)
			return "─│";
		return " │";
	}

	if (symbol->vbranch)
		return "──";

	return "  ";
}

const chtype *
graph_symbol_to_chtype(struct graph_symbol *symbol)
{
	static chtype graphics[2];

	if (symbol->commit) {
		graphics[0] = ' ';
		if (symbol->boundary)
			graphics[1] = 'o';
		else if (symbol->initial)
			graphics[1] = 'I';
		else if (symbol->merge)
			graphics[1] = 'M';
		else
			graphics[1] = 'o'; //ACS_DIAMOND; //'*';
		return graphics;
	}

	if (symbol->merge) {
		graphics[0] = ACS_HLINE;
		if (symbol->branch)
			graphics[1] = ACS_RTEE;
		else
			graphics[1] = ACS_URCORNER;
		return graphics;
	}

	if (symbol->branch) {
		graphics[0] = ACS_HLINE;
		if (symbol->branched) {
			if (symbol->vbranch)
				graphics[1] = ACS_BTEE;
			else
				graphics[1] = ACS_LRCORNER;
			return graphics;
		}

		if (!symbol->vbranch)
			graphics[0] = ' ';
		graphics[1] = ACS_VLINE;
		return graphics;
	}

	if (symbol->vbranch) {
		graphics[0] = graphics[1] = ACS_HLINE;
	} else
		graphics[0] = graphics[1] = ' ';

	return graphics;
}

const char *
graph_symbol_to_ascii(struct graph_symbol *symbol)
{
	if (symbol->commit) {
		if (symbol->boundary)
			return " o";
		else if (symbol->initial)
			return " I";
		else if (symbol->merge)
			return " M";
		return " *";
	}

	if (symbol->merge) {
		if (symbol->branch)
			return "-+";
		return "-.";
	}

	if (symbol->branch) {
		if (symbol->branched) {
			if (symbol->vbranch)
				return "-+";
			return "-'";
		}
		if (symbol->vbranch)
			return "-|";
		return " |";
	}

	if (symbol->vbranch)
		return "--";

	return "  ";
}
