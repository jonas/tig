AC_INIT([tig], [0],
	[Jonas Fonseca <fonseca@diku.dk>],
	[tig])
AC_LANG([C])
AC_CONFIG_HEADER(config.h)
AC_CONFIG_SRCDIR(tig.c)

AC_ARG_WITH(libiconv,
        AC_HELP_STRING([--with-libiconv=DIRECTORY],[base directory for libiconv]))
if test "$with_libiconv" != ""
then
	CFLAGS="$CFLAGS -I$with_libiconv/include"
	LDFLAGS="$LDFLAGS -L$with_libiconv/lib"
fi

dnl
dnl See if we need to link with -liconv to get the iconv() function.
dnl
AC_SEARCH_LIBS([wclear], [ncurses curses])
AC_SEARCH_LIBS([iconv], [iconv])

if test "$ac_cv_search_iconv" = "no"
then
	AC_MSG_FAILURE([iconv() not found. Please install libiconv.],[1])
fi

dnl
dnl See if iconv() requires a const char ** for the input buffer.
dnl
if test "$GCC" = "yes"
then
	OLD_CFLAGS="$CFLAGS"
	CFLAGS="$CFLAGS -Werror"
	AC_MSG_CHECKING([whether iconv needs const char **])
	AC_COMPILE_IFELSE(
		[AC_LANG_PROGRAM([[#include <iconv.h>]],
				 [[char **buf;
				   size_t *size;
				   iconv_t cd;
				   iconv(cd, buf, size, buf, size);]])],
		[AC_DEFINE([ICONV_INBUF_TYPE],[char *],
			   [Type of iconv() input buffer])
		 AC_MSG_RESULT([no])],
		[AC_DEFINE([ICONV_INBUF_TYPE],[const char *],
			   [Type of iconv() input buffer])
		 AC_MSG_RESULT([yes])])
	CFLAGS="$OLD_CFLAGS"
fi

AC_PROG_CC

AC_CONFIG_FILES([config.make])
AC_OUTPUT
