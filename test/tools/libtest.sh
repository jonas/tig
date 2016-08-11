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

set -eu
if [ -n "${BASH_VERSION:-}" ]; then
	set -o pipefail
	IFS=$'\n\t'
fi

test="$(basename "$0")"
source_dir="$(cd $(dirname "$0") >/dev/null && pwd)"
base_dir="$(echo "$source_dir" | sed -n 's#\(.*/test\)\([/].*\)*#\1#p')"
prefix_dir="$(echo "$source_dir" | sed -n 's#\(.*/test/\)\([/].*\)*#\2#p')"
output_dir="$base_dir/tmp/$prefix_dir/$test"
tmp_dir="$base_dir/tmp"
output_dir="$tmp_dir/$prefix_dir/$test"
work_dir="work dir"

# The locale must specify UTF-8 for Ncurses to output correctly. Since C.UTF-8
# does not exist on Mac OS X, we end up with en_US as the only sane choice.
export LANG=en_US.UTF-8
export LC_ALL=en_US.UTF-8

export PAGER=cat
export TZ=UTC
export TERM=dumb
export HOME="$output_dir"
unset CDPATH
unset VISUAL

# Git env
export GIT_CONFIG_NOSYSTEM
unset GIT_CONFIG GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE
unset GIT_AUTHOR_NAME GIT_AUTHOR_EMAIL GIT_AUTHOR_DATE
unset GIT_COMMITTER_NAME GIT_COMMITTER_EMAIL GIT_COMMITTER_DATE
unset GIT_EDITOR GIT_SEQUENCE_EDITOR GIT_PAGER GIT_EXTERNAL_DIFF
unset GIT_NOTES_REF GIT_NOTES_DISPLAY_REF

# Tig env
export TIG_TRACE=
export TIGRC_SYSTEM=
unset TIGRC_USER

# Ncurses env
export ESCDELAY=200
export LINES=30
export COLUMNS=80

# Internal test env
# A comma-separated list of options. See docs in test/README.
export TEST_OPTS="${TEST_OPTS:-}"
# Used by tig_script to set the test "scope" used by test_tig.
export TEST_NAME=

[ -e "$output_dir" ] && rm -rf "$output_dir"
mkdir -p "$output_dir/$work_dir"

if [ ! -d "$tmp_dir/.git" ]; then
	# Create a dummy repository to avoid reading .git/config
	# settings from the tig repository.
	git init -q "$tmp_dir"
fi

# For any utilities used in Tig scripts
BIN_DIR="$HOME/bin"
mkdir -p "$BIN_DIR"
export PATH="$BIN_DIR:$PATH"

executable() {
	path="$BIN_DIR/$1"; shift

	if [ "$#" = 0 ]; then
		case "$path" in
			stdin|expected*) cat ;;
			*) sed 's/^[ ]//' ;;
		esac > "$path"
	else
		printf '%s' "$@" > "$path"
	fi
	chmod +x "$path"
}

# Setup fake editor
export EDITOR="vim"
executable 'vim' <<EOF
#!/bin/sh

file="\$1"
lineno="\$(expr "\$1" : '+\([0-9]*\)')"
if [ -n "\$lineno" ]; then
	file="\$2"
fi

echo "\$@" >> "$HOME/editor.log"
sed -n -e "\${lineno}p" "\$file" >> "$HOME/editor.log" 2>&1
EOF

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
	if [ "$#" = 0 ]; then
		case "$path" in
			stdin|expected*) cat ;;
			*) sed 's/^[ ]//' ;;
		esac > "$path"
	else
		printf '%s' "$@" > "$path"
	fi
}

tig_script() {
	name="$1"; shift
	prefix="${name:+$name.}"

	export TIG_SCRIPT="$HOME/${prefix}steps"
	export TEST_NAME="$name"

	# Ensure that the steps finish by quitting
	printf '%s\n:quit\n' "$@" \
		| sed -e 's/^[ 	]*//' -e '/^$/d' \
		| sed "s|:save-display\s\+\(\S*\)|:save-display $HOME/\1|" \
		> "$TIG_SCRIPT"
}

steps() {
	tig_script "" "$@"
}

stdin() {
	file "stdin" "$@"
}

tigrc() {
	file "$HOME/.tigrc" "$@"
}

gitconfig() {
	file "$HOME/.gitconfig" "$@"
}

in_work_dir()
{
	(cd "$work_dir" && $@)
}

auto_detect_debugger() {
	for dbg in gdb lldb; do
		dbg="$(command -v "$dbg" 2>/dev/null || true)"
		if [ -n "$dbg" ]; then
			echo "$dbg"
			return
		fi
	done

	die "Failed to detect a supported debugger"
}

#
# Parse TEST_OPTS
#

expected_status_code=0
diff_color_arg=
[ -t 1 ] && diff_color_arg=--color

indent='            '
verbose=
debugger=
trace=
valgrind=

set -- $TIG_TEST_OPTS $TEST_OPTS

while [ $# -gt 0 ]; do
	arg="$1"; shift
	case "$arg" in
		verbose) verbose=yes ;;
		no-indent) indent= ;;
		debugger=*) debugger=$(expr "$arg" : 'debugger=\(.*\)') ;;
		debugger) debugger="$(auto_detect_debugger)" ;;
		trace) trace=yes ;;
		valgrind) valgrind="$HOME/valgrind.log" ;;
	esac
done

#
# Test runners and assertion checking.
#

assert_equals()
{
	file="$1"; shift

	file "expected/$file" "$@"

	if [ -e "$file" ]; then
		git diff -w --no-index $diff_color_arg "expected/$file" "$file" > "$file.diff" || true
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

assert_not_exists()
{
	file="$1"; shift

	if [ -e "$file" ]; then
		echo "[FAIL] $file should not exist" >> .test-result
	else
		echo "  [OK] $file does not exist" >> .test-result
	fi
}

vars_file="vars"
expected_var_file="$HOME/expected/$vars_file"

executable 'assert-var' <<EOF
#!/bin/sh

mkdir -p "$(dirname "$expected_var_file")"
while [ \$# -gt 1 ]; do
	arg="\$1"; shift

	case "\$arg" in
		==) break ;;
		*)  echo "\$arg" >> "$HOME/$vars_file" ;;
	esac
done
echo "\$@" >> "$expected_var_file"
EOF

assert_vars()
{
	if [ -e "$expected_var_file" ]; then
		assert_equals "$vars_file" "$(cat "$expected_var_file")"
	else
		echo "[FAIL] $expected_var_file not found" >> .test-result
	fi
}

show_test_results()
{
	if [ -e .test-skipped ]; then
		sed "s/^/$indent[skipped] /" < .test-skipped
		return
	fi
	if [ -n "$trace" -a -n "$TIG_TRACE" -a -e "$TIG_TRACE" ]; then
		sed "s/^/$indent[trace] /" < "$TIG_TRACE"
	fi
	if [ -n "$valgrind" -a -e "$valgrind" ]; then
		sed "s/^/$indent[valgrind] /" < "$valgrind"
	fi
	if [ ! -d "$HOME" ] || [ ! -e .test-result ]; then
		[ -e stderr ] &&
			sed "s/^/[stderr] /" < stderr
		[ -e stderr.orig ] &&
			sed "s/^/[stderr] /" < stderr.orig
		echo "No test results found"
	elif grep FAIL -q < .test-result; then
		failed="$(grep FAIL < .test-result | wc -l)"
		count="$(sed -n '/\(FAIL\|OK\)/p' < .test-result | wc -l)"

		printf "Failed %d out of %d test(s)%s\n" $failed $count 

		# Show output from stderr if no output is expected
		if [ -e stderr ]; then
			[ -e expected/stderr ] ||
				sed "s/^/[stderr] /" < stderr
		fi

		# Replace CR used by Git progress messages
		tr '\r' '\n' < .test-result
	elif [ "$verbose" ]; then
		count="$(sed -n '/\(OK\)/p' < .test-result | wc -l)"
		printf "Passed %d assertions\n" $count
	fi | sed "s/^/$indent| /"
}

