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
		"--cached", (context_arg), (space_arg), "HEAD", "--", (old_name), (new_name), NULL

#define GIT_DIFF_UNSTAGED(encoding_arg, context_arg, space_arg, old_name, new_name) \
	"git", "diff-files", (encoding_arg), "--root", "--patch-with-stat", "-C", "-M", \
		(context_arg), (space_arg), "--", (old_name), (new_name), NULL

/* Don't show staged unmerged entries. */
#define GIT_DIFF_STAGED_FILES(output_arg) \
	"git", "diff-index", (output_arg), "--diff-filter=ACDMRTXB", "-M", "--cached", "HEAD", "--", NULL

#define GIT_DIFF_UNSTAGED_FILES(output_arg) \
	"git", "diff-files", (output_arg), NULL

#define GIT_DIFF_BLAME(encoding_arg, context_arg, space_arg, new_name) \
	GIT_DIFF_UNSTAGED(encoding_arg, context_arg, space_arg, "", new_name)

#define GIT_DIFF_BLAME_NO_PARENT(encoding_arg, context_arg, space_arg, new_name) \
	GIT_DIFF_INITIAL(encoding_arg, "", context_arg, space_arg, "/dev/null", new_name)

#define GIT_MAIN_LOG(encoding_arg, diffargs, revargs, fileargs) \
	"git", "log", (encoding_arg), \
		opt_commit_order_arg, (diffargs), (revargs), \
		"--no-color", "--pretty=raw", "--parents", \
		"--", (fileargs), NULL

/* FIXME(jfonseca): This is incomplete, but enough to support:
 * git rev-list --author=vivien HEAD | tig --stdin --no-walk */
#define GIT_REV_FLAGS \
	"--stdin", "--no-walk", "--boundary"

#endif

/* vim: set ts=8 sw=8 noexpandtab: */
