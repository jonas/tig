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

#ifndef TIG_GIT_H
#define TIG_GIT_H

/*
 * Argv-style git command macros.
 */

#define GIT_DIFF_INITIAL(encoding_arg, cached_arg, context_arg, space_arg, old_name, new_name) \
	"git", "diff", (encoding_arg), "--no-color", "--patch-with-stat", \
		(cached_arg), (context_arg), (space_arg), "--", (old_name), (new_name), NULL

#define GIT_DIFF_STAGED_INITIAL(encoding_arg, context_arg, space_arg, new_name) \
	GIT_DIFF_INITIAL(encoding_arg, "--cached", context_arg, space_arg, "", new_name)

#define GIT_DIFF_STAGED(encoding_arg, context_arg, space_arg, old_name, new_name) \
	"git", "diff-index", (encoding_arg), "--root", "--patch-with-stat", "-C", "-M", \
		"--cached", "--diff-filter=ACDMRTXB", DIFF_ARGS, "%(cmdlineargs)", (context_arg), (space_arg), "HEAD", \
		"--", (old_name), (new_name), NULL

#define GIT_DIFF_UNSTAGED(encoding_arg, context_arg, space_arg, old_name, new_name) \
	"git", "diff-files", (encoding_arg), "--root", "--patch-with-stat", "-C", "-M", \
		DIFF_ARGS, "%(cmdlineargs)", (context_arg), (space_arg), "--", (old_name), (new_name), NULL

/* Don't show staged unmerged entries. */
#define GIT_DIFF_STAGED_FILES(output_arg) \
	"git", "diff-index", (output_arg), "--diff-filter=ACDMRTXB", "-C", "--cached", "HEAD", "--", NULL

#define GIT_DIFF_UNSTAGED_FILES(output_arg) \
	"git", "diff-files", (output_arg), NULL

#define GIT_DIFF_BLAME(encoding_arg, context_arg, space_arg, new_name) \
	GIT_DIFF_UNSTAGED(encoding_arg, context_arg, space_arg, "", new_name)

#define GIT_DIFF_BLAME_NO_PARENT(encoding_arg, context_arg, space_arg, new_name) \
	GIT_DIFF_INITIAL(encoding_arg, "", context_arg, space_arg, "/dev/null", new_name)

#define GIT_MAIN_LOG(encoding_arg, commit_order_arg, mainargs, diffargs, revargs, fileargs, show_notes_arg, pretty_arg) \
	"git", "log", (encoding_arg), \
		(commit_order_arg), (mainargs), (diffargs), (revargs), "--date=raw", "--parents", \
		"--no-color", (show_notes_arg), (pretty_arg), "--", (fileargs), NULL

#define GIT_MAIN_LOG_RAW(encoding_arg, commit_order_arg, mainargs, diffargs, revargs, fileargs, show_notes_arg) \
	GIT_MAIN_LOG(encoding_arg, commit_order_arg, mainargs, diffargs, revargs, fileargs, show_notes_arg, \
		     "--pretty=raw")

#endif

/* vim: set ts=8 sw=8 noexpandtab: */
