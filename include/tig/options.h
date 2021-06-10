/* Copyright (c) 2006-2015 Jonas Fonseca <jonas.fonseca@gmail.com>
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
#include "tig/line.h"

/*
 * Option variables.
 */

struct ref_format;
struct view_column;
typedef struct view_column *view_settings;

#ifndef TIG_USER_CONFIG
#define TIG_USER_CONFIG "~/.tigrc"
#endif

#define OPTION_INFO(_) \
	_(blame_options,		const char **,		VIEW_BLAME_LIKE) \
	_(blame_view,			view_settings,		VIEW_NO_FLAGS) \
	_(blob_view,			view_settings,		VIEW_NO_FLAGS) \
	_(commit_order,			enum commit_order,	VIEW_LOG_LIKE) \
	_(diff_context,			int,			VIEW_DIFF_LIKE) \
	_(diff_noprefix,		bool,			VIEW_NO_FLAGS) \
	_(diff_show_signs,		bool,			VIEW_NO_FLAGS) \
	_(diff_column_highlight,	enum diff_column_highlight,	VIEW_NO_FLAGS) \
	_(diff_options,			const char **,		VIEW_DIFF_LIKE) \
	_(diff_highlight,		const char *,		VIEW_DIFF_LIKE) \
	_(diff_view,			view_settings,		VIEW_NO_FLAGS) \
	_(editor_line_number,		bool,			VIEW_NO_FLAGS) \
	_(file_args,			const char **,		VIEW_NO_FLAGS) \
	_(file_filter,			bool,			VIEW_DIFF_LIKE | VIEW_LOG_LIKE) \
	_(focus_child,			bool,			VIEW_NO_FLAGS) \
	_(git_colors,			const char **,		VIEW_NO_FLAGS) \
	_(grep_view,			view_settings,		VIEW_NO_FLAGS) \
	_(history_size,			int,			VIEW_NO_FLAGS) \
	_(horizontal_scroll,		double,			VIEW_NO_FLAGS) \
	_(id_width,			int,			VIEW_NO_FLAGS) \
	_(ignore_case,			enum ignore_case,	VIEW_NO_FLAGS) \
	_(ignore_space,			enum ignore_space,	VIEW_DIFF_LIKE) \
	_(line_graphics,		enum graphic,		VIEW_RESET_DISPLAY) \
	_(log_options,			const char **,		VIEW_LOG_LIKE) \
	_(log_view,			view_settings,		VIEW_NO_FLAGS) \
	_(reflog_view,			view_settings,		VIEW_NO_FLAGS) \
	_(mailmap,			bool,			VIEW_DIFF_LIKE | VIEW_LOG_LIKE) \
	_(main_options,			const char **,		VIEW_LOG_LIKE) \
	_(main_view,			view_settings,		VIEW_NO_FLAGS) \
	_(mouse,			bool,			VIEW_NO_FLAGS) \
	_(mouse_scroll,			int,			VIEW_NO_FLAGS) \
	_(mouse_wheel_cursor,		bool,			VIEW_NO_FLAGS) \
	_(pager_view,			view_settings,		VIEW_NO_FLAGS) \
	_(pgrp,				bool,			VIEW_NO_FLAGS) \
	_(reference_format,		struct ref_format **,	VIEW_NO_FLAGS) \
	_(refresh_interval,		int,			VIEW_NO_FLAGS) \
	_(refresh_mode,			enum refresh_mode,	VIEW_NO_FLAGS) \
	_(refs_view,			view_settings,		VIEW_NO_FLAGS) \
	_(rev_args,			const char **,		VIEW_NO_FLAGS) \
	_(send_child_enter,		bool,			VIEW_NO_FLAGS) \
	_(show_changes,			bool,			VIEW_NO_FLAGS) \
	_(show_notes,			bool,			VIEW_NO_FLAGS) \
	_(show_untracked,		bool,			VIEW_NO_FLAGS) \
	_(split_view_height,		double,			VIEW_RESET_DISPLAY) \
	_(split_view_width,		double,			VIEW_RESET_DISPLAY) \
	_(stage_view,			view_settings,		VIEW_NO_FLAGS) \
	_(start_on_head,		bool,			VIEW_NO_FLAGS) \
	_(stash_view,			view_settings,		VIEW_NO_FLAGS) \
	_(status_show_untracked_dirs,	bool,			VIEW_STATUS_LIKE) \
	_(status_show_untracked_files,	bool,			VIEW_STATUS_LIKE) \
	_(status_view,			view_settings,		VIEW_NO_FLAGS) \
	_(tab_size,			int,			VIEW_NO_FLAGS) \
	_(tree_view,			view_settings,		VIEW_NO_FLAGS) \
	_(truncation_delimiter,		const char *,		VIEW_NO_FLAGS) \
	_(vertical_split,		enum vertical_split,	VIEW_RESET_DISPLAY | VIEW_DIFF_LIKE) \
	_(wrap_lines,			bool,			VIEW_NO_FLAGS) \
	_(wrap_search,			bool,			VIEW_NO_FLAGS) \

#define DEFINE_OPTION_EXTERNS(name, type, flags) extern type opt_##name;
OPTION_INFO(DEFINE_OPTION_EXTERNS)

/*
 * View column options.
 */

#define AUTHOR_COLUMN_OPTIONS(_) \
	_(display,			enum author,		VIEW_NO_FLAGS) \
	_(width,			int,			VIEW_NO_FLAGS) \
	_(maxwidth,			int,			VIEW_NO_FLAGS) \

