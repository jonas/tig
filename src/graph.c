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

#include "tig/tig.h"
#include "tig/util.h"
#include "tig/graph.h"

DEFINE_ALLOCATOR(realloc_graph_columns, struct graph_column, 32)
DEFINE_ALLOCATOR(realloc_graph_symbols, struct graph_symbol, 1)

struct id_color {
	char *id;
	size_t color;
};

struct id_color *
id_color_new(const char *id, size_t color)
{
	struct id_color *node = malloc(sizeof(struct id_color));

	node->id = (char *) malloc(strlen(id) + 1);
	strcpy(node->id, id);
	node->color = color;

	return node;
}

static void
id_color_delete(struct id_color *node)
{
	free(node->id);
	free(node);
}

static int
id_color_eq(const void *entry, const void *element)
{
	return strcmp(((const struct id_color *) entry)->id, ((const struct id_color *) element)->id) == 0;
}

static void
key_del(void *key)
{
	id_color_delete((struct id_color *) key);
}

static hashval_t
id_color_hash(const void *node)
{
	return htab_hash_string(((const struct id_color*) node)->id);
}

static void
colors_add_id(struct colors *colors, const char *id, const size_t color)
{
	struct id_color *node = id_color_new(id, color);
	void **slot = htab_find_slot(colors->id_map, node, INSERT);

	if (slot != NULL && *slot == NULL) {
		*slot = node;
		colors->count[color]++;
	} else {
		id_color_delete(node);
	}
}

static void
colors_remove_id(struct colors *colors, const char *id)
{
	struct id_color *node = id_color_new(id, 0);
	void **slot = htab_find_slot(colors->id_map, node, NO_INSERT);

	if (slot != NULL && *slot != NULL) {
		colors->count[((struct id_color *) *slot)->color]--;
		htab_clear_slot(colors->id_map, slot);
	}

	id_color_delete(node);
}

static size_t
colors_get_color(struct colors *colors, const char *id)
{
	struct id_color *key = id_color_new(id, 0);
	struct id_color *node = (struct id_color *) htab_find(colors->id_map, key);

	id_color_delete(key);

	if (node == NULL) {
		return (size_t) -1; // Max value of size_t. ID not found.
	}
	return node->color;
}

static size_t
colors_get_free_color(struct colors *colors)
{
	size_t free_color = 0;
	size_t lowest = (size_t) -1; // Max value of size_t
	int i;

	for (i = 0; i < ARRAY_SIZE(colors->count); i++) {
		if (colors->count[i] < lowest) {
			lowest = colors->count[i];
			free_color = i;
		}
	}
	return free_color;
}

static void
colors_init(struct colors *colors)
{
	if (colors->id_map == NULL) {
		uint size = 500;

		colors->id_map = htab_create_alloc(size, id_color_hash, id_color_eq, key_del, calloc, free);
	}
}

static size_t
get_color(struct graph *graph, char *new_id)
{
	size_t color;

	colors_init(&graph->colors);
	color = colors_get_color(&graph->colors, new_id);

	if (color < (size_t) -1) {
		return color;
	}

	color = colors_get_free_color(&graph->colors);
	colors_add_id(&graph->colors, new_id, color);

	return color;
}

void
done_graph(struct graph *graph)
{
	free(graph->prev_row.columns);
	free(graph->row.columns);
	free(graph->next_row.columns);
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
		if (!graph_column_has_commit(&row->columns[i]) && free_column == row->size)
			free_column = i;
		else if (!strcmp(row->columns[i].id, id))
			return i;
	}

	return free_column;
}

static size_t
graph_find_free_column(struct graph_row *row)
{
	size_t i;

	for (i = 0; i < row->size; i++) {
		if (!graph_column_has_commit(&row->columns[i]))
			return i;
	}

	return row->size;
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
	return graph->position + graph->parents.size > graph->row.size;
}

static bool
graph_expand(struct graph *graph)
{
	while (graph_needs_expansion(graph)) {
		if (!graph_insert_column(graph, &graph->prev_row, graph->prev_row.size, ""))
			return FALSE;

		if (!graph_insert_column(graph, &graph->row, graph->row.size, ""))
			return FALSE;

		if (!graph_insert_column(graph, &graph->next_row, graph->next_row.size, ""))
			return FALSE;
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
		graph->prev_row.size--;
		graph->row.size--;
		graph->next_row.size--;
	}

	return TRUE;
}

