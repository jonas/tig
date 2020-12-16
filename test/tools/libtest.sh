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
	# Prevent bash from changing LINES and COLUMNS variables
	shopt -u checkwinsize || true
fi
IFS='
	'

test="$(basename -- "$0")"
source_dir="$(cd "$(dirname -- "$0")" >/dev/null && pwd)"
base_dir="$(printf '%s\n' "$source_dir" | sed -n 's#\(.*/test\)\([/].*\)*#\1#p')"
prefix_dir="$(printf '%s\n' "$source_dir" | sed -n 's#\(.*/test/\)\([/].*\)*#\2#p')"
output_dir="$base_dir/tmp/$prefix_dir/$test"
tmp_dir="$base_dir/tmp"
output_dir="$tmp_dir/$prefix_dir/$test"
work_dir="work-dir"
tty_attrs="$(stty -g </dev/tty)"

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
unset INPUTRC

# Freedesktop env
unset XDG_CONFIG_HOME XDG_CONFIG_DIRS XDG_DATA_HOME XDG_DATA_DIRS XDG_CACHE_HOME

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

# Disable memleak detection for AddressSanitizer.
export ASAN_OPTIONS=detect_leaks=false

# Internal test env
# A space-separated list of options. See docs in test/README.
export TEST_OPTS="${TEST_OPTS:-}"
# Used by tig_script to set the test "scope" used by test_tig.
export TEST_NAME=
export TEST_KEYSTROKES=

[ -e "$output_dir" ] && rm -rf -- "$output_dir"
mkdir -p -- "$output_dir/$work_dir"

if [ ! -d "$tmp_dir/.git" ]; then
	# Create a dummy repository to avoid reading .git/config
	# settings from the tig repository.
	git init -q -- "$tmp_dir"
fi

# For any utilities used in Tig scripts
BIN_DIR="$HOME/bin"
mkdir -p -- "$BIN_DIR"
export PATH="$BIN_DIR:$PATH"

executable() {
	path="$BIN_DIR/$1"; shift

	if [ "$#" = 0 ]; then
		case "$path" in
			stdin|expected*) cat ;;
			*) sed 's/^[ ]//' ;;
		esac > "$path"
	else
		printf '%s' "$*" > "$path"
	fi
	chmod -- +x "$path"
}

# Setup fake editor
export EDITOR="vim"
executable 'vim' <<EOF
#!/bin/sh

file="\$1"
lineno="\$(expr "\$1" : '+\([0-9]*\)')"
if [ -n "\$lineno" ]; then
	file="\$2"
else
	lineno=1,5
fi

printf '%s\\n' "\$*" >> "$HOME/editor.log"
sed -n -e "\${lineno}p" "\$file" >> "$HOME/editor.log" 2>&1
EOF

cd "$output_dir"

#
# Utilities.
#

die()
{
	printf '%s\n' "$*" >&2
	exit 1
}

tty_reset()
{
	if [ -n "$tty_attrs" ]; then
		( trap '' TTOU; trap '' TTIN; stty "$tty_attrs" </dev/tty ) || true;
	fi
}

tempfile_name()
{
	temp_base="${1:-tempfile}"
	if which mktemp >/dev/null 2>&1; then
		temp_file="$(mktemp "$tmp_dir/${temp_base}.XXXXXX")"
	else
		temp_file="$tmp_dir/${temp_base}.$$"
		while [ -e "$temp_file" ]; do
			temp_file="${temp_file}.alt"
		done
	fi
	printf '%s\n' "$temp_file"
}

### Testing API AsciiDoc
#|
#| file(filename, [content, ...]) [< content]::
#|
file() {
	path="$1"; shift

	mkdir -p -- "$(dirname -- "$path")"
	if [ "$#" = 0 ]; then
		case "$path" in
			stdin|expected*) cat ;;
			*) sed 's/^[ ]//' ;;
		esac > "${path}.tmp"
		mv -f -- "${path}.tmp" "$path"
	else
		printf '%s' "$*" > "$path"
	fi
}

### Testing API AsciiDoc
#|
#| tig_script(name, content, [content, ...])::
#|
tig_script() {
	name="$1"; shift
	prefix="${name:+$name.}"

	export TIG_SCRIPT="$HOME/${prefix}steps"
	export TEST_NAME="$name"

	if [ -z "${TEST_KEYSTROKES:-}" ]; then
		# ensure that the steps finish by quitting
		quit_str=':quit'
	else
		# unless simulated keystrokes are also to be sent
		quit_str=''
	fi

	printf '%s\n%s\n' "$*" "$quit_str" \
		| sed -e 's/^[ 	]*//' \
		| sed "s|:save-display[ 	]\{1,\}\([^ 	]\{1,\}\)|:save-display $HOME/\1|" \
		| sed "s|:save-options[ 	]\{1,\}\([^ 	]\{1,\}\)|:save-options $HOME/\1|" \
		| sed "s|:save-view[ 	]\{1,\}\([^ 	]\{1,\}\)|:save-view $HOME/\1|" \
		> "$TIG_SCRIPT"
}

### Testing API AsciiDoc
#|
#| steps(content, [content, ...])::
#|
steps() {
	tig_script "" "$@"
}

_tig_keystrokes_driver()
{
	file="$1"; shift
	append_mode="$1"; shift

	if [ -n "$append_mode" ]; then
		printf '%s' "$*" >> "$file"
	else
		printf '%s' "$*" > "$file"
	fi
}

tig_keystrokes()
{
	name="$1"; shift
	prefix="${name:+$name.}"

	export TEST_KEYSTROKES="$HOME/${prefix}keystrokes"
	export TEST_NAME="$name"

	append_mode=''
	keysym_mode=''
	repeat_mode=''
	while [ "$#" -gt 0 ]; do
		case "$1" in
			-append|--append) append_mode=yes && shift;;
			-keysym|--keysym|-keysyms|--keysyms) keysym_mode=yes && shift;;
			-repeat=*|--repeat=*) repeat_mode="$(expr "$1" : '--*repeat=\([0-9][0-9]*\)')" && shift || die "bad value $1";;
			*) break;;
		esac
	done

	if [ -n "${TIG_SCRIPT:-}" ] && [ -s "$TIG_SCRIPT" ] && [ "$(tail -1 < "${TIG_SCRIPT}")" = ':quit'  ]; then
		# remove the trailing :quit from a script if it already was defined
		head -n "$(expr "$(grep -c '^' < "$TIG_SCRIPT")" - 1)" < "${TIG_SCRIPT}" > "${TIG_SCRIPT}.tmp"
		mv -f -- "${TIG_SCRIPT}.tmp" "$TIG_SCRIPT"
	fi

	if [ -n "$repeat_mode" ] && [ "$#" -gt 1 ]; then
		die "tig_keystrokes -repeat can only be used with a single argument"
	fi

	if [ -n "$repeat_mode" ]; then
		test "$#" -gt 1 && die "tig_keystrokes -repeat can only be used with a single key-sequence argument"
		test "$repeat_mode" -lt 1 && repeat_mode=1

		chunk="$1"
		if [ -n "$keysym_mode" ]; then
			chunk="%(keysym:$1)"
		fi

		while [ "$repeat_mode" -gt 0 ]; do
			_tig_keystrokes_driver "$TEST_KEYSTROKES" "$append_mode" "$chunk"
			append_mode=yes
			repeat_mode="$((repeat_mode - 1))"
		done
	elif [ -n "$keysym_mode" ]; then
		for chunk in "$@"; do
			_tig_keystrokes_driver "$TEST_KEYSTROKES" "$append_mode" "%(keysym:$chunk)"
			append_mode=yes
		done
	else
		_tig_keystrokes_driver "$TEST_KEYSTROKES" "$append_mode" "$@"
	fi

	if [ -n "$valgrind" ]; then
		test_skip "simulated keystrokes are not yet reliable under valgrind"
	fi

	test_require python
	test_require python-termios
}