trap 'show_test_results' EXIT

test_skip()
{
	echo "$@" >> .test-skipped
}

require_git_version()
{
	git_version="$(git version | sed 's/git version \([0-9\.]*\).*/\1/')"
	actual_major="$(expr "$git_version" : '\([0-9]\).*')"
	actual_minor="$(expr "$git_version" : '[0-9]\.\([0-9]\).*')"

	required_version="$1"; shift
	required_major="$(expr "$required_version" : '\([0-9]\).*')"
	required_minor="$(expr "$required_version" : '[0-9]\.\([0-9]\).*')"

	if [ "$required_major" -gt "$actual_major" ] ||
	   [ "$required_minor" -gt "$actual_minor" ]; then
		test_skip "$@"
	fi
}

test_require()
{
	while [ $# -gt 0 ]; do
		feature="$1"; shift

		case "$feature" in
		git-worktree)
			require_git_version 2.5 \
				"The test requires git-worktree, available in git version 2.5 or newer"
			;;
		address-sanitizer)
			if [ "${TIG_ADDRESS_SANITIZER_ENABLED:-no}" != yes ]; then
				test_skip "The test requires clang and is only run via \`make test-address-sanitizer\`"
			fi
			;;
		*)
			test_skip "Unknown feature requirement: $feature" 
		esac
	done
}

test_exec_work_dir()
{
	echo "=== $@ ===" >> "$HOME/test-exec.log"
	test_exec_log="$HOME/test-exec.log.tmp"
	rm -f "$test_exec_log"

	set +e
	in_work_dir "$@" 1>>"$test_exec_log" 2>>"$test_exec_log"
	test_exec_exit_code=$?
	set -e

	cat "$test_exec_log" >> "$HOME/test-exec.log"
	if [ "$test_exec_exit_code" != 0 ]; then
		cmd="$@"
		echo "[FAIL] unexpected exit code while executing '$cmd': $test_exec_exit_code" >> .test-result
		cat "$test_exec_log" >> .test-result
		# Exit gracefully to allow additional tests to run
		exit 0
	fi
}

test_setup()
{
	if [ -e .test-skipped ]; then
		exit 0
	fi

	run_setup="$(type test_setup_work_dir 2>/dev/null | grep 'function' || true)"

	if [ -n "$run_setup" ]; then
		if test ! -e "$HOME/test-exec.log" || ! grep -q test_setup_work_dir "$HOME/test-exec.log"; then
			test_exec_work_dir test_setup_work_dir
		fi
	fi
}

valgrind_exec()
{
	kernel="$(uname -s 2>/dev/null || echo unknown)"
	kernel_supp="test/tools/valgrind-$kernel.supp"

	valgrind_ops=
	if [ -e "$kernel_supp" ]; then
		valgrind_ops="$valgrind_ops --suppresions='$kernel_supp'"
	fi

	valgrind -q --gen-suppressions=all --track-origins=yes --error-exitcode=1 \
		--log-file="$valgrind.orig" $valgrind_ops \
		"$@"

	case "$kernel" in
	Darwin)	grep -v "mach_msg unhandled MACH_SEND_TRAILER option" < "$valgrind.orig" > "$valgrind" ;;
	*)	mv "$valgrind.orig" "$valgrind" ;;
	esac

	rm -f "$valgrind.orig" 
}

test_tig()
{
	name="$TEST_NAME"
	prefix="${name:+$name.}"

	test_setup
	export TIG_NO_DISPLAY=
	if [ -n "$trace" ]; then
		export TIG_TRACE="$HOME/${prefix}tig-trace"
	fi
	touch "${prefix}stdin" "${prefix}stderr"
	if [ -n "$debugger" ]; then
		echo "*** Running tests in '$(pwd)/$work_dir'"
		if [ -s "$work_dir/${prefix}stdin" ]; then
			echo "*** - This test requires data to be injected via stdin."
			echo "***   The expected input file is: '../${prefix}stdin'"
		fi
		if [ "$#" -gt 0 ]; then
			echo "*** - This test expects the following arguments: $@"
		fi
		(cd "$work_dir" && $debugger tig "$@")
	else
		set +e
		runner=
		# FIXME: Tell Valgrind to forward status code
		if [ "$expected_status_code" = 0 -a -n "$valgrind" ]; then
			runner=valgrind_exec
		fi
		if [ -s "${prefix}stdin" ]; then
			(cd "$work_dir" && $runner tig "$@") < "${prefix}stdin" > "${prefix}stdout" 2> "${prefix}stderr.orig"
		else
			(cd "$work_dir" && $runner tig "$@") > "${prefix}stdout" 2> "${prefix}stderr.orig"
		fi
		status_code="$?"
		if [ "$status_code" != "$expected_status_code" ]; then
			echo "[FAIL] unexpected status code: $status_code (should be $expected_status_code)" >> .test-result
		fi
		set -e
	fi
	# Normalize paths in stderr output
	if [ -e "${prefix}stderr.orig" ]; then
		sed "s#$output_dir#HOME#" < "${prefix}stderr.orig" > "${prefix}stderr"
		rm -f "${prefix}stderr.orig"
	fi
	if [ -n "$trace" ]; then
		export TIG_TRACE="$HOME/.tig-trace"
		if [ -n "$name" ]; then
			sed "s#^#[$name] #" < "$HOME/${prefix}tig-trace" >> "$HOME/.tig-trace"
		else
			mv "$HOME/${prefix}tig-trace" "$HOME/.tig-trace"
		fi
	fi
	if [ -n "$prefix" ]; then
		sed "s#^#[$name] #" < "${prefix}stderr" >> "stderr"
	fi
}

test_graph()
{
	test-graph $@ > stdout 2> stderr.orig
}

test_case()
{
	name="$1"; shift

	echo "$name" >> test-cases
	cat > "$name.expected"

	touch "$name-before" "$name-after" "$name.script" "$name-args"

	while [ "$#" -gt 0 ]; do
		arg="$1"; shift
		key="$(expr "X$arg" : 'X--\([^=]*\).*')"
		value="$(expr "X$arg" : 'X--[^=]*=\(.*\)')"

		case "$key" in
		before|after|script|args|cwd)
			echo "$value" > "$name-$key" ;;
		*)	die "Unknown test_case argument: $arg"
		esac
	done
}

run_test_cases()
{
	if [ ! -e test-cases ]; then
		return
	fi
	test_setup
	for name in $(cat test-cases); do
		tig_script "$name" "
			$(if [ -e "$name-script" ]; then cat "$name-script"; fi)
			:save-display $name.screen
		"
		if [ -e "$name-before" ]; then
			test_exec_work_dir "$SHELL" "$HOME/$name-before" 
		fi
		old_work_dir="$work_dir"
		if [ -e "$name-cwd" ]; then
			work_dir="$work_dir/$(cat "$name-cwd")"
		fi
		ORIG_IFS="$IFS"
	        IFS=$' '
		test_tig $(if [ -e "$name-args" ]; then cat "$name-args"; fi)
	        IFS="$ORIG_IFS"
		work_dir="$old_work_dir"
		if [ -e "$name-after" ]; then
			test_exec_work_dir "$SHELL" "$HOME/$name-after" 
		fi

		assert_equals "$name.screen" < "$name.expected"
		assert_equals "$name.stderr" ''
	done
}
