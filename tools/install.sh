#!/bin/sh
#
# Install data or executable file.
#
# Usage: $0 {data|bin} src dest
#
# Copyright (c) 2014 Jonas Fonseca <jonas.fonseca@gmail.com>
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License as
# published by the Free Software Foundation; either version 2 of
# the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

set -e

data="$1"
src="$2"
dest="$3"
mode=0755
trash=

case "$data" in data)
	mode=0644
esac

install -d "$dest"

if [ "$V" = "@" ]; then
	echo "$src -> $dest"
fi

# Replace fake /etc-path
case "$src" in doc/*)
	dest="$dest/$(basename "$src")"
	sed "s#++SYSCONFDIR++#${sysconfdir}#" < "$src" > "$src+"
	trash="$src+"
	src="$src+"
esac

install -p -m "$mode" "$src" "$dest"

if [ -n "$trash" ]; then
	rm -f "$trash"
fi
