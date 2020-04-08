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

#ifndef TIG_IO_H
#define TIG_IO_H

#include "tig/tig.h"

struct buffer {
	char *data;
	size_t size;
};

/*
 * Encoding conversion.
 */

#define ENCODING_UTF8	"UTF-8"

struct encoding;

struct encoding *encoding_open(const char *fromcode);
bool encoding_convert(struct encoding *encoding, struct buffer *buf);
const char *encoding_iconv(iconv_t iconv_out, const char *string, size_t length);
struct encoding *get_path_encoding(const char *path, struct encoding *default_encoding);

extern char encoding_arg[];
extern struct encoding *default_encoding;

/*
 * Path manipulation.
 */

#ifndef _PATH_DEFPATH
#define _PATH_DEFPATH	"/usr/bin:/bin"
#endif

bool path_expand(char *dst, size_t dstlen, const char *src);
bool path_search(char *dst, size_t dstlen, const char *query, const char *colon_path, int access_flags);

/*
 * Executing external commands.
 */

enum io_flags {
	IO_RD_FORWARD_STDIN = 1 << 0,	/* Forward stdin from parent process to child. */
	IO_RD_WITH_STDERR   = 1 << 1,	/* Redirect stderr to stdin. */
};

enum io_type {
	IO_BG,			/* Execute command in the background. */
	IO_FG,			/* Execute command with same std{in,out,err}. */
	IO_RD,			/* Read only fork+exec IO. */
	IO_RP,			/* Read only fork+exec IO with input pipe. */
	IO_WR,			/* Write only fork+exec IO. */
	IO_AP,			/* Append fork+exec output to file. */
};

struct io {
	int pipe;		/* Pipe end for reading or writing. */
	pid_t pid;		/* PID of spawned process. */
	int error;		/* Error status. */
	char *buf;		/* Read buffer. */
	size_t bufalloc;	/* Allocated buffer size. */
	size_t bufsize;		/* Buffer content size. */
	char *bufpos;		/* Current buffer position. */
	unsigned int eof:1;	/* Has end of file been reached. */
	int status:8;		/* Status exit code. */
};

typedef enum status_code (*io_read_fn)(char *, size_t, char *, size_t, void *data);

bool io_open(struct io *io, const char *fmt, ...) PRINTF_LIKE(2, 3);
bool io_from_string(struct io *io, const char *str);
bool io_kill(struct io *io);
bool io_done(struct io *io);
bool io_exec(struct io *io, enum io_type type, const char *dir, char * const env[], const char *argv[], int custom);
bool io_run(struct io *io, enum io_type type, const char *dir, char * const env[], const char *argv[]);
bool io_run_bg(const char **argv, const char *dir);
bool io_run_fg(const char **argv, const char *dir);
bool io_run_append(const char **argv, int fd);
bool io_eof(struct io *io);
int io_error(struct io *io);
char * io_strerror(struct io *io);
bool io_can_read(struct io *io, bool can_block);
ssize_t io_read(struct io *io, void *buf, size_t bufsize);
bool io_get(struct io *io, struct buffer *buf, int c, bool can_read);
bool io_write(struct io *io, const void *buf, size_t bufsize);
bool io_printf(struct io *io, const char *fmt, ...) PRINTF_LIKE(2, 3);
bool io_read_buf(struct io *io, char buf[], size_t bufsize, bool allow_empty);
bool io_run_buf(const char **argv, char buf[], size_t bufsize, const char *dir, bool allow_empty);
enum status_code io_load(struct io *io, const char *separators,
	    io_read_fn read_property, void *data);
enum status_code io_load_span(struct io *io, const char *separators,
	     size_t *lineno, io_read_fn read_property, void *data);
enum status_code io_run_load(struct io *io, const char **argv, const char *separators,
		io_read_fn read_property, void *data);
char *io_memchr(struct buffer *buf, char *data, int c);

const char *get_temp_dir(void);

bool PRINTF_LIKE(2, 3) io_fprintf(FILE *file, const char *fmt, ...);
bool io_trace(const char *fmt, ...);

#endif
/* vim: set ts=8 sw=8 noexpandtab: */