static void
graph_canvas_append_symbol(struct graph *graph, struct graph_symbol *symbol)
{
	struct graph_canvas *canvas = graph->canvas;

	if (realloc_graph_symbols(&canvas->symbols, canvas->size, 1))
		canvas->symbols[canvas->size++] = *symbol;
}

static void
graph_row_clear_commit(struct graph_row *row, const char *id)
{
	int i;

	for (i = 0; i < row->size; i++) {
		if (strcmp(row->columns[i].id, id) == 0) {
			row->columns[i].id[0] = 0;
		}
	}
}

static void
graph_insert_parents(struct graph *graph)
{
	struct graph_row *prev_row = &graph->prev_row;
	struct graph_row *row = &graph->row;
	struct graph_row *next_row = &graph->next_row;
	struct graph_row *parents = &graph->parents;
	int i;

	for (i = 0; i < parents->size; i++) {
		struct graph_column *new = &parents->columns[i];

		if (graph_column_has_commit(new)) {
			size_t match = graph_find_free_column(next_row);

			if (match == next_row->size && next_row->columns[next_row->size - 1].id) {
				graph_insert_column(graph, next_row, next_row->size, new->id);
				graph_insert_column(graph, row, row->size, "");
				graph_insert_column(graph, prev_row, prev_row->size, "");
			} else {
				next_row->columns[match] = *new;
			}
		}
	}
}

static bool
commit_is_in_row(const char *id, struct graph_row *row)
{
	int i;

	for (i = 0; i < row->size; i++) {
		if (!graph_column_has_commit(&row->columns[i]))
			continue;

		if (strcmp(id, row->columns[i].id) == 0)
			return true;
	}
	return false;
}

static void
graph_remove_collapsed_columns(struct graph *graph)
{
	struct graph_row *row = &graph->next_row;
	int i;

	for (i = row->size - 1; i > 0; i--) {
		if (i == graph->position)
			continue;

		if (i == graph->position + 1)
			continue;

		if (strcmp(row->columns[i].id, graph->id) == 0)
			continue;

		if (strcmp(row->columns[i].id, row->columns[i - 1].id) != 0)
			continue;

		if (commit_is_in_row(row->columns[i].id, &graph->parents) && !graph_column_has_commit(&graph->prev_row.columns[i]))
			continue;

		if (strcmp(row->columns[i - 1].id, graph->prev_row.columns[i - 1].id) != 0 || graph->prev_row.columns[i - 1].symbol.shift_left)
			row->columns[i] = row->columns[i + 1];
	}
}

static void
graph_fill_empty_columns(struct graph *graph)
{
	struct graph_row *row = &graph->next_row;
	int i;

	for (i = row->size - 2; i >= 0; i--) {
		if (!graph_column_has_commit(&row->columns[i])) {
			row->columns[i] = row->columns[i + 1];
		}
	}
}

static void
graph_generate_next_row(struct graph *graph)
{
	graph_row_clear_commit(&graph->next_row, graph->id);
	graph_insert_parents(graph);
	graph_remove_collapsed_columns(graph);
	graph_fill_empty_columns(graph);
}

static int
commits_in_row(struct graph_row *row)
{
	int count = 0;
	int i;

	for (i = 0; i < row->size;i++) {
		if (graph_column_has_commit(&row->columns[i]))
			count++;
	}

	return count;
}

static void
graph_commit_next_row(struct graph *graph)
{
	int i;

	for (i = 0; i < graph->row.size; i++) {
		graph->prev_row.columns[i] = graph->row.columns[i];

		if (i == graph->position && commits_in_row(&graph->parents) > 0)
			graph->prev_row.columns[i] = graph->next_row.columns[i];

		if (!graph_column_has_commit(&graph->prev_row.columns[i]))
			graph->prev_row.columns[i] = graph->next_row.columns[i];

		graph->row.columns[i] = graph->next_row.columns[i];
	}

	graph->prev_position = graph->position;
}

