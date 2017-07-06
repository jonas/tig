##
# bash completion support for tig
#
# Copyright (C) 2007-2010 Jonas fonseca
# Copyright (C) 2006,2007 Shawn Pearce
#
# Based git's git-completion.sh: http://repo.or.cz/w/git/fastimport.git
#
# The contained completion routines provide support for completing:
#
#    *) local and remote branch names
#    *) local and remote tag names
#    *) tig 'subcommands'
#    *) tree paths within 'ref:path/to/file' expressions
#
# To use these routines:
#
#    1) Install and activate the base git completion
#    2) Copy this file to somewhere (e.g. ~/.tig-completion.sh).
#    3) Added the following line to your .bashrc:
#        source ~/.tig-completion.sh
#
#    4) You may want to make sure the git executable is available
#       in your PATH before this script is sourced, as some caching
#       is performed while the script loads.  If git isn't found
#       at source time then all lookups will be done on demand,
#       which may be slightly slower.
#

__tig_cmds="blame status show log refs stash grep"

__tig_complete_revlist_or_cmd ()
{
	local pfx ls ref cur_="$cur"
	case "$cur_" in
	*..?*:*)
		return
		;;
	?*:*)
		ref="${cur_%%:*}"
		cur_="${cur_#*:}"
		case "$cur_" in
		?*/*)
			pfx="${cur_%/*}"
			cur_="${cur_##*/}"
			ls="$ref:$pfx"
			pfx="$pfx/"
			;;
		*)
			ls="$ref"
			;;
		esac

		case "$COMP_WORDBREAKS" in
		*:*) : great ;;
		*)   pfx="$ref:$pfx" ;;
		esac

		__gitcomp_nl "$(git --git-dir="$(__gitdir)" ls-tree "$ls" 2>/dev/null \
				| sed '/^100... blob /{
				           s,^.*	,,
				           s,$, ,
				       }
				       /^120000 blob /{
				           s,^.*	,,
				           s,$, ,
				       }
				       /^040000 tree /{
				           s,^.*	,,
				           s,$,/,
				       }
				       s/^.*	//')" \
			"$pfx" "$cur_" ""
		;;
	*...*)
		pfx="${cur_%...*}..."
		cur_="${cur_#*...}"
		__gitcomp_nl "$(__git_refs)" "$pfx" "$cur_"
		;;
	*..*)
		pfx="${cur_%..*}.."
		cur_="${cur_#*..}"
		__gitcomp_nl "$(__git_refs)" "$pfx" "$cur_"
		;;
	*)
		__gitcomp "$__tig_cmds"
		__gitcomp_nl_append "$(__git_refs)"
		;;
	esac
}

_tig ()
{
	local cur words cword prev
	local i c=1 command

	# load all completion utilities from git
	_completion_loader git

	_get_comp_words_by_ref -n =: cur words cword prev

	while [ $c -lt $cword ]; do
		i="${words[c]}"
		case "$i" in
		--) command="log"; break;;
		-*) ;;
		*) command="$i"; break ;;
		esac
		((c++))
	done

	if [ $c -eq $cword -a -z "$command" ]; then
		case "$cur" in
		--*)
			_git_log
			;;
		-*)
			__gitcomp "-v -h"
			;;
		*)
			__tig_complete_revlist_or_cmd ;;
		esac
		return
	fi

	case "$command" in
	show)
		_git_show ;;
	log)
		_git_log ;;
	status)
		_git_status;;
	blame)
		__gitcomp "$(__git_complete_file)";;
	*)
		_git_log ;;
	esac
}

# Detect if current shell is ZSH, and if so, load this file in bash
# compatibility mode.
if [ -n "$ZSH_VERSION" ]; then
	autoload bashcompinit
	bashcompinit
fi

complete -o default -o nospace -F _tig tig

# The following are necessary only for Cygwin, and only are needed
# when the user has tab-completed the executable name and consequently
# included the '.exe' suffix.
if [ Cygwin = "$(uname -o 2>/dev/null)" ]; then
complete -o default -o nospace -F _tig tig.exe
fi
