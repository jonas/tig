/*
   Provides a log file to ease tracing the program.

   Copyright (C) 2006-2020
   Free Software Foundation, Inc.

   Written by:
   Roland Illig <roland.illig@gmx.de>, 2006
   Slava Zanko <slavazanko@gmail.com>, 2009, 2011

   This file is part of the Midnight Commander.

   The Midnight Commander is free software: you can redistribute it
   and/or modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation, either version 3 of the License,
   or (at your option) any later version.

   The Midnight Commander is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/** \file logging.c
 *  \brief Source: provides a log file to ease tracing the program
 */

#include <config.h>

#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "compat/utf8proc.h"
#include "tig/options.h"
#include "tig/logging.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static bool logging_initialized = false;
static bool logging_enabled = false;
static char *log_file_path = NULL;

static char *
helper_get_log_filename (void)
{
	const char *env_home, *env_cache, *env_path;
	char *dyn_path;
        char buf[PATH_MAX+1] = {0};

        /* From env var */
	env_path = getenv ("TIG_LOG_FILE");
	if (env_path != NULL)
	    return strdup (env_path);

        /* From tigrc */
        if (opt_log_file_path && *opt_log_file_path)
            return strdup (opt_log_file_path);

        /* Construct ~/.cache/tig/tig.log path using XDG env vars */
        env_home = getenv("HOME");
        if (env_home == NULL)
        {
            env_home = "/tmp";
            env_cache = ".";
        }
        else
        {
            env_cache = getenv("XDG_CACHE_DIR");
            if (env_cache == NULL)
                env_cache = ".cache";
        }
        snprintf(buf,PATH_MAX,"%s/%s/tig", env_home, env_cache);
        mkdir(buf, 0775);
        snprintf(buf,PATH_MAX,"%s/%s/tig/tig.log", env_home, env_cache);
	return strdup(buf);
}

static void
tig_init_logging(void)
{
    logging_initialized=true;
    log_file_path=helper_get_log_filename();
}

static int
is_logging_enabled_from_env (void)
{
	const char *env_is_enabled;

	env_is_enabled = getenv ("TIG_LOG_ENABLE");
	if (env_is_enabled == NULL)
		return false;

	return env_is_enabled[0] != '0';
}

static int
is_logging_enabled (void)
{
	if (logging_initialized)
		return logging_enabled;

	logging_enabled |= (is_logging_enabled_from_env ());
	logging_enabled |= opt_logging_enabled;
	/* Initialize GLib logging */
	tig_init_logging();

	return logging_enabled;
}

static bool
tig_log_writer (int log_level, const char *domain, const char *format, va_list ap)
{
        int ret = false;
	char *logfilename;
	char msg[801] = {0};
	FILE *f = NULL;

        if(!is_logging_enabled())
            return ret;

	logfilename = log_file_path;
	if (logfilename == NULL || logfilename[0] == '\0')
		return ret;

        vsnprintf(msg, 800, format, ap);

	if (msg != NULL && msg[0] != '\0')
		f = fopen (logfilename, "a");

	if (f != NULL)
	{
                ret |= fprintf (f, "[%s] ", domain);
		ret |= fputs (msg, f) > 0;
		ret &= fputs ("\n", f) > 0;
		ret &= fclose (f) == 0;
		ret = true;
	}
	return ret;
}




/*** public functions ****************************************************************************/

char *
tig_log_wrapper (int type, const char *lptr, const char *ptr, ...)
{
	char *fname;
        char fmt_ex[800];
	char *dyn_ptr = NULL;
	va_list va;
	const char *domain = "TIG";
	int log_level = MSG_MSG;
	char *fmt = NULL, *ret_val = NULL;
	if (!is_logging_enabled ())
		return ret_val;

	va_start(va, ptr);

	if (type == 1)
		fmt = va_arg(va, char *);
	else if (type == 2) {
		log_level = va_arg(va, int);
		fmt = va_arg(va, char *);
	}
	/*
	 * Cases:
	 * – mclog("HI", "A message :)"),
	 * – mclog("HI", "A message %s", ":)");
	 * – mclog("A message %s", ":)");
	 */
	else if (type == 3)
	{
		/*
		 * Is there a domain next? I.e.: a string without
		 * any % in it? (Note: the case for mclog("Hi!") is
		 * being handled at type == 1).
		 */
		const char *arg = va_arg(va, char*);
		if (!strchr(arg, '%')) {
			/* Yes – obtain also log level and format string */
			domain = arg;
			log_level = va_arg(va, int);
			fmt = va_arg(va, char*);
		} else {
			/* No – first argument is format string */
			fmt = (char *) arg;
		}
	}
	else if (type == 8)
	{
	}
        dyn_ptr = strdup(ptr);
	fname = basename(dyn_ptr);
	snprintf(fmt_ex,800,"%s:%s « %s", fname, lptr, fmt);

	/*
	 * EMIT MESSAGE
	 * This routes it to writer function (tig_log_writer);
	 */
	tig_log_writer (log_level, domain, fmt_ex, va);

pureup_return:
	if (type == 5)
		free(fmt);
        free(dyn_ptr);
	va_end (va);
	return ret_val;
}



#undef tig_always_log

void
tig_always_log (const char *domain, const char *fmt, ...)
{
	va_list args;

	va_start (args, fmt);
	tig_log_writer (MSG_ERR, domain, fmt, args);
	va_end (args);
}