static bool
continued_down(struct graph_row *row, struct graph_row *next_row, int pos)
{
	if (strcmp(row->columns[pos].id, next_row->columns[pos].id) != 0)
		return false;

	if (row->columns[pos].symbol.shift_left)
		return false;

	return true;
}

static bool
shift_left(struct graph_row *row, struct graph_row *prev_row, int pos)
{
	int i;

	if (!graph_column_has_commit(&row->columns[pos]))
		return false;

	for (i = pos - 1; i >= 0; i--) {
		if (!graph_column_has_commit(&row->columns[i]))
			continue;

		if (strcmp(row->columns[i].id, row->columns[pos].id) != 0)
			continue;

		if (!continued_down(prev_row, row, i))
			return true;

		break;
	}

	return false;
}

static bool
new_column(struct graph_row *row, struct graph_row *prev_row, int pos)
{
	int i;

	if (!graph_column_has_commit(&prev_row->columns[pos]))
		return true;

	for (i = pos; i < row->size; i++) {
		if (strcmp(row->columns[pos].id, prev_row->columns[i].id) == 0)
			return false;
	}

	return true;
}

static bool
continued_right(struct graph_row *row, int pos, int commit_pos)
{
	int i, end;

	if (pos < commit_pos)
		end = commit_pos;
	else
		end = row->size;

	for (i = pos + 1; i < end; i++) {
		if (strcmp(row->columns[pos].id, row->columns[i].id) == 0)
			return true;
	}

	return false;
}

static bool
continued_left(struct graph_row *row, int pos, int commit_pos)
{
	int i, start;

	if (pos < commit_pos)
		start = 0;
	else
		start = commit_pos;

	for (i = start; i < pos; i++) {
		if (!graph_column_has_commit(&row->columns[i]))
			continue;

		if (strcmp(row->columns[pos].id, row->columns[i].id) == 0)
			return true;
	}

	return false;
}

static bool
parent_down(struct graph_row *parents, struct graph_row *next_row, int pos)
{
	int parent;

	for (parent = 0; parent < parents->size; parent++) {
		if (!graph_column_has_commit(&parents->columns[parent]))
			continue;

		if (strcmp(parents->columns[parent].id, next_row->columns[pos].id) == 0)
			return true;
	}

	return false;
}

static bool
parent_right(struct graph_row *parents, struct graph_row *row, struct graph_row *next_row, int pos)
{
	int parent, i;

	for (parent = 0; parent < parents->size; parent++) {
		if (!graph_column_has_commit(&parents->columns[parent]))
			continue;

		for (i = pos + 1; i < next_row->size; i++) {
			if (strcmp(parents->columns[parent].id, next_row->columns[i].id) != 0)
				continue;

			if (strcmp(parents->columns[parent].id, row->columns[i].id) != 0)
				return true;
		}
	}

	return false;
}

static bool
flanked(struct graph_row *row, int pos, int commit_pos, const char *commit_id)
{
	int i, start, end;

	if (pos < commit_pos) {
		start = 0;
		end = pos;
	} else {
		start = pos + 1;
		end = row->size;
	}

	for (i = start; i < end; i++) {
		if (strcmp(row->columns[i].id, commit_id) == 0)
			return true;
	}

	return false;
}

static bool
below_commit(int pos, struct graph *graph)
{
	if (!pos == graph->prev_position)
		return false;

	if (!strcmp(graph->row.columns[pos].id, graph->prev_row.columns[pos].id) == 0)
		return false;

	return true;
}

