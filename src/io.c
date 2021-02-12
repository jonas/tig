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
#include "tig/io.h"

/*
 * Encoding conversion.
 */

#define ENCODING_SEP	": encoding: "
#define ENCODING_ARG	"--encoding=" ENCODING_UTF8

#define CHARSET_SEP	"; charset="

struct encoding {
	struct encoding *next;
	iconv_t cd;
	char fromcode[1];
};

char encoding_arg[] = ENCODING_ARG;
struct encoding *default_encoding;
static struct encoding *encodings;

struct encoding *
encoding_open(const char *fromcode)
{
	struct encoding *encoding;
	size_t len = strlen(fromcode);

	if (!*fromcode)
		return NULL;

	for (encoding = encodings; encoding; encoding = encoding->next) {
		if (!strcasecmp(encoding->fromcode, fromcode))
			return encoding;
	}

	encoding = calloc(1, sizeof(*encoding) + len);
	strcpy(encoding->fromcode, fromcode);
	encoding->cd = iconv_open(ENCODING_UTF8, fromcode);
	if (encoding->cd == ICONV_NONE) {
		free(encoding);
		return NULL;
	}

	encoding->next = encodings;
	encodings = encoding;

	return encoding;
}

static bool
encoding_convert_string(iconv_t iconv_cd, struct buffer *buf)
{
	static char out_buffer[BUFSIZ * 2];
	ICONV_CONST char *inbuf = buf->data;
	size_t inlen = buf->size + 1;

	char *outbuf = out_buffer;
	size_t outlen = sizeof(out_buffer);

	size_t ret = iconv(iconv_cd, &inbuf, &inlen, &outbuf, &outlen);
	if (ret != (size_t) -1) {
		buf->data = out_buffer;
		buf->size = sizeof(out_buffer) - outlen;
	}

	return (ret != (size_t) -1);
}

bool
encoding_convert(struct encoding *encoding, struct buffer *buf)
{
	return encoding_convert_string(encoding->cd, buf);
}

const char *
encoding_iconv(iconv_t iconv_cd, const char *string, size_t length)
{
	char *instr = strndup(string, length);
	struct buffer buf = { instr, length };
	const char *ret = buf.data && encoding_convert_string(iconv_cd, &buf) ? buf.data : string;

	free(instr);
	return ret == instr ? string : ret;
}

struct encoding *
get_path_encoding(const char *path, struct encoding *default_encoding)
{
	const char *check_attr_argv[] = {
		"git", "check-attr", "encoding", "--", path, NULL
	};
	char buf[SIZEOF_STR];
	char *encoding;

	/* <path>: encoding: <encoding> */

	if (!*path || !io_run_buf(check_attr_argv, buf, sizeof(buf), NULL, false)
	    || !(encoding = strstr(buf, ENCODING_SEP)))
		return default_encoding;

	encoding += STRING_SIZE(ENCODING_SEP);
	if (!strcmp(encoding, ENCODING_UTF8)
	    || !strcmp(encoding, "unspecified")
	    || !strcmp(encoding, "set")) {
		const char *file_argv[] = {
			"file", "--mime", "--", path, NULL
		};

		if (!*path || !io_run_buf(file_argv, buf, sizeof(buf), NULL, false)
		    || !(encoding = strstr(buf, CHARSET_SEP)))
			return default_encoding;

		encoding += STRING_SIZE(CHARSET_SEP);
	}

	return encoding_open(encoding);
}

/*
 * Path manipulation.
 */

bool
path_expand(char *dst, size_t dstlen, const char *src)
{
	if (!src)
		return false;

	if (src[0] == '~') {
		/* constrain wordexp to tilde expansion only */
		const char *ifs = getenv("IFS") ? getenv("IFS") : " \t\n";
		wordexp_t we_result;
		size_t metachar_pos;
		char metachars[SIZEOF_STR];
		char leading[SIZEOF_STR];

		string_format(metachars, "%s%s", "/$|&;<>(){}`", ifs);
		metachar_pos = strcspn(src, metachars);
		if (src[metachar_pos] == '/' || src[metachar_pos] == 0) {
			string_nformat(leading, metachar_pos + 1, NULL, "%s", src);
			if (wordexp(leading, &we_result, WRDE_NOCMD))
				return false;
			string_nformat(dst, dstlen, NULL, "%s%s", we_result.we_wordv[0], src + metachar_pos);
			wordfree(&we_result);
			return true;
		}
	}

	/* else */
	string_ncopy_do(dst, dstlen, src, strlen(src));
	return true;
}

bool
path_search(char *dst, size_t dstlen, const char *query, const char *colon_path, int access_flags)
{
	const char *_colon_path = _PATH_DEFPATH; /* emulate execlp() */
	char test[SIZEOF_STR];
	char elt[SIZEOF_STR];
	size_t elt_len;

	if (!query || !*query)
		return false;

	if (strchr(query, '/')) {
		if (access(query, access_flags))
			return false;
		string_ncopy_do(dst, dstlen, query, strlen(query));
		return true;
	}

	if (colon_path && *colon_path)
		_colon_path = colon_path;

	while (_colon_path && *_colon_path) {
		elt_len = strcspn(_colon_path, ":");
		if (elt_len)
			string_ncopy(elt, _colon_path, elt_len);
		else
			string_ncopy(elt, ".", 1);

		_colon_path += elt_len;
		if (*_colon_path)
			_colon_path += 1;

		string_format(test, "%s/%s", elt, query);
		if (!access(test, access_flags)) {
			string_ncopy_do(dst, dstlen, test, strlen(test));
			return true;
		}
	}
	return false;
}

/*
 * Executing external commands.
 */

static void
io_init(struct io *io)
{
	memset(io, 0, sizeof(*io));
	io->pipe = -1;
}

bool
io_open(struct io *io, const char *fmt, ...)
{
	char name[SIZEOF_STR] = "";
	int retval;

	io_init(io);

	FORMAT_BUFFER(name, sizeof(name), fmt, retval, false);
	if (retval < 0) {
		io->error = ENAMETOOLONG;
		return false;
	}

	io->pipe = *name ? open(name, O_RDONLY) : dup(STDIN_FILENO);
	if (io->pipe == -1)
		io->error = errno;
	return io->pipe != -1;
}

bool
io_kill(struct io *io)
{
	return io->pid == 0 || kill(io->pid, SIGKILL) != -1;
}

bool
io_done(struct io *io)
{
	pid_t pid = io->pid;

	if (io->pipe != -1)
		close(io->pipe);
	free(io->buf);
	io_init(io);

	while (pid > 0) {
		int status = 0;
		pid_t waiting = waitpid(pid, &status, 0);

		if (waiting < 0) {
			if (errno == EINTR)
				continue;
			io->error = errno;
			return false;
		}

		io->status = WIFEXITED(status) ? WEXITSTATUS(status) : 0;

		return waiting == pid &&
		       !WIFSIGNALED(status) &&
		       !io->status;
	}

	return true;
}

static int
open_trace(int devnull, const char *argv[])
{
	static const char *trace_file;

	if (!trace_file) {
		trace_file = getenv("TIG_TRACE");
		if (!trace_file)
			trace_file = "";
	}

	if (*trace_file) {
		int fd = open(trace_file, O_RDWR | O_CREAT | O_APPEND, 0666);
		int i;

		flock(fd, LOCK_EX);
		for (i = 0; argv[i]; i++) {
			if (write(fd, argv[i], strlen(argv[i])) == -1
			    || write(fd, " ", 1) == -1)
				break;
		}
		if (argv[i] || write(fd, "\n", 1) == -1) {
			flock(fd, LOCK_UN);
			close(fd);
			return devnull;
		}

		flock(fd, LOCK_UN);
		return fd;
	}

	return devnull;
}

bool
io_trace(const char *fmt, ...)
{
	static FILE *trace_out; /* Intensionally leaked. */
	va_list args;
	int retval;

	if (!trace_out) {
		const char *trace_file = getenv("TIG_TRACE");

		if (trace_file)
			trace_out = fopen(trace_file, "a");
		if (!trace_out)
			return false;
	}

	va_start(args, fmt);
	retval = vfprintf(trace_out, fmt, args);
	va_end(args);
	fflush(trace_out);

	return retval != -1;
}