### Testing API AsciiDoc
#|
#| keystrokes([-append | -keysym | -repeat=<int>] key-sequence, [key-sequence, ...])::
#|
#|	Key sequences are given as Python strings, and accept
#|	https://docs.python.org/2.0/ref/strings.html[the same string escapes as
#|	Python]. Example: `'\134'` encodes a literal backslash. +
#|	+
#|	The key sequence may also contain special embedded codes:
#|	`%(keysym:<name>)`, `%(keypause:<seconds>)`, or `%(keysignal:<signal>)`. +
#|	+
#|	*`%(keysym:<name>)`* will be translated into the raw characters
#|	associated with the symbolic key name. <name> may be any form accepted
#|	by 'bind' in `~/.tigrc`, or any terminal capability name known to
#|	`tput`. Examples: `%(keysym:Left)`, `%(keysym:Ctrl-A)`. +
#|	+
#|	As a convenience, the `-keysym` option causes subsequent arguments to
#|	be interpreted as a series of keysym names.  Example:
#| -----------------------------------------------------------------------------
#|	keystrokes -keysym 'Down' 'Up'
#| -----------------------------------------------------------------------------
#|	::
#|	*`%(keypause:<seconds>)`* will insert a pause in the simulated
#|	keystrokes. If pauses are added, the test timeout may also need to be
#|	increased. +
#|	+
#|	*`%(keysignal:<signal>)`* will send a Unix signal to tig.  The signal
#|	may be given as a number or symbol. Example: `%(keysignal:SIGWINCH)` +
#|	+
#|	To send tig a literal sequence matching the characters of an embedded
#|	code, escape it with a backslash: `\%(keypause:1)`. +
#|	+
#|	Exiting: The passed key sequence should always arrange a clean exit.
#|	Tig will otherwise be shut down by a signal, which is less consistent
#|	for the purpose of testing. +
#|	+
#|	Appending: keystrokes may be defined in multiple passes using `-append`.
#|	This enables composing keystrokes with and without `-keysym`.  The last
#|	call to `keystrokes()` should arrange a clean exit from tig. +
#|	+
#|	Repeating: `-repeat=<int>` can be used to define repeated sequences.
#|	Only one key-sequence argument may be used with `-repeat`.  Example:
#| -----------------------------------------------------------------------------
#|	keystrokes -keysym -repeat=20 'Down'
#| -----------------------------------------------------------------------------
#|	::
#|	Interaction with `steps()`: It is possible to use both `steps()` and
#|	`keystrokes()` in the same test: during test execution, the `steps()`
#|	script will run first, and the simulated keystrokes will be sent to tig
#|	after the script finishes. When used together, `steps()` is modified so
#|	that it does not imply `:quit`. +
#|	+
#|	Whitespace handling: The "Enter" key will be fed to tig as a carriage
#|	return (`\r`). Interior newlines in key sequences, whether literal or
#|	encoded, will be translated to carriage returns before sending them to
#|	tig. But note that leading or trailing whitespace on the key-sequence
#|	argument is ignored. To send "Enter", "Tab", or "Space" as the first or
#|	last key in the sequence, use escape codes or keysyms (_ie_ `\r`,`\t`,
#|	`\040`, `%(keysym:Enter)`, `%(keysym:Tab)`, or `%(keysym:Space)`.) +
#|
keystrokes()
{
	tig_keystrokes "" "$@"
}

### Testing API AsciiDoc
#|
#| stdin([content, ...]) [< content]::
#|
stdin() {
	file "stdin" "$@"
}

### Testing API AsciiDoc
#|
#| tigrc([content, ...]) [< content]::
#|
tigrc() {
	file "$HOME/.tigrc" "$@"
}

### Testing API AsciiDoc
#|
#| gitconfig(content, ...)::
#|
gitconfig() {
	file "$HOME/.gitconfig" "$@"
}

### Testing API AsciiDoc
#|
#| in_work_dir(command, [args, ...])::
#|
in_work_dir()
{
	(cd "$work_dir" && "$@")
}

auto_detect_debugger() {
	for dbg in gdb lldb; do
		dbg="$(command -v "$dbg" 2>/dev/null || true)"
		if [ -n "$dbg" ]; then
			printf '%s\n' "$dbg"
			return
		fi
	done

	die "Failed to detect a supported debugger"
}

format_filter()
{
	test -z "$filter" && return

	case "$filter" in
		:*) filter="*$filter";;
	esac
	case "$filter" in
		*:) filter="$filter*";;
	esac
	if [ "$filter" = "*:*" ]; then
		filter=''
	fi
}

filter_file_ok()
{
	test -z "$filter" && return 0

	matcher="$0"
	_filter_filename_part="${filter%:*}"
	case "$matcher" in
		$_filter_filename_part) return 0;;
		*) return 1;;
	esac
}

process_tree()
{
	pid="${1:-0}"; shift
	pid="$(expr "$pid")"
	test "$pid" -gt 0 || return

	depth="${1:-0}"
	maxdepth=20

	printf '%s\n' "$pid"

	depth="$((depth + 1))"
	test "$depth" -ge "$maxdepth" && return

	last_pid=-1
	while [ "$pid" != "$last_pid" ]; do
		last_pid="$pid"
		proc_tmp="$(tempfile_name 'process_tree')"
		ps -e -o ppid=,pid= | grep "^[ 0]*$pid[^0-9]" > "$proc_tmp" || continue
		while read -r child_proc; do
			test -z "$child_proc" && continue
			ORIG_IFS="$IFS"; IFS=' '
			set -- $child_proc
			IFS="$ORIG_IFS"
			test "$#" -ne 2 && continue
			test "$pid" = "$2" && continue
			pid="$2"
			( process_tree "$pid" "$depth" )
		done < "$proc_tmp"
		rm -f -- "$proc_tmp"
	done
}