static void
graph_generate_symbols(struct graph *graph)
{
	struct graph_row *prev_row = &graph->prev_row;
	struct graph_row *row = &graph->row;
	struct graph_row *next_row = &graph->next_row;
	struct graph_row *parents = &graph->parents;
	int pos;

	for (pos = 0; pos < row->size; pos++) {
		struct graph_column *column = &row->columns[pos];
		struct graph_symbol *symbol = &column->symbol;
		char *id = next_row->columns[pos].id;

		symbol->commit            = (pos == graph->position);
		symbol->boundary          = (pos == graph->position && next_row->columns[pos].symbol.boundary);
		symbol->initial           = (commits_in_row(parents) < 1);
		symbol->merge             = (commits_in_row(parents) > 1);

		symbol->continued_down    = continued_down(row, next_row, pos);
		symbol->continued_up      = continued_down(prev_row, row, pos);
		symbol->continued_right   = continued_right(row, pos, graph->position);
		symbol->continued_left    = continued_left(row, pos, graph->position);
		symbol->continued_up_left = continued_left(prev_row, pos, prev_row->size);

		symbol->parent_down       = parent_down(parents, next_row, pos);
		symbol->parent_right      = (pos > graph->position && parent_right(parents, row, next_row, pos));

		symbol->below_commit      = below_commit(pos, graph);
		symbol->flanked           = flanked(row, pos, graph->position, graph->id);
		symbol->next_right        = continued_right(next_row, pos, 0);
		symbol->matches_commit    = (strcmp(column->id, graph->id) == 0);

		symbol->shift_left        = shift_left(row, prev_row, pos);
		symbol->continue_shift    = shift_left(row, prev_row, pos + 1);
		symbol->below_shift       = prev_row->columns[pos].symbol.shift_left;

		symbol->new_column        = new_column(row, prev_row, pos);
		symbol->empty             = (!graph_column_has_commit(&row->columns[pos]));

		if (graph_column_has_commit(column)) {
			id = column->id;
		}
		symbol->color = get_color(graph, id);

		graph_canvas_append_symbol(graph, symbol);
	}

	colors_remove_id(&graph->colors, graph->id);
}

bool
graph_render_parents(struct graph *graph)
{
	if (!graph_expand(graph))
		return FALSE;

	graph_generate_next_row(graph);
	graph_generate_symbols(graph);
	graph_commit_next_row(graph);

	graph->parents.size = graph->position = 0;

	if (!graph_collapse(graph))
		return FALSE;

	return TRUE;
}

