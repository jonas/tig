AC_INIT([tig],[0],[jonas.fonseca@gmail.com],[tig])

AC_LANG([C])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_SRCDIR(src)

dnl  Document where we keep our .m4 file.
AC_CONFIG_MACRO_DIR([tools])
dnl  Make sure aclocal actually found it!
m4_pattern_forbid([^AX_])

AC_PROG_CC

AC_CHECK_HEADERS([execinfo.h paths.h stdint.h stdlib.h string.h sys/time.h unistd.h wordexp.h])
AC_CHECK_FUNCS([gettimeofday])
AC_CHECK_DECLS([environ])
AC_CHECK_DECLS([errno], [], [], [#include <errno.h>])

dnl Checks for compatibility flags
AC_CHECK_FUNCS([mkstemps], [AC_SUBST([NO_MKSTEMPS], ["#"])])
AC_CHECK_FUNCS([setenv], [AC_SUBST([NO_SETENV], ["#"])])
AC_CHECK_FUNCS([strndup], [AC_SUBST([NO_STRNDUP], ["#"])])
AC_CHECK_FUNCS([wordexp], [AC_SUBST([NO_WORDEXP], ["#"])])

AX_WITH_CURSES
case "$ax_cv_ncurses" in "no")
	AC_MSG_ERROR([ncurses not found])
esac
case "$ax_cv_ncursesw" in "no")
	AC_MSG_WARN([The found ncurses library does not support wide-char.])
	AC_MSG_WARN([This means that tig will not correctly render UTF-8.])
esac
AC_SUBST(CURSES_LIBS)

AX_LIB_READLINE(6.3)

AM_ICONV

dnl Check for PCRE (Perl Compatible Regular Expressions) library
AC_ARG_WITH(pcre, [AS_HELP_STRING([--without-pcre], [do not use PCRE (Perl Compatible Regular Expressions) library])])
AS_IF([test "x$with_pcre" != xno], [
	AC_CHECK_HEADERS([pcre2posix.h])
	AS_IF([test "x$ac_cv_header_pcre2posix_h" = xyes], [
		AC_CHECK_LIB([pcre2-posix], [regexec], [
			AC_DEFINE([HAVE_PCRE2], [1], [Define if you have PCRE2])
			AC_DEFINE([PCRE2_CODE_UNIT_WIDTH], [8], [ ])
			LIBS="$LIBS -lpcre2-posix -lpcre2-8"
		])
	])
	AS_IF([test "x$ac_cv_lib_pcre2_posix_regexec" != xyes], [
		AC_CHECK_HEADERS([pcreposix.h])
		AS_IF([test "x$ac_cv_header_pcreposix_h" = xyes], [
			AC_CHECK_LIB([pcreposix], [regexec], [
				AC_DEFINE([HAVE_PCRE], [1], [Define if you have PCRE])
				LIBS="$LIBS -lpcreposix -lpcre"
			])
		])
	])
])

dnl OS-specific
case $(uname -s 2>/dev/null || echo unknown) in "OS400")
	AC_CHECK_LIB(util, main, [LIBS="$LIBS -lutil"], AC_MSG_ERROR([Please install the libutil-devel package]))
	;;
esac

AC_CHECK_PROGS(SED, [gsed], [sed])
AC_TDD_GCOV
AC_SUBST(COVERAGE_CFLAGS)
AC_SUBST(COVERAGE_LDFLAGS)

AC_CHECK_PROGS(ASCIIDOC, [asciidoc], [false])
AC_CHECK_PROGS(XMLTO, [xmlto], [false])
AC_CHECK_PROGS(DOCBOOK2PDF, [docbook2pdf], [false])

AC_CONFIG_FILES([config.make])
AC_OUTPUT

dnl Local Variables:
dnl eval: (font-lock-add-keywords nil '(("\\_<AX_\\sw+" . font-lock-keyword-face)))
dnl End:
