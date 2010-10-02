AC_INIT([tig], [0], [Jonas Fonseca <fonseca@diku.dk>], [tig])

AC_LANG([C])
AC_CONFIG_HEADER(config.h)
AC_CONFIG_SRCDIR(tig.c)

cursed=no
AC_CHECK_HEADERS([ncursesw/ncurses.h],
		 [AC_SEARCH_LIBS([initscr], [ncursesw], [cursed=yes])])
case "$cursed" in "no")
	AC_CHECK_HEADERS([ncurses/ncurses.h ncurses.h],
			 [AC_SEARCH_LIBS([wclear], [ncurses], [cursed=yes])])

	case "$cursed" in "no")
		AC_ERROR([ncurses not found])
	esac

	AC_MSG_WARN([The found ncurses library does not support wide-char.])
	AC_MSG_WARN([This means that tig will not correctly render UTF-8.])
esac

AM_ICONV

AC_PROG_CC

AC_CHECK_PROGS(ASCIIDOC, [asciidoc], [false])
AC_CHECK_PROGS(XMLTO, [xmlto], [false])
AC_CHECK_PROGS(DOCBOOK2PDF, [docbook2pdf], [false])

AC_CONFIG_FILES([config.make])
AC_OUTPUT