bool
io_exec(struct io *io, enum io_type type, const char *dir, char * const env[], const char *argv[], int custom)
{
	int pipefds[2] = { -1, -1 };
	bool read_from_stdin = type == IO_RD && (custom & IO_RD_FORWARD_STDIN);
	bool read_with_stderr = type == IO_RD && (custom & IO_RD_WITH_STDERR);

	io_init(io);

	if (dir && !strcmp(dir, argv[0]))
		return io_open(io, "%s%s", dir, argv[1]);

	if ((type == IO_RD || type == IO_RP || type == IO_WR) && pipe(pipefds) < 0) {
		io->error = errno;
		return false;
	} else if (type == IO_AP) {
		pipefds[1] = custom;
	}

	if ((io->pid = fork())) {
		if (io->pid == -1)
			io->error = errno;
		if (pipefds[!(type == IO_WR)] != -1)
			close(pipefds[!(type == IO_WR)]);
		if (io->pid != -1) {
			io->pipe = pipefds[!!(type == IO_WR)];
			return true;
		}

	} else {
		if (type != IO_FG) {
			int devnull = open("/dev/null", O_RDWR);
			int readfd  = type == IO_WR ? pipefds[0]
				    : type == IO_RP ? custom
				    : devnull;
			int writefd = (type == IO_RD || type == IO_RP || type == IO_AP)
				    ? pipefds[1] : devnull;
			int errorfd = open_trace(devnull, argv);

			/* Inject stdin given on the command line. */
			if (read_from_stdin)
				readfd = dup(STDIN_FILENO);

			dup2(readfd,  STDIN_FILENO);
			dup2(writefd, STDOUT_FILENO);
			if (read_with_stderr)
				dup2(writefd, STDERR_FILENO);
			else
				dup2(errorfd, STDERR_FILENO);

			if (devnull != errorfd)
				close(errorfd);
			close(devnull);
			if (pipefds[0] != -1)
				close(pipefds[0]);
			if (pipefds[1] != -1)
				close(pipefds[1]);
		}

		if (dir && *dir && chdir(dir) == -1)
			_exit(errno);

		if (env) {
			int i;

			for (i = 0; env[i]; i++)
				if (*env[i])
					putenv(env[i]);
		}

		execvp(argv[0], (char *const*) argv);

		close(STDOUT_FILENO);
		_exit(errno);
	}

	if (pipefds[!!(type == IO_WR)] != -1)
		close(pipefds[!!(type == IO_WR)]);
	return false;
}

bool
io_run(struct io *io, enum io_type type, const char *dir, char * const env[], const char *argv[])
{
	return io_exec(io, type, dir, env, argv, 0);
}

static bool
io_complete(enum io_type type, const char **argv, const char *dir, int fd)
{
	struct io io;

	return io_exec(&io, type, dir, NULL, argv, fd) && io_done(&io);
}

bool
io_run_bg(const char **argv, const char *dir)
{
	return io_complete(IO_BG, argv, dir, -1);
}

bool
io_run_fg(const char **argv, const char *dir)
{
	return io_complete(IO_FG, argv, dir, -1);
}

bool
io_run_append(const char **argv, int fd)
{
	return io_complete(IO_AP, argv, NULL, fd);
}

bool
io_eof(struct io *io)
{
	return io->eof;
}

int
io_error(struct io *io)
{
	return io->error;
}

char *
io_strerror(struct io *io)
{
	return strerror(io->error);
}

bool
io_can_read(struct io *io, bool can_block)
{
	struct timeval tv = { 0, 500 };
	fd_set fds;

	FD_ZERO(&fds);
	FD_SET(io->pipe, &fds);

	return select(io->pipe + 1, &fds, NULL, NULL, can_block ? NULL : &tv) > 0;
}

ssize_t
io_read(struct io *io, void *buf, size_t bufsize)
{
	do {
		ssize_t readsize = read(io->pipe, buf, bufsize);

		if (readsize < 0 && (errno == EAGAIN || errno == EINTR))
			continue;
		else if (readsize == -1)
			io->error = errno;
		else if (readsize == 0)
			io->eof = 1;
		return readsize;
	} while (1);
}

char *
io_memchr(struct buffer *buf, char *data, int c)
{
	char *pos;

	if (!buf || data < buf->data || buf->data + buf->size <= data)
		return NULL;

	pos = memchr(data, c, buf->size - (data - buf->data));
	return pos ? pos + 1 : NULL;
}

DEFINE_ALLOCATOR(io_realloc_buf, char, BUFSIZ)

static bool
io_get_line(struct io *io, struct buffer *buf, int c, size_t *lineno, bool can_read, char eol_char)
{
	char *eol;
	ssize_t readsize;

	while (true) {
		if (io->bufsize > 0) {
			eol = memchr(io->bufpos, c, io->bufsize);

			while (eol_char && io->bufpos < eol && eol[-1] == eol_char) {
				if (lineno)
					(*lineno)++;
				eol[-1] = eol[0] = ' ';
				eol = memchr(io->bufpos, c, io->bufsize);
			}
			if (eol) {
				buf->data = io->bufpos;
				buf->size = eol - buf->data;

				*eol = 0;
				io->bufpos = eol + 1;
				io->bufsize -= io->bufpos - buf->data;
				if (lineno)
					(*lineno)++;
				return true;
			}
		}

		if (io_eof(io)) {
			if (io->bufsize) {
				buf->data = io->bufpos;
				buf->size = io->bufsize;

				io->bufpos[io->bufsize] = 0;
				io->bufpos += io->bufsize;
				io->bufsize = 0;
				if (lineno)
					(*lineno)++;
				return true;
			}
			return false;
		}

		if (!can_read)
			return false;

		if (io->bufsize > 0 && io->bufpos > io->buf)
			memmove(io->buf, io->bufpos, io->bufsize);

		if (io->bufalloc == io->bufsize) {
			if (!io_realloc_buf(&io->buf, io->bufalloc, BUFSIZ))
				return false;
			io->bufalloc += BUFSIZ;
		}

		io->bufpos = io->buf;
		readsize = io_read(io, io->buf + io->bufsize, io->bufalloc - io->bufsize);
		if (io_error(io))
			return NULL;
		io->bufsize += readsize;
	}
}

