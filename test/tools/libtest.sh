#!/bin/sh
#
# Setup test environment.
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

test="$(basename "$0")"
source_dir="$(cd "$(dirname "$0")" && pwd)"
base_dir="$(echo "$source_dir" | sed -n 's#\(.*/test\)\([/].*\)*#\1#p')"
prefix_dir="$(echo "$source_dir" | sed -n 's#\(.*/test/\)\([/].*\)*#\2#p')"
output_dir="$base_dir/tmp/$prefix_dir/$test"
tmp_dir="$base_dir/tmp"
output_dir="$tmp_dir/$prefix_dir/$test"

[ -t 1 ] && diff_color_arg=--color

[ -e "$output_dir" ] && rm -rf "$output_dir"
mkdir -p "$output_dir"

# The locale must specify UTF-8 for Ncurses to output correctly. Since C.UTF-8
# does not exist on Mac OS X, we end up with en_US as the only sane choice.
export LANG=en_US.UTF-8
export LC_ALL=en_US.UTF-8

export PAGER=cat
export TZ=UTC
export TERM=dumb
export HOME="$output_dir"
export EDITOR=:
unset CDPATH

# Git env
export GIT_CONFIG_NOSYSTEM
unset GIT_CONFIG

# Tig env
export TIGRC_SYSTEM=
export TIGRC_USER="~/tigrc"

# Ncurses env
export ESCDELAY=200
export LINES=30
export COLUMNS=80

cd "$output_dir"

#
# Utilities.
#

die()
{
	echo >&2 "$*"
	exit 1
}

file() {
	path="$1"; shift

	mkdir -p "$(dirname "$path")"
	if [ -z "$1" ]; then
		case "$path" in
			stdin|expected*) cat ;;
			*) sed 's/^[ ]//' ;;
		esac > "$path"
	else
		printf '%s' "$@" > "$path"
	fi
}

steps() {
	# Ensure that the steps finish by quitting
	printf '%s\n:quit\n' "$@" \
		| sed -e 's/^[ 	]*//' -e '/^$/d' \
		| sed 's#:save-display\s\+\(\S*\)#:save-display ../\1#' \
		     > steps
	export TIG_SCRIPT="$HOME/steps"
}

stdin() {
	file "stdin" "$@"
}

tigrc() {
	file "tigrc" "$@"
}

gitconfig() {
	file "gitconfig" "$@"
}

#
# Test runners and assertion checking.
#

assert_equals()
{
	file="$1"; shift

	file "expected/$file" "$@"

	if [ -e "$file" ]; then
		git diff --no-index $diff_color_arg "expected/$file" "$file" > "$file.diff" || true
		if [ -s "$file.diff" ]; then
			echo "[FAIL] $file != expected/$file" >> .test-result
			cat "$file.diff" >> .test-result
		else
			echo "  [OK] $file assertion" >> .test-result
		fi
		rm -f "$file.diff"
	else
		echo "[FAIL] $file not found" >> .test-result
	fi
}

show_test_results()
{
	indent='            '

	if [ ! -e .test-result ]; then
		echo
		{
			echo "No test results found"
			[ -e stderr ] &&
				sed "s/^/[stderr] /" < stderr
		} | sed "s/^/$indent| /"
	elif grep FAIL -q < .test-result; then
		failed="$(grep FAIL < .test-result | wc -l)"
		count="$(sed -n '/\(FAIL\|OK\)/p' < .test-result | wc -l)"

		echo
		{
			printf "Failed %d out of %d test(s)%s\n" $failed $count 

			# Show output from stderr if no output is expected
			[ -e expected/stderr ] ||
				sed "s/^/[stderr] /" < stderr

			[ -e .test-result ] &&
				cat .test-result
		} | sed "s/^/$indent| /"
	else
		count="$(sed -n '/\(OK\)/p' < .test-result | wc -l)"
		printf " (passed %d)\n" $count
	fi
}

trap 'show_test_results' EXIT

test_tig()
{
	export TIG_NO_DISPLAY=
	touch stdin
	(cd "$work_dir" && tig "$@") < stdin > stdout 2> stderr.orig
	# Normalize paths in stderr output
	sed "s#$output_dir#HOME#" < stderr.orig > stderr
	rm -f stderr.orig
}

test_graph()
{
	test-graph $@ > stdout 2> stderr.orig
}
