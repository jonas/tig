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
#include "io.h"

bool
argv_to_string(const char *argv[SIZEOF_ARG], char *buf, size_t buflen, const char *sep)
{
	size_t bufpos, argc;

	for (bufpos = 0, argc = 0; argv[argc]; argc++)
		if (!string_nformat(buf, buflen, &bufpos, "%s%s",
				argc ? sep : "", argv[argc]))
			return FALSE;

	return TRUE;
}

static inline int
get_arg_valuelen(const char *arg, char *quoted)
{
	if (*arg == '"' || *arg == '\'') {
		const char *end = *arg == '"' ? "\"" : "'";
		int valuelen = strcspn(arg + 1, end);

		if (quoted)
			*quoted = *arg;
		return valuelen > 0 ? valuelen + 2 : strlen(arg);
	} else {
		if (quoted)
			*quoted = 0;
		return strcspn(arg, " \t");
	}
}

static bool
split_argv_string(const char *argv[SIZEOF_ARG], int *argc, char *cmd, bool remove_quotes)
{
	while (*cmd && *argc < SIZEOF_ARG) {
		char quoted = 0;
		int valuelen = get_arg_valuelen(cmd, &quoted);
		bool advance = cmd[valuelen] != 0;
		int quote_offset = !!(quoted && remove_quotes);

		cmd[valuelen - quote_offset] = 0;
		argv[(*argc)++] = chomp_string(cmd + quote_offset);
		cmd = chomp_string(cmd + valuelen + advance);
	}

	if (*argc < SIZEOF_ARG)
		argv[*argc] = NULL;
	return *argc < SIZEOF_ARG;
}

bool
argv_from_string_no_quotes(const char *argv[SIZEOF_ARG], int *argc, char *cmd)
{
	return split_argv_string(argv, argc, cmd, TRUE);
}

bool
argv_from_string(const char *argv[SIZEOF_ARG], int *argc, char *cmd)
{
	return split_argv_string(argv, argc, cmd, FALSE);
}

bool
argv_from_env(const char **argv, const char *name)
{
	char *env = argv ? getenv(name) : NULL;
	int argc = 0;

	if (env && *env)
		env = strdup(env);
	return !env || argv_from_string(argv, &argc, env);
}

void
argv_free(const char *argv[])
{
	int argc;

	if (!argv)
		return;
	for (argc = 0; argv[argc]; argc++)
		free((void *) argv[argc]);
	argv[0] = NULL;
}

size_t
argv_size(const char **argv)
{
	int argc = 0;

	while (argv && argv[argc])
		argc++;

	return argc;
}

bool
argv_contains(const char **argv, const char *arg)
{
	int i;

	for (i = 0; argv && argv[i]; i++)
		if (!strcmp(argv[i], arg))
			return TRUE;
	return FALSE;
}

DEFINE_ALLOCATOR(argv_realloc, const char *, SIZEOF_ARG)

bool
argv_append(const char ***argv, const char *arg)
{
	size_t argc = argv_size(*argv);
	char *alloc;

	if (!*arg && argc > 0)
		return TRUE;

	if (!argv_realloc(argv, argc, 2))
		return FALSE;

	alloc = strdup(arg);

	(*argv)[argc++] = alloc;
	(*argv)[argc] = NULL;

	return alloc != NULL;
}

bool
argv_append_array(const char ***dst_argv, const char *src_argv[])
{
	int i;

	for (i = 0; src_argv && src_argv[i]; i++)
		if (!argv_append(dst_argv, src_argv[i]))
			return FALSE;
	return TRUE;
}

bool
argv_remove_quotes(const char *argv[])
{
	int argc;

	for (argc = 0; argv[argc]; argc++) {
		char quoted = 0;
		const char *arg = argv[argc];
		int arglen = get_arg_valuelen(arg, &quoted);
		int unquotedlen = arglen - 1 - (arg[arglen - 1] == quoted);
		char *unquoted;

		if (!quoted)
			continue;

		unquoted = malloc(unquotedlen + 1);
		if (!unquoted)
			return FALSE;
		strncpy(unquoted, arg + 1, unquotedlen);
		unquoted[unquotedlen] = 0;
		free((void *) arg);
		argv[argc] = unquoted;
	}

	return TRUE;
}

