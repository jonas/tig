/* Copyright (c) 2006-2012 Jonas Fonseca <fonseca@diku.dk>
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

#ifndef TIG_GIT_H
#define TIG_GIT_H

/*
 * Argv-style git command macros.
 */

#define GIT_DIFF_INITIAL(cached_arg, context_arg, space_arg, old_name, new_name) \
	"git", "diff", ENCODING_ARG, "--no-color", "--patch-with-stat", \
		(cached_arg), (context_arg), (space_arg), "--", (old_name), (new_name), NULL

#define GIT_DIFF_STAGED_INITIAL(context_arg, space_arg, new_name) \
	GIT_DIFF_INITIAL("--cached", context_arg, space_arg, "", new_name)

#define GIT_DIFF_STAGED(context_arg, space_arg, old_name, new_name) \
	"git", "diff-index", ENCODING_ARG, "--root", "--patch-with-stat", \
		"--cached", (context_arg), (space_arg), "HEAD", "--", (old_name), (new_name), NULL

#define GIT_DIFF_UNSTAGED(context_arg, space_arg, old_name, new_name) \
	"git", "diff-files", ENCODING_ARG, "--root", "--patch-with-stat", \
		(context_arg), (space_arg), "--", (old_name), (new_name), NULL

/* Don't show staged unmerged entries. */
#define GIT_DIFF_STAGED_FILES(output_arg) \
	"git", "diff-index", (output_arg), "--diff-filter=ACDMRTXB", "--cached", "HEAD", NULL

#define GIT_DIFF_UNSTAGED_FILES(output_arg) \
	"git", "diff-files", (output_arg), NULL

#define GIT_DIFF_BLAME(context_arg, space_arg, new_name) \
	GIT_DIFF_UNSTAGED(context_arg, space_arg, "", new_name)

#define GIT_DIFF_BLAME_NO_PARENT(context_arg, space_arg, new_name) \
	GIT_DIFF_INITIAL("", context_arg, space_arg, "/dev/null", new_name)

#define GIT_MAIN_LOG(diffargs, revargs, fileargs) \
	"git", "log", ENCODING_ARG, "--no-color", "--pretty=raw", "--parents", \
		opt_commit_order_arg, (diffargs), (revargs), \
		"--", (fileargs), NULL

#endif
