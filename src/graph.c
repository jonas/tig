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
#include "tig/graph.h"

struct graph *init_graph_v1(void);
struct graph *init_graph_v2(void);

struct graph *
init_graph(enum graph_display display)
{
	if (display == GRAPH_DISPLAY_V1)
		return init_graph_v1();
	if (display == GRAPH_DISPLAY_V2)
		return init_graph_v2();
	return NULL;
}

/* vim: set ts=8 sw=8 noexpandtab: */
