dnl Copyright (C) 1987-2014 Free Software Foundation, Inc.
dnl
dnl This program is free software: you can redistribute it and/or modify
dnl it under the terms of the GNU General Public License as published by
dnl the Free Software Foundation, either version 3 of the License, or
dnl (at your option) any later version.
dnl
dnl This program is distributed in the hope that it will be useful,
dnl but WITHOUT ANY WARRANTY; without even the implied warranty of
dnl MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
dnl GNU General Public License for more details.
dnl
dnl You should have received a copy of the GNU General Public License
dnl along with this program.  If not, see <http://www.gnu.org/licenses/>.
dnl
dnl Taken from http://git.savannah.gnu.org/cgit/readline.git/tree/examples/autoconf/
dnl commit 835a39225c6bd4784c0d7f775b0cd44dd7c57f35 (Readline 6.3, version 2.73)
dnl
dnl Configuration of --with-readline and result check at EOF.

AC_DEFUN([BASH_CHECK_LIB_TERMCAP],
[
# save cpp and ld options
_save_CFLAGS="$CFLAGS"
_save_LDFLAGS="$LDFLAGS"
_save_LIBS="$LIBS"

if test "X$bash_cv_termcap_lib" = "X"; then
_bash_needmsg=yes
else
AC_MSG_CHECKING(which library has the termcap functions)
_bash_needmsg=
fi
AC_CACHE_VAL(bash_cv_termcap_lib,
[AC_CHECK_FUNC(tgetent, bash_cv_termcap_lib=libc,
if test "$ax_cv_curses_which" = "ncursesw"; then
	[AC_CHECK_LIB(ncursesw, tgetent, bash_cv_termcap_lib=libncursesw,
		[AC_CHECK_LIB(tinfow, tgetent, bash_cv_termcap_lib=libtinfow)]
	)]
elif test "$ax_cv_curses_which" = "ncurses"; then
	[AC_CHECK_LIB(ncurses, tgetent, bash_cv_termcap_lib=libncurses,
		[AC_CHECK_LIB(tinfo, tgetent, bash_cv_termcap_lib=libtinfo)]
	)]
elif test "$ax_cv_curses_which" = "plaincurses"; then
	[AC_CHECK_LIB(curses, tgetent, bash_cv_termcap_lib=libcurses)]
else
	[AC_CHECK_LIB(termcap, tgetent, bash_cv_termcap_lib=libtermcap,
		bash_cv_termcap_lib=gnutermcap
	)]
fi
)])
if test "X$_bash_needmsg" = "Xyes"; then
AC_MSG_CHECKING(which library has the termcap functions)
fi
AC_MSG_RESULT(using $bash_cv_termcap_lib)
if test X$bash_cv_termcap_lib = Xgnutermcap && test -z "$prefer_curses"; then
LDFLAGS="$LDFLAGS -L./lib/termcap"
TERMCAP_LIB="./lib/termcap/libtermcap.a"
TERMCAP_DEP="./lib/termcap/libtermcap.a"
elif test X$bash_cv_termcap_lib = Xlibtermcap && test -z "$prefer_curses"; then
TERMCAP_LIB=-ltermcap
TERMCAP_DEP=
elif test X$bash_cv_termcap_lib = Xlibtinfow; then
TERMCAP_LIB=-ltinfow
TERMCAP_DEP=
elif test X$bash_cv_termcap_lib = Xlibtinfo; then
TERMCAP_LIB=-ltinfo
TERMCAP_DEP=
elif test X$bash_cv_termcap_lib = Xlibncursesw; then
TERMCAP_LIB=-lncursesw
TERMCAP_DEP=
elif test X$bash_cv_termcap_lib = Xlibncurses; then
TERMCAP_LIB=-lncurses
TERMCAP_DEP=
elif test X$bash_cv_termcap_lib = Xlibc; then
TERMCAP_LIB=
TERMCAP_DEP=
else
TERMCAP_LIB=-lcurses
TERMCAP_DEP=
fi

CFLAGS="$_save_CFLAGS"
LDFLAGS="$_save_LDFLAGS"
LIBS="$_save_LIBS"
])

