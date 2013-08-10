#!/bin/sh

export WARNINGS="all"
set -e

# Ideally, we could just do this:
#
#${AUTORECONF:-autoreconf} -v -I contrib
#
# Unfortunately, Autoconf 2.61's autoreconf(1) (found in Mac OS X 10.5
# Leapard) neglects to pass the -I on to aclocal(1), which is
# precisely where we need it!  So we do basically what it would have
# done.

run () {
    echo $0: running: "$@"
    "$@"
}

run ${ACLOCAL:-aclocal} -I contrib
run ${AUTOCONF:-autoconf} --include=contrib
run ${AUTOHEADER:-autoheader} --include=contrib