descend_to_pg_leader()
{
	parent_pid="$1"; shift
	enforce_shortcmd="${1:-}"

	leader_pgid='-1'
	leader_shortcmd=''

	# A short delay is enough to ensure that all relevant processes are up,
	# have completed any initial fork/execs, and allows for some irrelevant
	# processes to be cleared.
	sleep 1

	for pid in $(process_tree "$parent_pid"); do
		test "$pid" -lt 1 && continue
		ORIG_IFS="$IFS"; IFS=' '
		set -- $(ps -o pgid=,command= "$pid" 2>/dev/null)
		IFS="$ORIG_IFS"
		test "$#" -lt 2 && continue

		# By convention a process-group leader uses its own PID as PGID
		test "$pid" -ne "$1" && continue

		leader_pgid="$1"
		leader_shortcmd="$(basename -- "$2")"
		break
	done

	if [ "$leader_pgid" -lt 1 ]; then
		die "could not find a process-group leader"
	fi
	if [ -n "$enforce_shortcmd" ] && [ "$leader_shortcmd" != "$enforce_shortcmd" ]; then
		die "expected process-group leader named $enforce_shortcmd"
	fi

	printf '%s\n' "$leader_pgid"
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
runner=exec
trace_keys=
trace=
todos=
valgrind=
filter=
timeout=10
vlg_timeout_bonus=60

ORIG_IFS="$IFS"
IFS=" 	"
for arg in ${MAKE_TEST_OPTS:-} ${TEST_OPTS:-}; do
	if [ -z "$arg" ]; then
		continue;
	fi
	case "$arg" in
		verbose) verbose=yes ;;
		no[-_]indent|noindent) indent= ;;
		debugger=*) debugger="$(expr "$arg" : 'debugger=\(.*\)')" ;;
		debugger) debugger="$(auto_detect_debugger)" ;;
		timeout=*) timeout="$(expr "$arg" : 'timeout=\(.*\)')" ;;
		trace[-_]keys|trace[-_]keystrokes) trace_keys=yes ;;
		trace) trace=yes ;;
		todo|todos) todos=yes ;;
		valgrind) valgrind="$HOME/valgrind.log" ;;
		filter=*) filter="$(expr "$arg" : 'filter=\(.*\)')" && format_filter;;
		*) die "unknown TEST_OPTS element '$arg'" ;;
	esac
done
IFS="$ORIG_IFS"
ORIG_IFS=

filter_file_ok || exit 0   # silently exit caller who sourced this file

#
# Test runners and assertion checking.
#

### Testing API AsciiDoc
#|
#| assert_equals(filename, [whitespace, note, ...]) < expected::
#|
assert_equals()
{
	file="$1"; shift
	whitespace_arg='-w';

	if [ "$#" -ge 1 ]; then
		whitespace_arg="${1:-}"
		shift
	fi
	if [ "$whitespace_arg" = strict ]; then
		whitespace_arg=''
	elif [ "$whitespace_arg" = ignore ]; then
		whitespace_arg='-w'
	fi

	file "expected/$file"

	if [ -e "$file" ]; then
		( IFS=' 	'; git diff --no-index $diff_color_arg $whitespace_arg -- "expected/$file" "$file" > "$file.diff" || true )
		if [ -s "$file.diff" ]; then
			printf '[FAIL] %s != expected/%s\n' "$file" "$file" >> .test-result
			if [ -n "$*" ]; then
				printf '[NOTE] %s\n' "$*" >> .test-result
			fi
			cat < "$file.diff" >> .test-result
		else
			printf '  [OK] %s assertion\n' "$file" >> .test-result
		fi
		rm -f -- "$file.diff"
	else
		printf '[FAIL] %s not found\n' "$file" >> .test-result
	fi
}

### Testing API AsciiDoc
#|
#| assert_not_exists(filename)::
#|
assert_not_exists()
{
	file="$1"; shift

	if [ -e "$file" ]; then
		printf '[FAIL] %s should not exist\n' "$file" >> .test-result
	else
		printf '  [OK] %s does not exist\n' "$file" >> .test-result
	fi
}

