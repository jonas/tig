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

#include "tig.h"
#include "util.h"

static const char *status_messages[] = {
#define STATUS_CODE_MESSAGE(name, msg) msg
	STATUS_CODE_INFO(STATUS_CODE_MESSAGE)
};

const char *
get_status_message(enum status_code code)
{
	if (code == SUCCESS)
		return "";
	return status_messages[code];
}

/* vim: set ts=8 sw=8 noexpandtab: */
