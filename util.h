/* Copyright (c) 2006-2013 Jonas Fonseca <fonseca@diku.dk>
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

#endif
/* vim: set ts=8 sw=8 noexpandtab: */