vars_file="vars"
vars_count_file="${vars_file}_assert_count"
expected_vars_file="$HOME/expected/$vars_file"

executable 'assert-var' <<EOF
#!/bin/sh

mkdir -p "$(dirname -- "$expected_vars_file")"
lhs="\${1:-}"
if [ "\$#" -gt 0 ]; then
	shift
fi
if [ "\${1:-}" = "==" ]; then
	shift
fi
rhs="\$*"

if [ -z "\$lhs" ]; then
	lhs='\"\"'
fi
if [ -z "\$rhs" ]; then
	rhs='\"\"'
fi

printf '%s\\n' "\$lhs" >> "$HOME/$vars_file"
printf '%s\\n' "\$rhs" >> "$expected_vars_file"
EOF

### Testing API AsciiDoc
#|
#| assert_vars(count)::
#|
assert_vars()
{
	if [ -n "${1:-}" ]; then
		printf '%s\n' "$1" > "$vars_count_file"
		shift
	else
		die "Test must supply the expected count of assertions to assert_vars()"
	fi

	grep -c . < "$vars_file" | assert_equals "$vars_count_file" strict "$*"

	if [ -e "$expected_vars_file" ]; then
		assert_equals "$vars_file" strict "$*" < "$expected_vars_file"
	else
		printf '[FAIL] %s not found\n' "$expected_vars_file" >> .test-result
	fi
}

show_test_results()
{
	if [ -e .test-skipped ]; then
		sed "s/^/$indent[skipped] /" < .test-skipped
		return
	fi
	if [ -n "$trace" ] && [ -n "$TIG_TRACE" ] && [ -e "$TIG_TRACE" ]; then
		sed "s/^/$indent[trace] /" < "$TIG_TRACE"
	fi
	if [ -n "$valgrind" ] && [ -s "$valgrind" ]; then
		grep -v '^-\+[0-9]\+-\+ \+run:' < "${valgrind}" || true | sed "s/^/$indent[valgrind] /"
	fi
	if [ ! -d "$HOME" ] || [ ! -e .test-result ]; then
		[ -e stderr ] &&
			sed "s/^/[stderr] /" < stderr
		[ -e stderr.orig ] &&
			sed "s/^/[stderr] /" < stderr.orig
		printf 'No test results found\n'
	elif grep -q '^ *\[FAIL\]' < .test-result; then
		failed="$(grep -c '^ *\[FAIL\]' < .test-result || true)"
		count="$(grep -c '^ *\[\(FAIL\|OK\)\]' < .test-result || true)"

		printf 'Failed %d out of %d test(s)\n' "$failed" "$count"

		# Show output from stderr if no output is expected
		if [ -e stderr ]; then
			[ -e expected/stderr ] ||
				sed "s/^/[stderr] /" < stderr
		fi

		# Replace CR used by Git progress messages
		tr '\r' '\n' < .test-result
	elif [ -n "$verbose" ]; then
		count="$(grep -c '^ *\[OK\]' < .test-result || true)"
		printf 'Passed %d assertions\n' "$count"
	fi | sed "s/^/$indent| /"
}

trap "tty_reset; show_test_results" EXIT

test_skip()
{
	printf '%s\n' "$*" >> .test-skipped
}

test_todo_message()
{
	explanation="$*"
	if [ -n "$explanation" ]; then
		explanation=": $explanation"
	fi

	printf '[TODO] Not yet expected to pass%s\n' "$explanation"
}

### Testing API AsciiDoc
#|
#| test_todo([note, ...])::
#|
test_todo()
{
	if [ -n "$todos" ]; then
		return
	fi

	test_todo_message "$*" >> .test-skipped
}

### Testing API AsciiDoc
#|
#| test_timeout(seconds)::
#|
test_timeout()
{
	if [ -z "${1:-}" ]; then
		die 'test_timeout requires an argument'
	fi

	timeout="${1:-}"
}

### Testing API AsciiDoc
#|
#| require_git_version(version, [note, ...])::
#|
require_git_version()
{
	git_version="$(git version | sed 's/git version \([0-9\.]*\).*/\1/')"
	actual_major="$(expr "$git_version" : '\([0-9]*\).*')"
	actual_minor="$(expr "$git_version" : '[0-9]*\.\([0-9]*\).*')"

	required_version="$1"; shift
	required_major="$(expr "$required_version" : '\([0-9]*\).*')"
	required_minor="$(expr "$required_version" : '[0-9]*\.\([0-9]*\).*')"

	if [ "$required_major" -gt "$actual_major" ] ||
	   [ "$required_major" -eq "$actual_major" -a "$required_minor" -gt "$actual_minor" ]; then
		test_skip "$@"
	fi
}

