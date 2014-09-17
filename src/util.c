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

/*
 * Error handling.
 */

static const char *status_messages[] = {
	"Success",
#define STATUS_CODE_MESSAGE(name, msg) msg
	STATUS_CODE_INFO(STATUS_CODE_MESSAGE)
};

static char status_custom_message[SIZEOF_STR];
static bool status_success_message = FALSE;

const char *
get_status_message(enum status_code code)
{
	if (code == SUCCESS) {
		const char *message = status_success_message ? status_custom_message : "";

		status_success_message = FALSE;
		return message;
	}
	if (code == ERROR_CUSTOM_MESSAGE)
		return status_custom_message;
	return status_messages[code];
}

enum status_code
error(const char *msg, ...)
{
	int retval;

	FORMAT_BUFFER(status_custom_message, sizeof(status_custom_message), msg, retval, TRUE);
	status_success_message = FALSE;

	return ERROR_CUSTOM_MESSAGE;
}

enum status_code
success(const char *msg, ...)
{
	int retval;

	FORMAT_BUFFER(status_custom_message, sizeof(status_custom_message), msg, retval, TRUE);
	status_success_message = TRUE;

	return SUCCESS;
}

void
warn(const char *msg, ...)
{
	va_list args;

	va_start(args, msg);
	fputs("tig warning: ", stderr);
	vfprintf(stderr, msg, args);
	fputs("\n", stderr);
	va_end(args);
}

die_fn die_callback = NULL;
void TIG_NORETURN
die(const char *err, ...)
{
	va_list args;

	if (die_callback)
		die_callback();

	va_start(args, err);
	fputs("tig: ", stderr);
	vfprintf(stderr, err, args);
	fputs("\n", stderr);
	va_end(args);

	exit(1);
}

/*
 * Git data formatters and parsers.
 */

int
timecmp(const struct time *t1, const struct time *t2)
{
	return t1->sec - t2->sec;
}

const char *
mkdate(const struct time *time, enum date date)
{
	static char buf[STRING_SIZE("2006-04-29 14:21") + 1];
	static const struct enum_map_entry reldate[] = {
		{ "second", 1,			60 * 2 },
		{ "minute", 60,			60 * 60 * 2 },
		{ "hour",   60 * 60,		60 * 60 * 24 * 2 },
		{ "day",    60 * 60 * 24,	60 * 60 * 24 * 7 * 2 },
		{ "week",   60 * 60 * 24 * 7,	60 * 60 * 24 * 7 * 5 },
		{ "month",  60 * 60 * 24 * 30,	60 * 60 * 24 * 365 },
		{ "year",   60 * 60 * 24 * 365, 0 },
	};
	struct tm tm;
	const char *format;

	if (!date || !time || !time->sec)
		return "";

	if (date == DATE_RELATIVE) {
		struct timeval now;
		time_t date = time->sec + time->tz;
		time_t seconds;
		int i;

		gettimeofday(&now, NULL);
		seconds = now.tv_sec < date ? date - now.tv_sec : now.tv_sec - date;
		for (i = 0; i < ARRAY_SIZE(reldate); i++) {
			if (seconds >= reldate[i].value && reldate[i].value)
				continue;

			seconds /= reldate[i].namelen;
			if (!string_format(buf, "%s%ld %s%s",
					   now.tv_sec >= date ? "-" : "+",
					   seconds, reldate[i].name,
					   seconds > 1 ? "s" : ""))
				break;
			return buf;
		}
	}

	if (date == DATE_LOCAL) {
		time_t date = time->sec + time->tz;
		localtime_r(&date, &tm);
	}
	else {
		gmtime_r(&time->sec, &tm);
	}

	format = date == DATE_SHORT ? "%Y-%m-%d" : "%Y-%m-%d %H:%M";
	return strftime(buf, sizeof(buf), format, &tm) ? buf : NULL;
}

