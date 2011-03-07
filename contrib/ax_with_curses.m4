# ===========================================================================
#      http://www.gnu.org/software/autoconf-archive/ax_with_curses.html
# ===========================================================================
#
# SYNOPSIS
#
#   AX_WITH_CURSES
#
# DESCRIPTION
#
#   Detect SysV compatible curses, such as ncurses.
#
#   Defines HAVE_CURSES_H or HAVE_NCURSES_H if curses is found. CURSES_LIB
#   is also set with the required library, but is not appended to LIBS
#   automatically. If no working curses library is found CURSES_LIB will be
#   left blank. If CURSES_LIB is set in the environment, the supplied value
#   will be used.
#
#   There are two options: --with-ncurses forces the use of ncurses, and
#   --with-ncursesw forces the use of ncursesw (wide character ncurses). The
#   corresponding options --without-ncurses and --without-ncursesw force
#   those libraries not to be used. By default, ncursesw is preferred to
#   ncurses, which is preferred to plain curses.
#
#   ax_cv_curses is set to "yes" if any curses is found (including
#   ncurses!); ax_cv_ncurses is set to "yes" if any ncurses is found, and
#   ax_cv_ncursesw is set to "yes" if ncursesw is found.
#
# LICENSE
#
#   Copyright (c) 2009 Mark Pulford <mark@kyne.com.au>
#   Copyright (c) 2009 Damian Pietras <daper@daper.net>
#   Copyright (c) 2009 Reuben Thomas <rrt@sc3d.org>
#
#   This program is free software: you can redistribute it and/or modify it
#   under the terms of the GNU General Public License as published by the
#   Free Software Foundation, either version 3 of the License, or (at your
#   option) any later version.
#
#   This program is distributed in the hope that it will be useful, but
#   WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
#   Public License for more details.
#
#   You should have received a copy of the GNU General Public License along
#   with this program. If not, see <http://www.gnu.org/licenses/>.
#
#   As a special exception, the respective Autoconf Macro's copyright owner
#   gives unlimited permission to copy, distribute and modify the configure
#   scripts that are the output of Autoconf when processing the Macro. You
#   need not follow the terms of the GNU General Public License when using
#   or distributing such scripts, even though portions of the text of the
#   Macro appear in them. The GNU General Public License (GPL) does govern
#   all other use of the material that constitutes the Autoconf Macro.
#
#   This special exception to the GPL applies to versions of the Autoconf
#   Macro released by the Autoconf Archive. When you make and distribute a
#   modified version of the Autoconf Macro, you may extend this special
#   exception to the GPL to apply to your modified version as well.

#serial 6

AU_ALIAS([MP_WITH_CURSES], [AX_WITH_CURSES])
AC_DEFUN([AX_WITH_CURSES],
  [AC_ARG_WITH(ncurses, [AS_HELP_STRING([--with-ncurses],
        [Force the use of ncurses over curses])],,)
   ax_save_LIBS="$LIBS"
   AC_ARG_WITH(ncursesw, [AS_HELP_STRING([--without-ncursesw],
        [Don't use ncursesw (wide character support)])],,)
   if test ! "$CURSES_LIB" -a "$with_ncurses" != no -a "$with_ncursesw" != "no"
   then
       AC_CACHE_CHECK([for working ncursesw], ax_cv_ncursesw,
         [LIBS="$ax_save_LIBS -lncursesw"
          AC_TRY_LINK(
            [#include <ncurses.h>],
            [chtype a; int b=A_STANDOUT, c=KEY_LEFT; initscr(); ],
            ax_cv_ncursesw=yes, ax_cv_ncursesw=no)])
       if test "$ax_cv_ncursesw" = yes
       then
         AC_CHECK_HEADER([ncursesw/curses.h], AC_DEFINE(HAVE_NCURSESW_H, 1,
            [Define if you have ncursesw.h]))
         AC_DEFINE(HAVE_NCURSES_H, 1, [Define if you have ncursesw/curses.h])
         AC_DEFINE(HAVE_NCURSESW, 1, [Define if you have libncursesw])
         CURSES_LIB="-lncursesw"
         ax_cv_ncurses=yes
         ax_cv_curses=yes
       fi
   fi
   if test ! "$CURSES_LIB" -a "$with_ncurses" != no -a "$with_ncursesw" != yes
   then
     AC_CACHE_CHECK([for working ncurses], ax_cv_ncurses,
       [LIBS="$ax_save_LIBS -lncurses"
        AC_TRY_LINK(
          [#include <ncurses.h>],
          [chtype a; int b=A_STANDOUT, c=KEY_LEFT; initscr(); ],
          ax_cv_ncurses=yes, ax_cv_ncurses=no)])
     if test "$ax_cv_ncurses" = yes
     then
       AC_DEFINE([HAVE_NCURSES_H],[1],[Define if you have ncurses.h])
       CURSES_LIB="-lncurses"
       ax_cv_curses=yes
     fi
   fi
   if test "$ax_cv_curses" != yes -a "$with_ncurses" != yes -a "$with_ncursesw" != yes
   then
     if test ! "$CURSES_LIB"
     then
       CURSES_LIB="-lcurses"
     fi
     AC_CACHE_CHECK([for working curses], ax_cv_curses,
       [LIBS="$ax_save_LIBS $CURSES_LIB"
        AC_TRY_LINK(
          [#include <curses.h>],
          [chtype a; int b=A_STANDOUT, c=KEY_LEFT; initscr(); ],
          ax_cv_curses=yes, ax_cv_curses=no)])
     if test "$ax_cv_curses" = yes
     then
       AC_DEFINE([HAVE_CURSES_H],[1],[Define if you have curses.h])
     fi
   fi
   LIBS="$ax_save_LIBS"
])dnl
