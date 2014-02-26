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

#ifndef TIG_UTIL_H
#define TIG_UTIL_H

#include "tig.h"
#include "types.h"

/*
 * Error handling.
 */

#define STATUS_CODE_INFO(_) \
	_(INTEGER_VALUE_OUT_OF_BOUND, "Integer value out of bound"), \
	_(INVALID_STEP_VALUE, "Invalid step value"), \
	_(NO_OPTION_VALUE, "No option value"), \
	_(NO_VALUE_ASSIGNED, "No value assigned"), \
	_(OBSOLETE_REQUEST_NAME, "Obsolete request name"), \
	_(OUT_OF_MEMORY, "Out of memory"), \
	_(TOO_MANY_OPTION_ARGUMENTS, "Too many option arguments"), \
	_(FILE_DOES_NOT_EXIST, "File does not exist"), \
	_(UNKNOWN_ATTRIBUTE, "Unknown attribute"), \
	_(UNKNOWN_COLOR, "Unknown color"), \
	_(UNKNOWN_COLOR_NAME, "Unknown color name"), \
	_(UNKNOWN_KEY, "Unknown key"), \
	_(UNKNOWN_KEY_MAP, "Unknown key map"), \
	_(UNKNOWN_OPTION_COMMAND, "Unknown option command"), \
	_(UNKNOWN_REQUEST_NAME, "Unknown request name"), \
	_(UNKNOWN_VARIABLE_NAME, "Unknown variable name"), \
	_(UNMATCHED_QUOTATION, "Unmatched quotation"), \
	_(WRONG_NUMBER_OF_ARGUMENTS, "Wrong number of arguments"), \
	_(HOME_UNRESOLVABLE, "HOME environment variable could not be resolved"),

enum status_code {
	SUCCESS,
#define STATUS_CODE_ENUM(name, msg) ERROR_ ## name
	STATUS_CODE_INFO(STATUS_CODE_ENUM)
};

const char *get_status_message(enum status_code code);

void TIG_NORETURN die(const char *err, ...) PRINTF_LIKE(1, 2);
void warn(const char *msg, ...) PRINTF_LIKE(1, 2);

/*
 * Git data formatters and parsers.
 */

struct time {
	time_t sec;
	int tz;
};

struct ident {
	const char *name;
	const char *email;
};

extern const struct ident unknown_ident;

int timecmp(const struct time *t1, const struct time *t2);
int ident_compare(const struct ident *i1, const struct ident *i2);

const char *mkdate(const struct time *time, enum date date);
const char *mkfilesize(unsigned long size, enum file_size format);
const char *mkauthor(const struct ident *ident, int cols, enum author author);
const char *mkmode(mode_t mode);

#define author_trim(cols) (cols == 0 || cols > 10)

#endif
/* vim: set ts=8 sw=8 noexpandtab: */
