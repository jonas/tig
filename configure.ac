AC_INIT([tig], [0],
	[Jonas Fonseca <fonseca@diku.dk>],
	[tig])

AC_LANG([C])
AC_CONFIG_HEADER(config.h)
AC_CONFIG_SRCDIR(tig.c)

AC_SEARCH_LIBS([wclear], [ncursesw ncurses curses], [],
	       [AC_ERROR([curses not found])])

AM_ICONV

AC_PROG_CC

AC_CHECK_PROG(GIT, [git], [git], [AC_ERROR([git not found])])
AC_MSG_CHECKING([which config command git supports])
GIT_CONFIG="git repo-config"
git config --list >/dev/null && GIT_CONFIG="git config"
AC_MSG_RESULT([$GIT_CONFIG])
AC_DEFINE_UNQUOTED(GIT_CONFIG,"$GIT_CONFIG",[git config program])

AC_CHECK_PROGS(ASCIIDOC, [asciidoc false])
AC_CHECK_PROGS(XMLTO, [xmlto false])
AC_CHECK_PROGS(DOCBOOK2PDF, [docbook2pdf false])

AC_CONFIG_FILES([config.make])
AC_OUTPUT

case "$LIBS" in
*-lncursesw*) ;;
*) AC_MSG_RESULT([NOTE: The found ncurses library does not support wide-char.])
   AC_MSG_RESULT([NOTE: This means that tig will not correctly render UTF-8])
esac