has_readline()
{
	if tig --version | grep readline >/dev/null 2>&1; then
		return 0
	else
		return 1
	fi
}

### Testing API AsciiDoc
#|
#| test_require(git-worktree, address-sanitizer, diff-highlight, readline)::
#|
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
		diff-highlight|python)
			if [ "$feature" = "diff-highlight" ]; then
				for elt in "$(git --exec-path)/../../share/git-core/contrib/diff-highlight" \
					   "$(git --exec-path)/../../share/git/contrib/diff-highlight" \
					   ; do
					if [ -d "$elt" ]; then
						PATH="$PATH":"$elt"
					fi
				done
			fi
			if ! which "$feature" >/dev/null 2>&1; then
				test_skip "The test requires a '$feature' executable"
			fi
			;;
		python-termios)
			if ! python -c 'import termios; termios.TIOCSTI' >/dev/null 2>&1; then
				test_skip "The test requires python with termios.TIOCSTI support"
			fi
			;;
		readline)
			if ! has_readline; then
				test_skip "The test requires a tig compiled with readline"
			fi
			;;

		*)
			test_skip "Unknown feature requirement: $feature"
		esac
	done
}

test_exec_work_dir()
{
	cmd="$@"
	printf '=== %s ===\n' "$cmd" >> "$HOME/test-exec.log"
	test_exec_log="$HOME/test-exec.log.tmp"
	rm -f -- "$test_exec_log"

	set +e
	in_work_dir "$@" 1>>"$test_exec_log" 2>>"$test_exec_log"
	test_exec_exit_code=$?
	set -e

	cat < "$test_exec_log" >> "$HOME/test-exec.log"
	if [ "$test_exec_exit_code" != 0 ]; then
		printf "[FAIL] unexpected exit code while executing '%s': %s\n" "$cmd" "$test_exec_exit_code" >> .test-result
		cat < "$test_exec_log" >> .test-result
		# Exit gracefully to allow additional tests to run
		exit 0
	fi
}

### Testing API AsciiDoc
#|
#| test_setup()::
#|
test_setup()
{
	if [ -e .test-skipped ]; then
		exit 0
	fi

	run_setup="$(type test_setup_work_dir 2>/dev/null | grep 'function' || true)"

	if [ -n "$run_setup" ]; then
		if test ! -e "$HOME/test-exec.log" || ! grep -q test_setup_work_dir -- "$HOME/test-exec.log"; then
			test_exec_work_dir test_setup_work_dir
		fi
	fi
}

install_pid_timeout() {
	pid="${1:-}"
	signal="${2:-ALRM}"
	test "$timeout" -gt 0 || return
	test "$pid" -gt 0 || return
	test "$pid" != "$$" || return
	trap '' "$signal"
	(
	trap '' "$signal"
	count=0
	granularity=1
	while [ "$count" -lt "$timeout" ]; do
		count="$((count + granularity))"
		sleep "$granularity"
		kill -0 "$pid" || break
	done
	kill -0 "$pid" && kill -"$signal" "$pid" || true
	) >/dev/null 2>&1 &
}

simulate_keystrokes()
{
	if [ -z "${TEST_KEYSTROKES:-}" ] || ! [ -s "$TEST_KEYSTROKES" ]; then
		return
	fi

	if ! [ "${1:-}" -gt 1 ]; then
		die "simulate_keystrokes requires an argument: the pgid to attach to"
	else
		# We were probably passed the PID, and can assume PID == PGID.
		# Calling descend_to_pg_leader is safe but not usually needed.
		tig_pgid="$1"; shift
	fi

	if [ -n "$valgrind" ]; then

		# valgrind needs a generous delay to warm up
		# todo: it might need extra time on its first run
		sleep 5

		tig_pgid="$(descend_to_pg_leader "$tig_pgid" valgrind 2>/dev/null)"
		if [ -z "$tig_pgid" ]; then
			die "could not find a process-group leader"
		fi
	fi

	if [ -n "$trace_keys" ]; then
		printf '%s%s %s %s %s\n' "$indent" keystroke-stuffer --with-shutdown "$tig_pgid" "$TEST_KEYSTROKES"
		keystroke-stuffer --debug --debug "$tig_pgid" "$TEST_KEYSTROKES" | sed "s/^/$indent$indent/"
	fi

	keystroke-stuffer --with-shutdown "$tig_pgid" "$TEST_KEYSTROKES"
}

