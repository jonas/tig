# bash/zsh completion for tig
#
# Copyright (C) 2019 Roland Hieber, Pengutronix
# Copyright (C) 2006-2024 Jonas fonseca
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License as
# published by the Free Software Foundation; either version 2 of
# the License, or (at your option) any later version.
#
# This completion builds upon the git completion (>= git 1.17.11),
# which most tig users should already have available at this point.
# To use these routines:
#
#    1) Copy this file to somewhere (e.g. ~/.bash_completion.d/tig).
#
#    2) Add the following line to your .bashrc:
#
#           source ~/.bash_completion.d/tig
#
#       Note that most Linux distributions source everything in
#       ~/.bash_completion.d/ automatically at bash startup, so you
#       have to source this script manually only in shells that were
#       already running before.
#
#    3) You may want to make sure the git executable is available
#       in your PATH before this script is sourced, as some caching
#       is performed while the script loads.  If git isn't found
#       at source time then all lookups will be done on demand,
#       which may be slightly slower.

#tig-completion requires __git_complete
#* If not defined, source git completions script so __git_complete is available
if ! declare -f __git_complete &>/dev/null; then
	_bash_completion=$(pkg-config --variable=completionsdir bash-completion 2>/dev/null) ||
		_bash_completion='/usr/share/bash-completion/completions/'
			_locations=(
				"$(dirname "${BASH_SOURCE[0]%:*}")"/git-completion.bash #in same dir as this
				"$HOME/.local/share/bash-completion/completions/git"
				"$_bash_completion/git"
				'/etc/bash_completion.d/git' # old debian
			)
			for _e in "${_locations[@]}"; do
				# shellcheck disable=1090
				test -f "$_e" && . "$_e" && break
			done
			unset _bash_completion _locations _e
			if ! declare -f __git_complete &>/dev/null; then
				return #silently return without completions
			fi
fi

__tig_options="
	-v --version
	-h --help
	-C
"
__tig_commands="
	blame
	grep
	log
	reflog
	refs
	stash
	status
	show
"

__tig_main () {
	# parse already existing parameters
	local i c=1 command __git_repo_path
	while [ $c -lt $cword ]; do
		i="${words[c]}"
		case "$i" in
		--)	command="log"; break;;
		-C)	return;;
		-*)	;;
		*)	command="$i"; break ;;
		esac
		c=$((++c))
	done

	# commands
	case "$command" in
		refs|status|stash)
			__gitcomp "$__tig_options"
			;;
		reflog)
			__git_complete_command log
			;;
		"")
			__git_complete_command log
			__gitcomp "$__tig_options $__tig_commands"
			;;
		*)
			__git_complete_command $command
			;;
	esac
}

# we use internal git-completion functions, so wrap _tig for all necessary
# variables (like cword and prev) to be defined
__git_complete tig __tig_main

# The following are necessary only for Cygwin, and only are needed
# when the user has tab-completed the executable name and consequently
# included the '.exe' suffix.
if [ Cygwin = "$(uname -o 2>/dev/null)" ]; then
	__git_complete tig.exe __tig_main
fi
