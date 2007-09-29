AC_INIT([tig], [0],
	[Jonas Fonseca <fonseca@diku.dk>],
	[tig])
AC_LANG([C])
AC_CONFIG_HEADER(config.h)
AC_CONFIG_SRCDIR(tig.c)

AC_SEARCH_LIBS([wclear], [ncurses curses])
AM_ICONV

AC_PROG_CC
AC_CHECK_PROGS(GIT_CONFIG, [git-config git-repo-config])
AC_DEFINE_UNQUOTED(GIT_CONFIG,"$GIT_CONFIG",[git config program])
AC_CHECK_PROGS(ASCIIDOC, [asciidoc false])
AC_CHECK_PROGS(XMLTO, [xmlto false])
AC_CHECK_PROGS(DOCBOOK2PDF, [docbook2pdf false])

AC_CONFIG_FILES([config.make])
AC_OUTPUT