valgrind_exec()
{
	kernel="$(uname -s 2>/dev/null || printf 'unknown\n')"
	kernel_supp="$base_dir/tools/valgrind-$kernel.supp"

	valgrind_ops=""

	valgrind_supp="--suppressions=/dev/null"
	if [ -e "$kernel_supp" ]; then
		valgrind_supp="--suppressions=$kernel_supp"
	fi

	(
		IFS=' 	'
		valgrind -q --gen-suppressions=all --track-origins=yes --error-exitcode=1 \
			--log-file="$valgrind.orig" "$valgrind_supp" $valgrind_ops  \
			"$@"
	)
	valgrind_status_code=$?

	case "$kernel" in
		Darwin)	grep -v "mach_msg unhandled MACH_SEND_TRAILER option" < "$valgrind.orig" > "$valgrind" ;;
		*)	mv -- "$valgrind.orig" "$valgrind" ;;
	esac

	rm -f -- "$valgrind.orig"

	return "$valgrind_status_code"
}

### Testing API AsciiDoc
#|
#| test_tig()::
#|
#|	Set up a controlled environment and report the test result.
#|	Input to be processed via stdin is passed and stderr is captured and
#|	can be used for later assertions.
#|	Example
#| --------------------------------------------------------------------------------
#| test_tig show 1a2b3c4d5e6f
#| --------------------------------------------------------------------------------
#|
test_tig()
{
	name="$TEST_NAME"
	prefix="${name:+$name.}"

	test_setup
	export TIG_NO_DISPLAY=
	if [ -n "$trace" ]; then
		export TIG_TRACE="$HOME/${prefix}tig-trace"
	fi
	touch -- "${prefix}stdin" "${prefix}stderr"
	(
		# subshell handles cleanup of cwd, variables, redirections, set +e
		cd "$work_dir" || die "chdir failed"
		tty_reset
		if [ -n "$debugger" ]; then
			printf "*** Running tests in '%s/%s'\n" "$HOME" "$work_dir"
			if [ -s "$HOME/${prefix}stdin" ]; then
				printf '*** - This test requires data to be injected via stdin.\n'
				printf "***   The expected input file is: '%s'\n" "../${prefix}stdin"
			fi
			if [ "$#" -gt 0 ]; then
				printf '*** - This test expects the following arguments: %s\n' "$*"
			fi
			"$debugger" tig "$@"
		else
			set +e
			if [ "$expected_status_code" = 0 ] && [ -n "$valgrind" ]; then
				runner=valgrind_exec
				if [ "$timeout" -gt 0 ]; then
					timeout="$((timeout + vlg_timeout_bonus))"
				fi
			fi
			if [ -s "$HOME/${prefix}stdin" ]; then
				exec 4<"$HOME/${prefix}stdin"
			else
				exec 4<&0
			fi
			"$runner" tig "$@" <&4 > "$HOME/${prefix}stdout" 2> "$HOME/${prefix}stderr.orig" &
			tig_pid="$!"
			signal=14
			install_pid_timeout "$tig_pid" "$signal"
			simulate_keystrokes "$tig_pid"
			wait "$tig_pid"
		fi
		status_code="$?"
		tty_reset
		if [ "$status_code" -eq "$(( 256 + signal))" ] || [ "$status_code" -eq "$(( 128 + signal))" ]; then
			printf '[FAIL] Test timed out after %s seconds\n' "$timeout" >> "$HOME/.test-result"
		elif [ "$status_code" != "$expected_status_code" ]; then
			printf '[FAIL] unexpected status code: %s (should be %s)\n' "$status_code" "$expected_status_code" >> "$HOME/.test-result"
		fi
	)
	# Normalize paths in stderr output
	if [ -e "${prefix}stderr.orig" ]; then
		sed "s#$output_dir#HOME#" < "${prefix}stderr.orig" > "${prefix}stderr"
		rm -f -- "${prefix}stderr.orig"
	fi
	if [ -n "$trace" ]; then
		export TIG_TRACE="$HOME/.tig-trace"
		if [ -n "$name" ]; then
			sed "s#^#[$name] #" < "$HOME/${prefix}tig-trace" >> "$HOME/.tig-trace"
		else
			mv -- "$HOME/${prefix}tig-trace" "$HOME/.tig-trace"
		fi
	fi
	if [ -n "$prefix" ]; then
		sed "s#^#[$name] #" < "${prefix}stderr" >> "stderr"
	fi
}

### Testing API AsciiDoc
#|
#| test_graph() < expected::
#|
test_graph()
{
	test-graph "$@" > stdout 2> stderr.orig
}

### Testing API AsciiDoc
#|
#| test_case([--before=<string>, --after=<string>, --script=<string>, --args=<string>, --cwd=<string>, --tigrc=<string>, --assert-stderr=<string>, --todo=<string>, --subshell=<string>, --timeout=<string>]) < expected::
#|
test_case()
{
	name="$1"; shift

	printf '%s\n' "$name" >> test-cases
	cat > "$name.expected"

	touch -- "$name-before" "$name-after" "$name-script" "$name-args" "$name-tigrc" "$name-assert-stderr" \
		 "$name-todo" "$name-subshell" "$name-timeout" "$name-keystrokes"

	while [ "$#" -gt 0 ]; do
		arg="$1"; shift
		key="$(expr "X$arg" : 'X--\([^=]*\).*')"
		value="$(expr "X$arg" : 'X--[^=]*=\(.*\)')"

		case "$key" in
		before|after|script|args|cwd|tigrc|assert-stderr|todo|subshell|timeout|keystrokes)
			printf '%s\n' "$value" > "$name-$key" ;;
		assert-equals)
			filename="$(expr "X$value" : 'X\([^=]*\)')"
			content="$(expr "X$value" : 'X[^=]*=\(.*\)')"
			printf '%s\n' "$filename" > "$name-$key"
			printf '%s\n' "$content" > "$name-$key-content" ;;
		*)	die "Unknown test_case argument: $arg"
		esac
	done

	# hack to stop tests from wedging.  unsatisfactory that the script
	# file is modified implicitly in multiple places.
	if ! [ -s "$name-script" ] && ! [ -s "$name-keystrokes" ]; then
		printf ':none\n' > "$name-script"
	fi
}

### Testing API AsciiDoc
#|
#| run_test_cases()::
#|
run_test_cases()
{
	if [ ! -e test-cases ]; then
		return
	fi
	test_setup
	while read -r name <&3; do
		export TEST_CASE="$name"
		if [ -n "$filter" ]; then
			matcher="$name"
			_filter_case_part="${filter#*:}"
			if [ "$filter" != "$_filter_case_part" ]; then
				case "$matcher" in
					$_filter_case_part) true;;
					*) continue;;
				esac
			fi
		fi
		if [ "${V:-}" = '@' ]; then
			# align with output from make, based on $V which is inherited from make
			printf '      CASE  %s\n' "$0:$name"
		fi
		if [ -s "$name-todo" ] && [ -z "$todos" ]; then
			printf '%s[skipped] ' "$indent"
			test_todo_message "$(cat < "$name-todo")"
			test_todo_message "$(cat < "$name-todo")" >> ".test-skipped-subtest-$name"
			continue;
		fi
		if [ -s "$name-tigrc" ]; then
			tigrc "$(cat < "$name-tigrc")"
		fi
		if [ -e "$name-before" ]; then
			test_exec_work_dir "$SHELL" "$HOME/$name-before"
		fi
		(
			if [ -s "$name-script" ]; then
				tig_script "$name" "
					   $(cat < "$name-script")
					   :save-display $name.screen
					   "
			fi
			if [ -s "$name-keystrokes" ]; then
				tig_keystrokes "$name" "$(cat < "$name-keystrokes")"
			fi
			if [ -e "$name-cwd" ]; then
				work_dir="$work_dir/$(cat < "$name-cwd")"
			fi
			if [ -e ./"$name-subshell" ]; then
				. ./"$name-subshell"
			fi
			if [ -s "$name-timeout" ]; then
				timeout="$(cat < "$name-timeout")"
			fi
			IFS=' 	'
			test_tig $(if [ -e "$name-args" ]; then cat < "$name-args"; fi)
		)
		if [ -e "$name-after" ]; then
			test_exec_work_dir "$SHELL" "$HOME/$name-after"
		fi
		if [ -s "$name-script" ]; then
			assert_equals "$name.screen" < "$name.expected"
		fi
		if [ -s "$name-assert-stderr" ]; then
			assert_equals "$name.stderr" < "$name-assert-stderr"
		else
			assert_equals "$name.stderr" < /dev/null
		fi
		if [ -e "$name-assert-equals" ]; then
			assert_equals "$(cat < "$name-assert-equals")" < "$name-assert-equals-content"
		fi
	done 3< test-cases
}
