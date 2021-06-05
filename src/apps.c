/* Copyright (c) 2006-2017 Jonas Fonseca <jonas.fonseca@gmail.com>
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
#include "tig/io.h"
#include "tig/apps.h"

/*
 * general
 */

static bool
app_oneline_buf(char *buf, size_t bufsize, struct app_external *app, const char *dir)
{
	struct io io;
	return io_run(&io, IO_RD, dir, app->env, app->argv) \
		&& io_read_buf(&io, buf, bufsize, false);
}

/*
 * git
 */

static bool
app_git_exec_path(char *path, size_t path_len)
{
	static char exec_path[SIZEOF_STR] = "";
	struct app_external app = {
		{ "git", "--exec-path", NULL },
		{ "GIT_CONFIG=/dev/null", NULL },
	};

	if (!*exec_path)
		app_oneline_buf(exec_path, sizeof(exec_path), &app, NULL);

	if (!*exec_path)
		return false;

	string_ncopy_do(path, path_len, exec_path, sizeof(exec_path));
	return true;
}

/*
 * diff-highlight
 */

static bool
app_diff_highlight_path_search(char *dest, size_t destlen, const char *query)
{
	const char *env_path = getenv("PATH");
	char env_path_plus[SIZEOF_MED_STR];
	char exec_path[SIZEOF_STR];

	if (!query || !*query)
		return false;

	if (strchr(query, '~'))
		return path_expand(dest, destlen, query);

	if (strchr(query, '/')) {
		/* can only be interpreted as a fully qualified path */
		string_ncopy_do(dest, destlen, query, strlen(query));
		return true;
	}

	if (!env_path || !*env_path)
		env_path = _PATH_DEFPATH;

	if (app_git_exec_path(exec_path, sizeof(exec_path)))
		string_format(env_path_plus, "%s:%s/%s:%s/%s:%s/%s:%s/%s",
			      env_path,
			      exec_path, "../../share/git-core/contrib/diff-highlight",
			      exec_path, "../share/git-core/contrib/diff-highlight",
			      exec_path, "../../share/git/contrib/diff-highlight",
			      exec_path, "../share/git/contrib/diff-highlight");
	else
		string_ncopy(env_path_plus, env_path, strlen(env_path));

	if (!path_search(dest, destlen, query, env_path_plus, X_OK)
	    && !strcmp(query, "diff-highlight")
	    && !path_search(dest, destlen, "diff-highlight.perl", env_path_plus, R_OK))
		return false;

	return true;
}

struct app_external
*app_diff_highlight_load(const char *query)
{
	static struct app_external dhlt_app = { { NULL }, { "GIT_CONFIG=/dev/null", NULL } };
	static bool did_search = false;
	static char dhlt_path[SIZEOF_STR];
	static char perl_path[SIZEOF_STR];
	static char perl_include[SIZEOF_STR];

	if (!did_search
	    && app_diff_highlight_path_search(dhlt_path, sizeof(dhlt_path), query)
	    && *dhlt_path) {
		if (suffixcmp(dhlt_path, strlen(dhlt_path), "/diff-highlight.perl")) {
			dhlt_app.argv[0] = dhlt_path;
			dhlt_app.argv[1] = NULL;
		} else if (path_search(perl_path, sizeof(perl_path), "perl", getenv("PATH"), X_OK)) {
			/* if the package manager failed to "make install" within the contrib dir, rescue via */
			/* perl -MDiffHighlight -I/path/containing /path/containing/diff-highlight.perl */
			string_format(perl_include, "-I%s", dirname(dhlt_path));
			dhlt_app.argv[0] = perl_path;
			dhlt_app.argv[1] = "-MDiffHighlight";
			dhlt_app.argv[2] = perl_include;
			dhlt_app.argv[3] = dhlt_path;
			dhlt_app.argv[4] = NULL;
		}
	}
	did_search = true;

	return &dhlt_app;
}

/* vim: set ts=8 sw=8 noexpandtab: */
