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

#ifndef TIG_OPTIONS_H
#define TIG_OPTIONS_H

#include "tig/tig.h"
#include "tig/util.h"

/*
 * Option variables.
 */

#define OPTION_INFO(_) \
	_(author_width,			int) \
	_(blame_options,		const char **) \
	_(commit_order,			enum commit_order) \
	_(diff_context,			int) \
	_(diff_options,			const char **) \
	_(editor_line_number,		bool) \
	_(focus_child,			bool) \
	_(horizontal_scroll,		double) \
	_(id_width,			int) \
	_(ignore_case,			bool) \
	_(ignore_space,			enum ignore_space) \
	_(line_graphics,		enum graphic) \
	_(line_number_interval,		int) \
	_(mouse,			bool) \
	_(mouse_scroll,			int) \
	_(read_git_colors,		bool) \
	_(scale_vsplit_view,		double) \
	_(show_author,			enum author) \
	_(show_changes,			bool) \
	_(show_date,			enum date) \
	_(show_file_size,		enum file_size) \
	_(show_filename,		enum filename) \
	_(show_filename_width,		int) \
	_(show_id,			bool) \
	_(show_line_numbers,		bool) \
	_(show_notes,			bool) \
	_(show_refs,			bool) \
	_(show_rev_graph,		bool) \
	_(split_view_height,		double) \
	_(status_untracked_dirs,	bool) \
	_(tab_size,			int) \
	_(title_overflow,		int) \
	_(vertical_split,		enum vertical_split) \
	_(wrap_lines,			bool) \

#define DEFINE_OPTION_EXTERNS(name, type) extern type opt_##name;
OPTION_INFO(DEFINE_OPTION_EXTERNS);

/*
 * Global state variables.
 */

extern bool opt_file_filter;
extern iconv_t opt_iconv_out;
extern char opt_editor[SIZEOF_STR];
extern const char **opt_cmdline_argv;
extern const char **opt_rev_argv;
extern const char **opt_file_argv;
extern char opt_env_lines[64];
extern char opt_env_columns[64];
extern char *opt_env[];

/*
 * Mapping between options and command argument mapping.
 */

void update_options_from_argv(const char *argv[]);

const char *ignore_space_arg();
const char *commit_order_arg();
const char *diff_context_arg();
const char *show_notes_arg();

/*
 * Option loading and parsing.
 */

enum status_code parse_int(int *opt, const char *arg, int min, int max);
enum status_code set_option(const char *opt, char *value);
int load_options(void);
int load_git_config(void);

#endif
/* vim: set ts=8 sw=8 noexpandtab: */