bool
argv_copy(const char ***dst, const char *src[])
{
	int argc;

	argv_free(*dst);
	for (argc = 0; src[argc]; argc++)
		if (!argv_append(dst, src[argc]))
			return FALSE;
	return TRUE;
}

/*
 * Encoding conversion.
 */

#define ENCODING_SEP	": encoding: "
#define ENCODING_ARG	"--encoding=" ENCODING_UTF8

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
	strncpy(encoding->fromcode, fromcode, len);
	encoding->cd = iconv_open(ENCODING_UTF8, fromcode);
	if (encoding->cd == ICONV_NONE) {
		free(encoding);
		return NULL;
	}

	encoding->next = encodings;
	encodings = encoding;

	return encoding;
}

static char *
encoding_convert_string(iconv_t iconv_cd, char *line)
{
	static char out_buffer[BUFSIZ * 2];
	ICONV_CONST char *inbuf = line;
	size_t inlen = strlen(line) + 1;

	char *outbuf = out_buffer;
	size_t outlen = sizeof(out_buffer);

	size_t ret = iconv(iconv_cd, &inbuf, &inlen, &outbuf, &outlen);

	return (ret != (size_t) -1) ? out_buffer : line;
}

char *
encoding_convert(struct encoding *encoding, char *line)
{
	return encoding_convert_string(encoding->cd, line);
}

const char *
encoding_iconv(iconv_t iconv_cd, const char *string)
{
	char *instr = strdup(string);
	const char *ret = encoding_convert_string(iconv_cd, instr);

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

	if (!*path || !io_run_buf(check_attr_argv, buf, sizeof(buf))
	    || !(encoding = strstr(buf, ENCODING_SEP)))
		return default_encoding;

	encoding += STRING_SIZE(ENCODING_SEP);
	if (!strcmp(encoding, ENCODING_UTF8)
	    || !strcmp(encoding, "unspecified")
	    || !strcmp(encoding, "set"))
		return default_encoding;

	return encoding_open(encoding);
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

	FORMAT_BUFFER(name, sizeof(name), fmt, retval, FALSE);
	if (retval < 0) {
		io->error = ENAMETOOLONG;
		return FALSE;
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
		int status;
		pid_t waiting = waitpid(pid, &status, 0);

		if (waiting < 0) {
			if (errno == EINTR)
				continue;
			io->error = errno;
			return FALSE;
		}

		if (WEXITSTATUS(status)) {
			io->status = WEXITSTATUS(status);
		}

		return waiting == pid &&
		       !WIFSIGNALED(status) &&
		       !io->status;
	}

	return TRUE;
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

		for (i = 0; argv[i]; i++) {
			if (write(fd, argv[i], strlen(argv[i])) == -1
			    || write(fd, " ", 1) == -1)
				break;
		}
		if (argv[i] || write(fd, "\n", 1) == -1) {
			close(fd);
			return devnull;
		}

		return fd;
	}

	return devnull;
}

