#!/bin/sh

export WARNINGS="all"
set -e

# Ideally, we could just do this:
#
#${AUTORECONF:-autoreconf} -v -I tools
#
# Unfortunately, Autoconf 2.61's autoreconf(1) (found in Mac OS X 10.5
# Leopard) neglects to pass the -I on to aclocal(1), which is
# precisely where we need it!  So we do basically what it would have
# done.

run () {
    test "${V}" = 1 && echo $0: running: "$@"
    "$@"
}

run ${ACLOCAL:-aclocal} -I tools
run ${AUTOCONF:-autoconf} --include=tools
run ${AUTOHEADER:-autoheader} --include=tools
