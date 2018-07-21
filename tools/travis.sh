#!/bin/bash

set -euo pipefail
IFS=$'\n\t'

build_config_make() {
	cp contrib/config.make .
	make all-debug
	make update-docs && git diff --exit-code
	make test

	make prefix=/tmp/bare-prefix install install-doc
	/tmp/bare-prefix/bin/tig --version
	make prefix=/tmp/bare-prefix uninstall
	test ! -d /tmp/bare-prefix

 	make distclean
}

build_autoconf() {
	make dist
	./configure --prefix=/tmp/conf-prefix
	make V=1 TEST_SHELL=bash all test

	make install install-doc
	/tmp/conf-prefix/bin/tig --version
	make uninstall
	test ! -d /tmp/conf-prefix

	make DESTDIR=/tmp/bare-destdir install install-doc
	/tmp/bare-destdir/tmp/conf-prefix/bin/tig --version
	make DESTDIR=/tmp/bare-destdir uninstall
	test ! -d /tmp/bare-destdir

	make clean
}

build_address_sanitizer() {
	cp contrib/config.make .
	make test-address-sanitizer
}

build_valgrind() {
	cp contrib/config.make .
	make all-debug test TEST_OPTS=valgrind
}

case "$TIG_BUILD" in
	config.make)		build_config_make ;;
	autoconf)		build_autoconf ;;
	address-sanitizer)	build_address_sanitizer ;;
	valgrind)		build_valgrind ;;

	*)
		echo "Unknown config: $TIG_BUILD"
		exit 1
esac