#define COMMIT_TITLE_COLUMN_OPTIONS(_) \
	_(display,			bool,			VIEW_NO_FLAGS) \
	_(graph,			enum graph_display,	VIEW_LOG_LIKE) \
	_(refs,				bool,			VIEW_NO_FLAGS) \
	_(overflow,			int,			VIEW_NO_FLAGS) \

#define DATE_COLUMN_OPTIONS(_) \
	_(display,			enum date,		VIEW_NO_FLAGS) \
	_(local,			bool,			VIEW_NO_FLAGS) \
	_(format,			const char *,		VIEW_NO_FLAGS) \
	_(width,			int,			VIEW_NO_FLAGS) \

#define FILE_NAME_COLUMN_OPTIONS(_) \
	_(display,			enum filename,		VIEW_GREP_LIKE) \
	_(width,			int,			VIEW_NO_FLAGS) \
	_(maxwidth,			int,			VIEW_NO_FLAGS) \

#define FILE_SIZE_COLUMN_OPTIONS(_) \
	_(display,			enum file_size,		VIEW_NO_FLAGS) \
	_(width,			int,			VIEW_NO_FLAGS) \

#define ID_COLUMN_OPTIONS(_) \
	_(display,			bool,			VIEW_NO_FLAGS) \
	_(color,			bool,			VIEW_NO_FLAGS) \
	_(width,			int,			VIEW_NO_FLAGS) \

#define LINE_NUMBER_COLUMN_OPTIONS(_) \
	_(display,			bool,			VIEW_NO_FLAGS) \
	_(interval,			int,			VIEW_NO_FLAGS) \
	_(width,			int,			VIEW_NO_FLAGS) \

#define MODE_COLUMN_OPTIONS(_) \
	_(display,			bool,			VIEW_NO_FLAGS) \
	_(width,			int,			VIEW_NO_FLAGS) \

#define REF_COLUMN_OPTIONS(_) \
	_(display,			bool,			VIEW_NO_FLAGS) \
	_(width,			int,			VIEW_NO_FLAGS) \
	_(maxwidth,			int,			VIEW_NO_FLAGS) \

#define SECTION_COLUMN_OPTIONS(_) \
	_(type,				enum line_type,		VIEW_NO_FLAGS) \
	_(text,				const char *,		VIEW_NO_FLAGS) \

#define STATUS_COLUMN_OPTIONS(_) \
	_(display,			enum status_label,	VIEW_NO_FLAGS) \

#define TEXT_COLUMN_OPTIONS(_) \
	_(display,			bool,			VIEW_NO_FLAGS) \
	_(commit_title_overflow,	int,			VIEW_NO_FLAGS) \

#define COLUMN_OPTIONS(_) \
	_(author, AUTHOR, AUTHOR_COLUMN_OPTIONS) \
	_(commit_title, COMMIT_TITLE, COMMIT_TITLE_COLUMN_OPTIONS) \
	_(date, DATE, DATE_COLUMN_OPTIONS) \
	_(file_name, FILE_NAME, FILE_NAME_COLUMN_OPTIONS) \
	_(file_size, FILE_SIZE, FILE_SIZE_COLUMN_OPTIONS) \
	_(id, ID, ID_COLUMN_OPTIONS) \
	_(line_number, LINE_NUMBER, LINE_NUMBER_COLUMN_OPTIONS) \
	_(mode, MODE, MODE_COLUMN_OPTIONS) \
	_(ref, REF, REF_COLUMN_OPTIONS) \
	_(section, SECTION, SECTION_COLUMN_OPTIONS) \
	_(status, STATUS, STATUS_COLUMN_OPTIONS) \
	_(text, TEXT, TEXT_COLUMN_OPTIONS) \

#define DEFINE_COLUMN_OPTIONS_STRUCT_VALUE(name, type, flags) type name;

#define DEFINE_COLUMN_OPTIONS_STRUCT(name, id, options) \
	struct name##_options { \
		options(DEFINE_COLUMN_OPTIONS_STRUCT_VALUE) \
	} name;

union view_column_options {
	COLUMN_OPTIONS(DEFINE_COLUMN_OPTIONS_STRUCT)
};

/*
 * Global state variables.
 */

extern iconv_t opt_iconv_out;
extern char opt_editor[SIZEOF_STR];
extern const char **opt_cmdline_args;
extern bool opt_log_follow;
extern bool opt_word_diff;

/*
 * Mapping between options and command argument mapping.
 */

void update_options_from_argv(const char *argv[]);

const char *ignore_space_arg();
const char *commit_order_arg();
const char *commit_order_arg_with_graph(enum graph_display graph_display);
const char *log_custom_pretty_arg();
const char *use_mailmap_arg();
const char *diff_context_arg();
const char *show_notes_arg();

/*
 * Option loading and parsing.
 */

struct option_info {
	const char *name;
	size_t namelen;
	const char *type;
	void *value;
	int flags;
	bool seen;
};

struct option_info *find_option_info(struct option_info *option, size_t options, const char *prefix, const char *name);
enum status_code parse_option(struct option_info *option, const char *prefix, const char *arg);
struct option_info *find_column_option_info(enum view_column_type type, union view_column_options *opts,
					const char *option, struct option_info *column_info, const char **column_name);
enum status_code parse_int(int *opt, const char *arg, int min, int max);
enum status_code parse_step(double *opt, const char *arg);
enum status_code set_option(const char *opt, int argc, const char *argv[]);
enum status_code load_options(void);
enum status_code load_git_config(void);
enum status_code save_options(const char *path);
const char *format_option_value(const struct option_info *option, char buf[], size_t bufsize);

#endif
/* vim: set ts=8 sw=8 noexpandtab: */
