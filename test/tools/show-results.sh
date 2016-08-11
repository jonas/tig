#!/bin/sh
#
# Aggregate test results.
# Usage: $0
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

set -eu
if [ -n "${BASH_VERSION:-}" ]; then
	set -o pipefail
	IFS=$' \n\t'
fi

tests="$(find test/ -name ".test-result" | wc -l)"
asserts="$(find test/ -name ".test-result" | xargs sed -n '/\[\(OK\|FAIL\)\]/p' | wc -l)"
failures="$(find test/ -name ".test-result" | xargs grep FAIL | wc -l || true)"
skipped="$(find test/ -name ".test-skipped" | wc -l || true)"

if [ $failures = 0 ]; then
	printf "Passed %d assertions in %d tests" "$asserts" "$tests"
else
	printf "Failed %d of %d assertions in %d tests" "$failures" "$asserts" "$tests"
fi

if [ $skipped != 0 ]; then
	printf " (%d skipped)" "$skipped"
fi

echo
exit $failures
