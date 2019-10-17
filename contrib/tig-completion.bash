# bash/zsh completion for tig
# 
# Copyright (C) 2019 Roland Hieber, Pengutronix
# Copyright (C) 2007-2010 Jonas fonseca
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

__tig_options="
	-v --version
	-h --help
	-C
	--
	+
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

_tig() {
	# parse already existing parameters
	local i c=1 command
	while [ $c -lt $cword ]; do
		i="${words[c]}"
		case "$i" in
		--)	command="log"; break;;
		-*)	;;
		*)	command="$i"; break ;;
		esac
		c=$((++c))
	done

	# options -- only before command
	case "$command$cur" in
		-C*)
			COMPREPLY=( $(compgen -d -P '-C' -- ${cur##-C}) )
			return
			;;
	esac

	# commands
	case "$command" in
		refs|status|stash)
			COMPREPLY=( $(compgen -W "$__tig_options" -- "$cur") )
			;;
		reflog)
			__git_complete_command log
			;;
		"")
			__git_complete_command log
			__gitcompappend "$(compgen -W "$__tig_options $__tig_commands" -- "$cur")"
			;;
		*)
			__git_complete_command $command
			;;
	esac
}

# Detect if current shell is ZSH, and if so, load this file in bash
# compatibility mode.
if [ -n "$ZSH_VERSION" ]; then
	autoload bashcompinit
	bashcompinit
fi

# we use internal git-completion functions, so wrap _tig for all necessary
# variables (like cword and prev) to be defined
__git_complete tig _tig 

# The following are necessary only for Cygwin, and only are needed
# when the user has tab-completed the executable name and consequently
# included the '.exe' suffix.
if [ Cygwin = "$(uname -o 2>/dev/null)" ]; then
	__git_complete tig.exe _tig
fi