AC_DEFUN([RL_LIB_READLINE_VERSION],
[
AC_REQUIRE([BASH_CHECK_LIB_TERMCAP])

AC_MSG_CHECKING([version of installed readline library])

# What a pain in the ass this is.

# save cpp and ld options
_save_CFLAGS="$CFLAGS"
_save_LDFLAGS="$LDFLAGS"
_save_LIBS="$LIBS"

# Don't set ac_cv_rl_prefix if the caller has already assigned a value.  This
# allows the caller to do something like $_rl_prefix=$withval if the user
# specifies --with-installed-readline=PREFIX as an argument to configure

if test -z "$ac_cv_rl_prefix"; then
test "x$prefix" = xNONE && ac_cv_rl_prefix=$ac_default_prefix || ac_cv_rl_prefix=${prefix}
fi

eval ac_cv_rl_includedir=${ac_cv_rl_prefix}/include
eval ac_cv_rl_libdir=${ac_cv_rl_prefix}/lib

LIBS="$LIBS -lreadline ${TERMCAP_LIB}"
CFLAGS="$CFLAGS -I${ac_cv_rl_includedir}"
LDFLAGS="$LDFLAGS -L${ac_cv_rl_libdir}"

AC_CACHE_VAL(ac_cv_rl_version,
[AC_RUN_IFELSE([AC_LANG_SOURCE([[
#include <stdio.h>
#include <readline/readline.h>

extern int rl_gnu_readline_p;

main()
{
	FILE *fp;
	fp = fopen("conftest.rlv", "w");
	if (fp == 0)
		exit(1);
	if (rl_gnu_readline_p != 1)
		fprintf(fp, "0.0\n");
	else
		fprintf(fp, "%s\n", rl_library_version ? rl_library_version : "0.0");
	fclose(fp);
	exit(0);
}
]])],
ac_cv_rl_version=`cat conftest.rlv`,
ac_cv_rl_version='0.0',
ac_cv_rl_version='4.2')])

CFLAGS="$_save_CFLAGS"
LDFLAGS="$_save_LDFLAGS"
LIBS="$_save_LIBS"

RL_MAJOR=0
RL_MINOR=0

# (
case "$ac_cv_rl_version" in
2*|3*|4*|5*|6*|7*|8*|9*)
	RL_MAJOR=`echo $ac_cv_rl_version | sed 's:\..*$::'`
	RL_MINOR=`echo $ac_cv_rl_version | sed -e 's:^.*\.::' -e 's:[[a-zA-Z]]*$::'`
	;;
esac

# (((
case $RL_MAJOR in
[[0-9][0-9]])	_RL_MAJOR=$RL_MAJOR ;;
[[0-9]])	_RL_MAJOR=0$RL_MAJOR ;;
*)		_RL_MAJOR=00 ;;
esac

# (((
case $RL_MINOR in
[[0-9][0-9]])	_RL_MINOR=$RL_MINOR ;;
[[0-9]])	_RL_MINOR=0$RL_MINOR ;;
*)		_RL_MINOR=00 ;;
esac

RL_VERSION="0x${_RL_MAJOR}${_RL_MINOR}"

# Readline versions greater than 4.2 have these defines in readline.h

if test $ac_cv_rl_version = '0.0' ; then
	AC_MSG_RESULT([none])
	AC_MSG_WARN([Could not test version of installed readline library.])
elif test $RL_MAJOR -gt 4 || { test $RL_MAJOR = 4 && test $RL_MINOR -gt 2 ; } ; then
	# set these for use by the caller
	RL_PREFIX=$ac_cv_rl_prefix
	RL_LIBDIR=$ac_cv_rl_libdir
	RL_INCLUDEDIR=$ac_cv_rl_includedir
	AC_MSG_RESULT($ac_cv_rl_version)
else

AC_DEFINE_UNQUOTED(RL_READLINE_VERSION, $RL_VERSION, [encoded version of the installed readline library])
AC_DEFINE_UNQUOTED(RL_VERSION_MAJOR, $RL_MAJOR, [major version of installed readline library])
AC_DEFINE_UNQUOTED(RL_VERSION_MINOR, $RL_MINOR, [minor version of installed readline library])

AC_SUBST(RL_VERSION)
AC_SUBST(RL_MAJOR)
AC_SUBST(RL_MINOR)

# set these for use by the caller
RL_PREFIX=$ac_cv_rl_prefix
RL_LIBDIR=$ac_cv_rl_libdir
RL_INCLUDEDIR=$ac_cv_rl_includedir

AC_MSG_RESULT($ac_cv_rl_version)

fi
])

AC_DEFUN([AX_LIB_READLINE], [
  RL_VERSION_REQUIRED="$1"
  RL_MAJOR_REQUIRED="$(echo "$1" | sed -n 's/\([[]0-9[]]*\)[[].[]]\([[]0-9[]]*\)/\1/p')"
  RL_MINOR_REQUIRED="$(echo "$1" | sed -n 's/\([[]0-9[]]*\)[[].[]]\([[]0-9[]]*\)/\2/p')"
  RL_MAJOR=0
  RL_MINOR=0

  AC_ARG_WITH([readline], [AS_HELP_STRING([--with-readline=DIR],
  	[search for readline in DIR/include and DIR/lib])],
	[ac_cv_rl_prefix=$with_readline])

  AS_IF([test "x$with_readline" != xno], [
    AC_CHECK_HEADERS([readline/readline.h], [
      AC_CHECK_HEADERS([readline/history.h], [
        RL_LIB_READLINE_VERSION
      ])
    ])

    if test $RL_MAJOR -gt $RL_MAJOR_REQUIRED || {
       test $RL_MAJOR = $RL_MAJOR_REQUIRED && test $RL_MINOR -ge $RL_MINOR_REQUIRED ; } ; then
      LIBS="$LIBS -lreadline ${TERMCAP_LIB}"
      CFLAGS="$CFLAGS -I${RL_INCLUDEDIR}"
      LDFLAGS="$LDFLAGS -L${RL_LIBDIR}"

      AC_DEFINE(HAVE_READLINE, 1, [Define if you have a GNU readline compatible library])

    elif test -n "$ac_cv_rl_prefix"; then
      AC_MSG_WARN([Minimum required version of readline is $RL_VERSION_REQUIRED])
    fi
  ])
])
