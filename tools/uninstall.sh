#!/bin/sh
#
# Uninstall data or executable file.
#
# Usage: $0 dest
#
# Copyright (c) 2006-2024 Jonas Fonseca <jonas.fonseca@gmail.com>
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

file="$1"
dir="$(dirname "$file")"

if [ -d "$file" -a "$dir" = "tig" ]; then
	rm -rf "$file"
elif [ -f "$file" ]; then
	rm -f "$file"
fi

if [ -d "$dir" ]; then
	set +e
	rmdir -p "$dir" 2>/dev/null
fi

echo "$file"
