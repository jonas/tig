#!/bin/bash

build_config () {
	cp contrib/config.make .
	make all-debug
	make update-docs && git diff --exit-code
	make test
	make test TEST_OPTS=valgrind
	if [ $CC = clang ]; then make test-address-sanitizer; fi
	make DESTDIR=/tmp/bare-destdir install install-doc
	make DESTDIR=/tmp/bare-destdir uninstall
	test ! -d /tmp/bare-destdir
	make prefix=/tmp/bare-prefix install install-doc
	/tmp/bare-prefix/bin/tig --version
	make prefix=/tmp/bare-prefix uninstall
	test ! -d /tmp/bare-prefix
 	make distclean
}

build_autoconf () {
	make dist
	./configure --prefix=/tmp/conf-prefix
	make V=1 TEST_SHELL=bash all test install install-doc
	/tmp/conf-prefix/bin/tig --version
	make uninstall
	test ! -d /tmp/conf-prefix
	make clean
}

if [[ $TRAVIS_TIG = config ]]; then
	build_config
elif [[ $TRAVIS_TIG = autoconf ]]; then
	build_autoconf
fi
