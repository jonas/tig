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

#include "tig/tig.h"
#include "tig/types.h"

/*
 * Error handling.
 */

#define STATUS_CODE_INFO(_) \
	_(CUSTOM_MESSAGE, NULL), \
	_(NO_OPTION_VALUE, "No option value"), \
	_(OUT_OF_MEMORY, "Out of memory"), \
	_(FILE_DOES_NOT_EXIST, "File does not exist"), \
	_(UNMATCHED_QUOTATION, "Unmatched quotation"), \

enum status_code {
	SUCCESS,
#define STATUS_CODE_ENUM(name, msg) ERROR_ ## name
	STATUS_CODE_INFO(STATUS_CODE_ENUM)
};

const char *get_status_message(enum status_code code);
enum status_code error(const char *fmt, ...) PRINTF_LIKE(1, 2);

typedef void (*die_fn)(void);
extern die_fn die_callback;
void TIG_NORETURN die(const char *err, ...) PRINTF_LIKE(1, 2);
void warn(const char *msg, ...) PRINTF_LIKE(1, 2);

static inline int
count_digits(unsigned long i)
{
	int digits;

	if (!i)
		return 1;

	for (digits = 0; i; digits++)
		i /= 10;
	return digits;
}

static inline int
apply_step(double step, int value)
{
	if (step >= 1)
		return (int) step;
	value *= step + 0.01;
	return value ? value : 1;
}

/*
 * Git data formatters.
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

/*
 * Allocation helper.
 */

#define DEFINE_ALLOCATOR(name, type, chunk_size)				\
static type *									\
name(type **mem, size_t size, size_t increase)					\
{										\
	size_t num_chunks = (size + chunk_size - 1) / chunk_size;		\
	size_t num_chunks_new = (size + increase + chunk_size - 1) / chunk_size;\
	type *tmp = *mem;							\
										\
	if (mem == NULL || num_chunks != num_chunks_new) {			\
		size_t newsize = num_chunks_new * chunk_size * sizeof(type);	\
										\
		tmp = realloc(tmp, newsize);					\
		if (tmp) {							\
			*mem = tmp;						\
			if (num_chunks_new > num_chunks) {			\
				size_t offset = num_chunks * chunk_size;	\
				size_t oldsize = offset * sizeof(type);		\
										\
				memset(tmp + offset, 0,	newsize - oldsize);	\
			}							\
		}								\
	}									\
										\
	return tmp;								\
}

#endif
/* vim: set ts=8 sw=8 noexpandtab: */
