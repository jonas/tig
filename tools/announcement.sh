#!/bin/sh
#
# Prepare the content of the next tig release announcement.
# Usage: $0 [revision]
#
# Copyright (c) 2006-2025 Jonas Fonseca <jonas.fonseca@gmail.com>
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

root="$(git rev-parse --show-cdup)"
NEWS="${root}NEWS.adoc"
README="${root}README.adoc"
from="$(sed -n '7,/^tig-/p' < "$NEWS" | tail -n 1 | cut -d' ' -f 1)"
to="${1-HEAD}"
short=

test -n "$(git rev-list --skip=50 $from..$to)" && short=-s

cat <<EOF
$to
$(echo "$to" | sed 's/[0-9a-zA-Z.-]/=/g')

*** text for the announcement ***

$(sed -n '/What is tig?/,/^$/p' < "$README")

$(sed -n 's/\( -.*\)[[(].*/\1/p' < "$README")

Release notes
-------------
$(sed -n '7,/^tig-/p' < "$NEWS" | sed '/^tig-/d')

Change summary
--------------
The diffstat and log summary for changes made in this release.

$(git diff-tree -M --stat=72 $from..$to)

$(git shortlog --no-merges $short $from..$to)
EOF
