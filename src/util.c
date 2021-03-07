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
static bool status_success_message = false;

const char *
get_status_message(enum status_code code)
{
	if (code == SUCCESS) {
		const char *message = status_success_message ? status_custom_message : "";

		status_success_message = false;
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

	FORMAT_BUFFER(status_custom_message, sizeof(status_custom_message), msg, retval, true);
	status_success_message = false;

	return ERROR_CUSTOM_MESSAGE;
}

enum status_code
success(const char *msg, ...)
{
	int retval;

	FORMAT_BUFFER(status_custom_message, sizeof(status_custom_message), msg, retval, true);
	status_success_message = true;

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
time_now(struct timeval *timeval, struct timezone *tz)
{
	static bool check_env = true;

	if (check_env) {
		const char *time;

		if ((time = getenv("TEST_TIME_NOW"))) {
			memset(timeval, 0, sizeof(*timeval));
			if (tz)
				memset(tz, 0, sizeof(*tz));
			timeval->tv_sec = atoi(time);
			return 0;
		}

		check_env = false;
	}

	return gettimeofday(timeval, tz);
}

int
timecmp(const struct time *t1, const struct time *t2)
{
	return t1->sec - t2->sec;
}

struct reldate {
	const char *name;
	const char compact_symbol;
	int in_seconds, interval;
};

static const struct reldate reldate[] = {
	{ "second", 's', 1,			 60 * 2 },
	{ "minute", 'm', 60,			 60 * 60 * 2 },
	{ "hour",   'h', 60 * 60,		 60 * 60 * 24 * 2 },
	{ "day",    'D', 60 * 60 * 24,		 60 * 60 * 24 * 7 * 2 },
	{ "week",   'W', 60 * 60 * 24 * 7,	 60 * 60 * 24 * 7 * 5 },
	{ "month",  'M', 60 * 60 * 24 * 30,	 60 * 60 * 24 * 365 },
	{ "year",   'Y', 60 * 60 * 24 * 365,  0 },
};

static const char *
get_relative_date(const struct time *time, char *buf, size_t buflen, bool compact)
{
	struct timeval now;
	time_t timestamp = time->sec + time->tz;
	long long seconds;
	int i;

	if (time_now(&now, NULL))
		return "";

	seconds = now.tv_sec < timestamp ? (long long) difftime(timestamp, now.tv_sec) : (long long) difftime(now.tv_sec, timestamp);

	for (i = 0; i < ARRAY_SIZE(reldate); i++) {
		if (seconds >= reldate[i].interval && reldate[i].interval)
			continue;

		seconds /= reldate[i].in_seconds;
		if (compact) {
			if (!string_nformat(buf, buflen, NULL, "%s%lld%c",
				    now.tv_sec >= timestamp ? "" : "-",
				    seconds, reldate[i].compact_symbol))
				return "";

		} else if (!string_nformat(buf, buflen, NULL, "%lld %s%s %s",
				    seconds, reldate[i].name,
				    seconds > 1 ? "s" : "",
				    now.tv_sec >= timestamp ? "ago" : "ahead"))
			return "";

		return buf;
	}

	return "";
}

const char *
mkdate(const struct time *time, enum date date, bool local, const char *custom_format)
{
	static char buf[SIZEOF_STR];
	struct tm tm;
	const char *format;
	bool tz_fmt;

	if (!date || !time || !time->sec)
		return "";

	if (date == DATE_RELATIVE || date == DATE_RELATIVE_COMPACT)
		return get_relative_date(time, buf, sizeof(buf),
					 date == DATE_RELATIVE_COMPACT);

	format = (date == DATE_CUSTOM && custom_format)
	       ? custom_format
	       : local
	       ? "%Y-%m-%d %H:%M"
	       : "%Y-%m-%d %H:%M %z";

	tz_fmt = strstr(format, "%z") || strstr(format, "%Z");

	if (local) {
		time_t timestamp = time->sec + time->tz;

		localtime_r(&timestamp, &tm);
	} else {
		gmtime_r(&time->sec, &tm);
	}

	if (local || (!tz_fmt))
		return !strftime(buf, sizeof(buf), format, &tm) ? NULL : buf;

	{
		char format_buf[SIZEOF_STR];
		char *format_pos = format_buf;
		char *buf_pos = buf;
		size_t buf_size = sizeof(buf);
		int tz = ABS(time->tz);

		string_ncopy(format_buf, format, strlen(format));

		while (*format_pos) {
			char *z_pos = strstr(format_pos, "%z");
			char *Z_pos = strstr(format_pos, "%Z");
			char *tz_pos = (z_pos && Z_pos) ? MIN(z_pos, Z_pos) : MAX(z_pos, Z_pos);
			size_t time_len;

			if (tz_pos)
				*tz_pos = 0;

			time_len = strftime(buf_pos, buf_size, format_pos, &tm);
			if (!time_len)
				return NULL;

			buf_pos += time_len;
			buf_size -= time_len;

			if (!tz_pos)
				break;

			/* Skip the %z format flag and insert the timezone. */
			format_pos = tz_pos + 2;

			if (buf_size < 5)
				return NULL;

			buf_pos[0] = time->tz > 0 ? '-' : '+';
			buf_pos[1] = '0' + (tz / 60 / 60 / 10);
			buf_pos[2] = '0' + (tz / 60 / 60 % 10);
			buf_pos[3] = '0' + (tz / 60 % 60 / 10);
			buf_pos[4] = '0' + (tz / 60 % 60 % 10);
			buf_pos[5] = 0;

			buf_pos += 5;
			buf_size -= 5;
		}

#undef buf_size
	}

	return buf;
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

	return string_format(buf, "%lu", size) ? buf : NULL;
}

const struct ident unknown_ident = {
	"unknown@localhostUnknown",	// key
	"Unknown",			// name
	"unknown@localhost"		// email
};

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

/*
 * Allocation helper.
 */

void *
chunk_allocator(void *mem, size_t type_size, size_t chunk_size, size_t size, size_t increase)
{
	size_t num_chunks = (size + chunk_size - 1) / chunk_size;
	size_t num_chunks_new = (size + increase + chunk_size - 1) / chunk_size;

	if (mem == NULL || num_chunks != num_chunks_new) {
		size_t newsize = num_chunks_new * chunk_size * type_size;
		char *tmp = realloc(mem, newsize);

		if (!tmp)
			return NULL;

		if (num_chunks_new > num_chunks) {
			size_t oldsize = num_chunks * chunk_size * type_size;

			memset(tmp + oldsize, 0, newsize - oldsize);
		}

		return tmp;
	}

	return mem;
}

/* vim: set ts=8 sw=8 noexpandtab: */