bool
graph_add_commit(struct graph *graph, struct graph_canvas *canvas,
		 const char *id, const char *parents, bool is_boundary)
{
	graph->position = graph_find_column_by_id(&graph->row, id);
	string_copy_rev(graph->id, id);
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

const bool
graph_symbol_forks(struct graph_symbol *symbol)
{
	if (!symbol->continued_down)
		return false;

	if (!symbol->continued_right)
		return false;

	if (!symbol->continued_up)
		return false;

	return true;
}

const bool
graph_symbol_cross_over(struct graph_symbol *symbol)
{
	if (symbol->empty)
		return false;

	if (!symbol->continued_down)
		return false;

	if (!symbol->continued_up && !symbol->new_column && !symbol->below_commit)
		return false;

	if (symbol->shift_left)
		return false;

	if (symbol->parent_right && symbol->merge)
		return true;

	if (symbol->flanked)
		return true;

	return false;
}

const bool
graph_symbol_turn_left(struct graph_symbol *symbol)
{
	if (symbol->matches_commit && symbol->continued_right && !symbol->continued_down)
		return false;

	if (symbol->continue_shift)
		return false;

	if (symbol->continued_up || symbol->new_column || symbol->below_commit) {
		if (symbol->matches_commit)
			return true;

		if (symbol->shift_left)
			return true;
	}

	return false;
}

const bool
graph_symbol_turn_down_cross_over(struct graph_symbol *symbol)
{
	if (!symbol->continued_down)
		return false;

	if (!symbol->continued_right)
		return false;

	if (symbol->flanked)
		return true;

	if (symbol->merge)
		return true;

	return false;
}

const bool
graph_symbol_turn_down(struct graph_symbol *symbol)
{
	if (!symbol->continued_down)
		return false;

	if (!symbol->continued_right)
		return false;

	return true;
}

const bool
graph_symbol_merge(struct graph_symbol *symbol)
{
	if (symbol->continued_down)
		return false;

	if (!symbol->parent_down)
		return false;

	if (symbol->parent_right)
		return false;

	return true;
}

const bool
graph_symbol_multi_merge(struct graph_symbol *symbol)
{
	if (!symbol->parent_down)
		return false;

	if (!symbol->parent_right)
		return false;

	return true;
}

const bool
graph_symbol_vertical_bar(struct graph_symbol *symbol)
{
	if (symbol->empty)
		return false;

	if (symbol->shift_left)
		return false;

	if (!symbol->continued_down)
		return false;

	if (symbol->continued_up)
		return true;

	if (symbol->parent_right)
		return false;

	if (symbol->flanked)
		return false;

	if (symbol->continued_right)
		return false;

	return true;
}

const bool
graph_symbol_horizontal_bar(struct graph_symbol *symbol)
{
	if (symbol->shift_left)
		return true;

	if (symbol->continued_down)
		return false;

	if (!symbol->parent_right && !symbol->continued_right)
		return false;

	if ((symbol->continued_up && !symbol->continued_up_left))
		return false;

	if (!symbol->below_commit)
		return true;

	return false;
}

const bool
graph_symbol_multi_branch(struct graph_symbol *symbol)
{
	if (symbol->continued_down)
		return false;

	if (!symbol->continued_right)
		return false;

	if (symbol->below_shift)
		return false;

	if (symbol->continued_up || symbol->new_column || symbol->below_commit) {
		if (symbol->matches_commit)
			return true;

		if (symbol->shift_left)
			return true;
	}

	return false;
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

	if (graph_symbol_cross_over(symbol))
		return "─│";

	if (graph_symbol_vertical_bar(symbol))
		return " │";

	if (graph_symbol_turn_left(symbol))
		return "─╯";

	if (graph_symbol_multi_branch(symbol))
		return "─┴";

	if (graph_symbol_horizontal_bar(symbol))
		return "──";

	if (graph_symbol_forks(symbol))
		return " ├";

	if (graph_symbol_turn_down_cross_over(symbol))
		return "─╭";

	if (graph_symbol_turn_down(symbol))
		return " ╭";

	if (graph_symbol_merge(symbol))
		return "─╮";

	if (graph_symbol_multi_merge(symbol))
		return "─┬";

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

	} else if (graph_symbol_cross_over(symbol)) {
		graphics[0] = ACS_HLINE;
		graphics[1] = ACS_VLINE;

	} else if (graph_symbol_vertical_bar(symbol)) {
		graphics[0] = ' ';
		graphics[1] = ACS_VLINE;

	} else if (graph_symbol_turn_left(symbol)) {
		graphics[0] = ACS_HLINE;
		graphics[1] = ACS_LRCORNER;

	} else if (graph_symbol_multi_branch(symbol)) {
		graphics[0] = ACS_HLINE;
		graphics[1] = ACS_BTEE;

	} else if (graph_symbol_horizontal_bar(symbol)) {
		graphics[0] = graphics[1] = ACS_HLINE;

	} else if (graph_symbol_forks(symbol)) {
		graphics[0] = ' ';
		graphics[1] = ACS_LTEE;

	} else if (graph_symbol_turn_down_cross_over(symbol)) {
		graphics[0] = ACS_HLINE;
		graphics[1] = ACS_ULCORNER;

	} else if (graph_symbol_turn_down(symbol)) {
		graphics[0] = ' ';
		graphics[1] = ACS_ULCORNER;

	} else if (graph_symbol_merge(symbol)) {
		graphics[0] = ACS_HLINE;
		graphics[1] = ACS_URCORNER;

	} else if (graph_symbol_multi_merge(symbol)) {
		graphics[0] = ACS_HLINE;
		graphics[1] = ACS_TTEE;

	} else {
		graphics[0] = graphics[1] = ' ';
	}

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

	if (graph_symbol_cross_over(symbol))
		return "-|";

	if (graph_symbol_vertical_bar(symbol))
		return " |";

	if (graph_symbol_turn_left(symbol))
		return "-'";

	if (graph_symbol_multi_branch(symbol))
		return "-+";

	if (graph_symbol_horizontal_bar(symbol))
		return "--";

	if (graph_symbol_forks(symbol))
		return " +";

	if (graph_symbol_turn_down_cross_over(symbol))
		return "-.";

	if (graph_symbol_turn_down(symbol))
		return " .";

	if (graph_symbol_merge(symbol))
		return "-.";

	if (graph_symbol_multi_merge(symbol))
		return "-+";

	return "  ";
}

/* vim: set ts=8 sw=8 noexpandtab: */
