AC_INIT([tig],[0],[Jonas Fonseca <fonseca@diku.dk>],[tig])

AC_LANG([C])
AC_CONFIG_HEADER(config.h)
AC_CONFIG_SRCDIR(tig.c)

dnl  Document where we keep our .m4 file.
AC_CONFIG_MACRO_DIR([contrib])
dnl  Make sure aclocal actually found it!
m4_pattern_forbid([^AX_])

AX_WITH_CURSES
case "$ax_cv_ncurses" in "no")
	AC_MSG_ERROR([ncurses not found])
esac
case "$ax_cv_ncursesw" in "no")
	AC_MSG_WARN([The found ncurses library does not support wide-char.])
	AC_MSG_WARN([This means that tig will not correctly render UTF-8.])
esac
AC_SUBST(CURSES_LIB)

AM_ICONV

AC_PROG_CC

AC_CHECK_PROGS(ASCIIDOC, [asciidoc], [false])
AC_CHECK_PROGS(XMLTO, [xmlto], [false])
AC_CHECK_PROGS(DOCBOOK2PDF, [docbook2pdf], [false])

AC_CONFIG_FILES([config.make])
AC_OUTPUT

dnl Local Variables:
dnl eval: (font-lock-add-keywords nil '(("\\_<AX_\\sw+" . font-lock-keyword-face)))
dnl End:
