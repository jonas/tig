# Example config.make.

# Install files under /usr/local instead of under $HOME.
prefix=/usr/local

# Use ncursesw.
LDLIBS =-lncursesw
CPPFLAGS =-DHAVE_NCURSESW_CURSES_H

# Uncomment to enable work-around for missing setenv().
#NO_SETENV=y

# Uncomment to enable work-around for missing mkstemps().
#NO_MKSTEMPS=y

# vim: ft=make:
