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

#include "tig/tig.h"
#include "tig/util.h"
#include "tig/graph.h"

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

struct graph_column {
	struct graph_symbol symbol;
	char id[SIZEOF_REV];		/* Parent SHA1 ID. */
};

struct graph_row {
	size_t size;
	struct graph_column *columns;
};

struct graph_v1 {
	struct graph api;
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

DEFINE_ALLOCATOR(realloc_graph_columns, struct graph_column, 32)
DEFINE_ALLOCATOR(realloc_graph_symbols, struct graph_symbol, 1)

static size_t get_free_graph_color(struct graph_v1 *graph)
{
	size_t i, free_color;

	for (free_color = i = 0; i < ARRAY_SIZE(graph->colors); i++) {
		if (graph->colors[i] < graph->colors[free_color])
			free_color = i;
	}

	graph->colors[free_color]++;
	return free_color;
}

static void
done_graph(struct graph *graph_)
{
	struct graph_v1 *graph = graph_->private;

	free(graph);
}

static void
done_graph_rendering(struct graph *graph_)
{
	struct graph_v1 *graph = graph_->private;

	free(graph->row.columns);
	free(graph->parents.columns);
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
graph_insert_column(struct graph_v1 *graph, struct graph_row *row, size_t pos, const char *id)
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

static bool
graph_add_parent(struct graph *graph_, const char *parent)
{
	struct graph_v1 *graph = graph_->private;

	if (graph->has_parents)
		return true;
	return graph_insert_column(graph, &graph->parents, graph->parents.size, parent) != NULL;
}

static bool
graph_needs_expansion(struct graph_v1 *graph)
{
	return graph->position + graph->parents.size > graph->row.size;
#if 0
	return graph->parents.size > 1
	    && graph->expanded < graph->parents.size;
#endif
}

static bool
graph_expand(struct graph_v1 *graph)
{
	while (graph_needs_expansion(graph)) {
		if (!graph_insert_column(graph, &graph->row, graph->position + graph->expanded, ""))
			return false;
		graph->expanded++;
	}

	return true;
}

static bool
graph_needs_collapsing(struct graph_v1 *graph)
{
	return graph->row.size > 1
	    && !graph_column_has_commit(&graph->row.columns[graph->row.size - 1]);
}

static bool
graph_collapse(struct graph_v1 *graph)
{
	while (graph_needs_collapsing(graph)) {
		graph->row.size--;
	}

	return true;
}

static void
graph_reorder_parents(struct graph_v1 *graph)
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
graph_canvas_append_symbol(struct graph_v1 *graph, struct graph_symbol *symbol)
{
	struct graph_canvas *canvas = graph->canvas;

	if (realloc_graph_symbols(&canvas->symbols, canvas->size, 1))
		canvas->symbols[canvas->size++] = *symbol;
}

static bool
graph_insert_parents(struct graph_v1 *graph)
{
	struct graph_row *row = &graph->row;
	struct graph_row *parents = &graph->parents;
	size_t orig_size = row->size;
	bool branched = false;
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
			branched = true;
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
			if (new->symbol.boundary) {
				symbol.boundary = 1;
			} else if (!graph_column_has_commit(new)) {
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

	return true;
}

static bool
graph_render_parents(struct graph *graph_, struct graph_canvas *canvas)
{
	struct graph_v1 *graph = graph_->private;

	if (!graph_expand(graph))
		return false;
	graph_reorder_parents(graph);
	graph_insert_parents(graph);
	if (!graph_collapse(graph))
		return false;

	return true;
}

static bool
graph_is_merge(struct graph_canvas *canvas)
{
	return !!canvas->symbols->merge;
}

static bool
graph_add_commit(struct graph *graph_, struct graph_canvas *canvas,
		 const char *id, const char *parents, bool is_boundary)
{
	struct graph_v1 *graph = graph_->private;
	int has_parents = 0;

	graph->position = graph_find_column_by_id(&graph->row, id);
	graph->id = id;
	graph->canvas = canvas;
	graph->is_boundary = is_boundary;
	graph->has_parents = false;

	while ((parents = strchr(parents, ' '))) {
		parents++;
		if (!graph_add_parent(graph_, parents))
			return false;
		has_parents++;
	}

	if (graph->parents.size == 0 &&
	    !graph_add_parent(graph_, ""))
		return false;

	graph->has_parents = has_parents > 0;

	return true;
}

static const char *
graph_symbol_to_utf8(const struct graph_symbol *symbol)
{
	if (symbol->commit) {
		if (symbol->boundary)
			return " ◯";
		else if (symbol->initial)
			return " ◎";
		else if (symbol->merge)
			return " ●";
		return " ∙";
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

static const chtype *
graph_symbol_to_chtype(const struct graph_symbol *symbol)
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

static const char *
graph_symbol_to_ascii(const struct graph_symbol *symbol)
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

static void
graph_foreach_symbol(const struct graph *graph, const struct graph_canvas *canvas,
		     graph_symbol_iterator_fn fn, void *data)
{
	int i;

	for (i = 0; i < canvas->size; i++) {
		struct graph_symbol *symbol = &canvas->symbols[i];
		int color_id = symbol->commit ? GRAPH_COMMIT_COLOR : symbol->color;

		if (fn(data, graph, symbol, color_id, i == 0))
			break;
	}
}

struct graph *
init_graph_v1(void)
{
	struct graph_v1 *graph = calloc(1, sizeof(*graph));
	struct graph *api = NULL;

	if (graph) {
		api = &graph->api;

		api->private = graph;
		api->done = done_graph;
		api->done_rendering = done_graph_rendering;
		api->add_commit = graph_add_commit;
		api->add_parent = graph_add_parent;
		api->render_parents = graph_render_parents;
		api->is_merge = graph_is_merge;
		api->foreach_symbol = graph_foreach_symbol;
		api->symbol_to_ascii = graph_symbol_to_ascii;
		api->symbol_to_utf8 = graph_symbol_to_utf8;
		api->symbol_to_chtype = graph_symbol_to_chtype;
	}

	return api;
}

/* vim: set ts=8 sw=8 noexpandtab: */