bool
io_run(struct io *io, enum io_type type, const char *dir, char * const env[], const char *argv[], ...)
{
	int pipefds[2] = { -1, -1 };
	va_list args;
	bool read_from_stdin = type == IO_RD_STDIN;

	io_init(io);

	if (read_from_stdin)
		type = IO_RD;

	if (dir && !strcmp(dir, argv[0]))
		return io_open(io, "%s%s", dir, argv[1]);

	if ((type == IO_RD || type == IO_WR) && pipe(pipefds) < 0) {
		io->error = errno;
		return FALSE;
	} else if (type == IO_AP) {
		va_start(args, argv);
		pipefds[1] = va_arg(args, int);
		va_end(args);
	}

	if ((io->pid = fork())) {
		if (io->pid == -1)
			io->error = errno;
		if (pipefds[!(type == IO_WR)] != -1)
			close(pipefds[!(type == IO_WR)]);
		if (io->pid != -1) {
			io->pipe = pipefds[!!(type == IO_WR)];
			return TRUE;
		}

	} else {
		if (type != IO_FG) {
			int devnull = open("/dev/null", O_RDWR);
			int readfd  = type == IO_WR ? pipefds[0] : devnull;
			int writefd = (type == IO_RD || type == IO_AP)
							? pipefds[1] : devnull;
			int errorfd = open_trace(devnull, argv);

			/* Inject stdin given on the command line. */
			if (read_from_stdin)
				readfd = dup(STDIN_FILENO);

			dup2(readfd,  STDIN_FILENO);
			dup2(writefd, STDOUT_FILENO);
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
			exit(errno);

		if (env) {
			int i;

			for (i = 0; env[i]; i++)
				if (*env[i])
					putenv(env[i]);
		}

		execvp(argv[0], (char *const*) argv);
		exit(errno);
	}

	if (pipefds[!!(type == IO_WR)] != -1)
		close(pipefds[!!(type == IO_WR)]);
	return FALSE;
}

bool
io_complete(enum io_type type, const char **argv, const char *dir, int fd)
{
	struct io io;

	return io_run(&io, type, dir, NULL, argv, fd) && io_done(&io);
}

bool
io_run_bg(const char **argv)
{
	return io_complete(IO_BG, argv, NULL, -1);
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

DEFINE_ALLOCATOR(io_realloc_buf, char, BUFSIZ)

char *
io_get(struct io *io, int c, bool can_read)
{
	char *eol;
	ssize_t readsize;

	while (TRUE) {
		if (io->bufsize > 0) {
			eol = memchr(io->bufpos, c, io->bufsize);
			if (eol) {
				char *line = io->bufpos;

				*eol = 0;
				io->bufpos = eol + 1;
				io->bufsize -= io->bufpos - line;
				return line;
			}
		}

		if (io_eof(io)) {
			if (io->bufsize) {
				io->bufpos[io->bufsize] = 0;
				io->bufsize = 0;
				return io->bufpos;
			}
			return NULL;
		}

		if (!can_read)
			return NULL;

		if (io->bufsize > 0 && io->bufpos > io->buf)
			memmove(io->buf, io->bufpos, io->bufsize);

		if (io->bufalloc == io->bufsize) {
			if (!io_realloc_buf(&io->buf, io->bufalloc, BUFSIZ))
				return NULL;
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
io_write(struct io *io, const void *buf, size_t bufsize)
{
	size_t written = 0;

	while (!io_error(io) && written < bufsize) {
		ssize_t size;

		size = write(io->pipe, buf + written, bufsize - written);
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

	FORMAT_BUFFER(buf, sizeof(buf), fmt, retval, FALSE);
	if (retval < 0) {
		io->error = ENAMETOOLONG;
		return FALSE;
	}

	return io_write(io, buf, retval);
}

bool
io_read_buf(struct io *io, char buf[], size_t bufsize)
{
	char *result = io_get(io, '\n', TRUE);

	if (result) {
		result = chomp_string(result);
		string_ncopy_do(buf, bufsize, result, strlen(result));
	}

	return io_done(io) && result;
}

bool
io_run_buf(const char **argv, char buf[], size_t bufsize)
{
	struct io io;

	return io_run(&io, IO_RD, NULL, NULL, argv) && io_read_buf(&io, buf, bufsize);
}

bool
io_from_string(struct io *io, const char *str)
{
	size_t len = strlen(str);

	io_init(io);

	if (!io_realloc_buf(&io->buf, io->bufalloc, len))
		return FALSE;

	io->bufsize = io->bufalloc = len;
	io->bufpos = io->buf;
	io->eof = TRUE;
	strncpy(io->buf, str, len);

	return TRUE;
}

int
io_load(struct io *io, const char *separators,
	io_read_fn read_property, void *data)
{
	char *name;
	int state = OK;

	while (state == OK && (name = io_get(io, '\n', TRUE))) {
		char *value;
		size_t namelen;
		size_t valuelen;

		name = chomp_string(name);
		namelen = strcspn(name, separators);

		if (name[namelen]) {
			name[namelen] = 0;
			value = chomp_string(name + namelen + 1);
			valuelen = strlen(value);

		} else {
			value = "";
			valuelen = 0;
		}

		state = read_property(name, namelen, value, valuelen, data);
	}

	if (state != ERR && io_error(io))
		state = ERR;
	io_done(io);

	return state;
}

int
io_run_load(const char **argv, const char *separators,
	    io_read_fn read_property, void *data)
{
	struct io io;

	if (!io_run(&io, IO_RD, NULL, NULL, argv))
		return ERR;
	return io_load(&io, separators, read_property, data);
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
