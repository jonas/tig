/** \file logging.h
 *  \brief Header: provides a log file to ease tracing the program
 */

#ifndef TIG_LOGGING_H
#define TIG_LOGGING_H

#include <config.h>

/*
   This file provides an easy-to-use function for writing all kinds of
   events into a central log file that can be used for debugging.
 */

#define MSG_INV   7
#define MSG_INF   5
#define MSG_MSG   3
#define MSG_WARN  1
#define MSG_CRI   0
#define MSG_ERR   -1

#define STRINGIZE(x) #x
#define STRING(x) STRINGIZE(x)

#define _log_1(_1)  tig_log_wrapper (1, STRING(__LINE__), __FILE__, _1) // TODO
#define _log_23(_1, ...)  tig_log_wrapper( \
				_Generic((_1), int: 2, \
								char *: 3, \
								const char *: 3), \
				STRING(__LINE__), __FILE__, _1, ##__VA_ARGS__)

#define _log_N(_15, _14, _13, _12, _11, _10, _9, _8, _7, _6,_5,_4,_3,_2,_1,N,...)  _log_##N

/* The end-user log macro */
#define tiglog(...)  _log_N(__VA_ARGS__,23,23,23,23,23,23,23,23,23,23,23,23,23,23,1)(__VA_ARGS__)

/* tig_always_log will have correctness of formats checked */
#define PRINTF( format_idx, arg_idx )    \
  __attribute__((__format__ (__printf__, format_idx, arg_idx)))

void tig_always_log (const char *domain, const char *fmt, ...) PRINTF (2, 3);

#define tiglog_mark() tiglog("%s:%i",__FILE__,__LINE__)

/* Backend implementation function */
char * tig_log_wrapper (int type, const char *lptr, const char *ptr,  ...);

#endif