const char *
mkfilesize(unsigned long size, enum file_size format)
{
	static char buf[64 + 1];
	static const char relsize[] = {
		'B', 'K', 'M', 'G', 'T', 'P'
	};

	if (!format)
		return "";

	if (format == FILE_SIZE_UNITS) {
		const char *fmt = "%.0f%c";
		double rsize = size;
		int i;

		for (i = 0; i < ARRAY_SIZE(relsize); i++) {
			if (rsize > 1024.0 && i + 1 < ARRAY_SIZE(relsize)) {
				rsize /= 1024;
				continue;
			}

			size = rsize * 10;
			if (size % 10 > 0)
				fmt = "%.1f%c";

			return string_format(buf, fmt, rsize, relsize[i])
				? buf : NULL;
		}
	}

	return string_format(buf, "%ld", size) ? buf : NULL;
}

const struct ident unknown_ident = { "Unknown", "unknown@localhost" };

int
ident_compare(const struct ident *i1, const struct ident *i2)
{
	if (!i1 || !i2)
		return (!!i1) - (!!i2);
	if (!i1->name || !i2->name)
		return (!!i1->name) - (!!i2->name);
	return strcmp(i1->name, i2->name);
}

static const char *
get_author_initials(const char *author)
{
	static char initials[256];
	size_t pos = 0;
	const char *end = strchr(author, '\0');

#define is_initial_sep(c) (isspace(c) || ispunct(c) || (c) == '@' || (c) == '-')

	memset(initials, 0, sizeof(initials));
	while (author < end) {
		unsigned char bytes;
		size_t i;

		while (author < end && is_initial_sep(*author))
			author++;

		bytes = utf8_char_length(author);
		if (bytes >= sizeof(initials) - 1 - pos)
			break;
		while (bytes--) {
			initials[pos++] = *author++;
		}

		i = pos;
		while (author < end && !is_initial_sep(*author)) {
			bytes = utf8_char_length(author);
			if (bytes >= sizeof(initials) - 1 - i) {
				while (author < end && !is_initial_sep(*author))
					author++;
				break;
			}
			while (bytes--) {
				initials[i++] = *author++;
			}
		}

		initials[i++] = 0;
	}

	return initials;
}

static const char *
get_email_user(const char *email)
{
	static char user[SIZEOF_STR + 1];
	const char *end = strchr(email, '@');
	int length = end ? end - email : strlen(email);

	string_format(user, "%.*s%c", length, email, 0);
	return user;
}

const char *
mkauthor(const struct ident *ident, int cols, enum author author)
{
	bool trim = author_trim(cols);
	bool abbreviate = author == AUTHOR_ABBREVIATED || !trim;

	if (author == AUTHOR_NO || !ident)
		return "";
	if (author == AUTHOR_EMAIL && ident->email)
		return ident->email;
	if (author == AUTHOR_EMAIL_USER && ident->email)
		return get_email_user(ident->email);
	if (abbreviate && ident->name)
		return get_author_initials(ident->name);
	return ident->name;
}

const char *
mkmode(mode_t mode)
{
	if (S_ISDIR(mode))
		return "drwxr-xr-x";
	else if (S_ISLNK(mode))
		return "lrwxrwxrwx";
	else if (S_ISGITLINK(mode))
		return "m---------";
	else if (S_ISREG(mode) && mode & S_IXUSR)
		return "-rwxr-xr-x";
	else if (S_ISREG(mode))
		return "-rw-r--r--";
	else
		return "----------";
}

const char *
mkstatus(const char status, enum status_label label)
{
	static char default_label[] = { '?', 0 };
	static const char *labels[][2] = {
		{ "!", "ignored" },
		{ "?", "untracked" },
		{ "A", "added" },
		{ "C", "copied" },
		{ "D", "deleted" },
		{ "M", "modified" },
		{ "R", "renamed" },
		{ "U", "unmerged" },
	};
	int i;

	if (label == STATUS_LABEL_NO)
		return "";

	for (i = 0; i < ARRAY_SIZE(labels); i++) {
		if (status == *labels[i][0]) {
			if (label == STATUS_LABEL_LONG)
				return labels[i][1];
			else
				return labels[i][0];
		}
	}

	default_label[0] = status;
	return default_label;
}

/* vim: set ts=8 sw=8 noexpandtab: */