bool
io_get(struct io *io, struct buffer *buf, int c, bool can_read)
{
	return io_get_line(io, buf, c, NULL, can_read, 0);
}

bool
io_write(struct io *io, const void *buf, size_t bufsize)
{
	const char *bytes = buf;
	size_t written = 0;

	while (!io_error(io) && written < bufsize) {
		ssize_t size;

		size = write(io->pipe, bytes + written, bufsize - written);
		if (size < 0 && (errno == EAGAIN || errno == EINTR))
			continue;
		else if (size == -1)
			io->error = errno;
		else
			written += size;
	}

	return written == bufsize;
}

bool
io_printf(struct io *io, const char *fmt, ...)
{
	char buf[SIZEOF_STR] = "";
	int retval;

	FORMAT_BUFFER(buf, sizeof(buf), fmt, retval, false);
	if (retval < 0) {
		io->error = ENAMETOOLONG;
		return false;
	}

	return io_write(io, buf, retval);
}

bool
io_read_buf(struct io *io, char buf[], size_t bufsize, bool allow_empty)
{
	struct buffer result = {0};

	if (io_get(io, &result, '\n', true)) {
		result.data = string_trim(result.data);
		string_ncopy_do(buf, bufsize, result.data, strlen(result.data));
	}

	return io_done(io) && (result.data || allow_empty);
}

bool
io_run_buf(const char **argv, char buf[], size_t bufsize, const char *dir, bool allow_empty)
{
	struct io io;

	return io_run(&io, IO_RD, dir, NULL, argv) && io_read_buf(&io, buf, bufsize, allow_empty);
}

bool
io_from_string(struct io *io, const char *str)
{
	size_t len = strlen(str);

	io_init(io);

	if (!io_realloc_buf(&io->buf, io->bufalloc, len))
		return false;

	io->bufsize = io->bufalloc = len;
	io->bufpos = io->buf;
	io->eof = true;
	strncpy(io->buf, str, len);

	return true;
}

static enum status_code
io_load_file(struct io *io, const char *separators,
	     size_t *lineno, io_read_fn read_property, void *data)
{
	struct buffer buf;
	enum status_code state = SUCCESS;

	while (state == SUCCESS && io_get_line(io, &buf, '\n', lineno, true, '\\')) {
		char *name;
		char *value;
		size_t namelen;
		size_t valuelen;

		name = string_trim(buf.data);
		namelen = strcspn(name, separators);

		if (name[namelen]) {
			name[namelen] = 0;
			value = string_trim(name + namelen + 1);
			valuelen = strlen(value);

		} else {
			value = "";
			valuelen = 0;
		}

		state = read_property(name, namelen, value, valuelen, data);
	}

	if (state == SUCCESS && io_error(io))
		state = error("%s", io_strerror(io));
	io_done(io);

	return state;
}

enum status_code
io_load_span(struct io *io, const char *separators, size_t *lineno,
	     io_read_fn read_property, void *data)
{
	return io_load_file(io, separators, lineno, read_property, data);
}

enum status_code
io_load(struct io *io, const char *separators,
	io_read_fn read_property, void *data)
{
	return io_load_file(io, separators, NULL, read_property, data);
}

enum status_code
io_run_load(struct io *io, const char **argv, const char *separators,
	    io_read_fn read_property, void *data)
{
	if (!io_run(io, IO_RD, NULL, NULL, argv))
		return error("Failed to open IO");
	return io_load(io, separators, read_property, data);
}

bool
io_fprintf(FILE *file, const char *fmt, ...)
{
	va_list args;
	int fmtlen, retval;

	va_start(args, fmt);
	fmtlen = vsnprintf(NULL, 0, fmt, args);
	va_end(args);

	va_start(args, fmt);
	retval = vfprintf(file, fmt, args);
	va_end(args);

	return fmtlen == retval;
}

const char *
get_temp_dir(void)
{
	static const char *tmp;

	if (tmp)
		return tmp;

	if (!tmp)
		tmp = getenv("TMPDIR");
	if (!tmp)
		tmp = getenv("TEMP");
	if (!tmp)
		tmp = getenv("TMP");
	if (!tmp)
		tmp = "/tmp";

	return tmp;
}

/* vim: set ts=8 sw=8 noexpandtab: */
