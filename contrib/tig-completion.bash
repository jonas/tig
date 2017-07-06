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

# Utilities copied from mainline git completion and renamed from _git* to _tig*
# (9743f18f3fef0b77b8715cba256a740a7238f761)

__tig_find_repo_path ()
{
	if [ -n "$__tig_repo_path" ]; then
		# we already know where it is
		return
	fi

	if [ -n "${__tig_dir-}" ]; then
		test -d "$__tig_dir" &&
		__tig_repo_path="$__tig_dir"
	elif [ -n "${GIT_DIR-}" ]; then
		test -d "${GIT_DIR-}" &&
		__tig_repo_path="$GIT_DIR"
	elif [ -d .git ]; then
		__tig_repo_path=.git
	else
		__tig_repo_path="$(git rev-parse --git-dir 2>/dev/null)"
	fi
}

__tig_git ()
{
	git ${__tig_dir:+--git-dir="$__tig_dir"} "$@" 2>/dev/null
}

__tigcomp_direct ()
{
	local IFS=$'\n'

	COMPREPLY=($1)
}

__tigcompappend ()
{
	local x i=${#COMPREPLY[@]}
	for x in $1; do
		if [[ "$x" == "$3"* ]]; then
			COMPREPLY[i++]="$2$x$4"
		fi
	done
}

__tigcompadd ()
{
	COMPREPLY=()
	__tigcompappend "$@"
}

__tigcomp ()
{
	local cur_="${3-$cur}"

	case "$cur_" in
	--*=)
		;;
	*)
		local c i=0 IFS=$' \t\n'
		for c in $1; do
			c="$c${4-}"
			if [[ $c == "$cur_"* ]]; then
				case $c in
				--*=*|*.) ;;
				*) c="$c " ;;
				esac
				COMPREPLY[i++]="${2-}$c"
			fi
		done
		;;
	esac
}

__tigcomp_nl_append ()
{
	local IFS=$'\n'
	__tigcompappend "$1" "${2-}" "${3-$cur}" "${4- }"
}

__tigcomp_nl ()
{
	COMPREPLY=()
	__tigcomp_nl_append "$@"
}

__tigcomp_file ()
{
	local IFS=$'\n'

	# XXX does not work when the directory prefix contains a tilde,
	# since tilde expansion is not applied.
	# This means that COMPREPLY will be empty and Bash default
	# completion will be used.
	__tigcompadd "$1" "${2-}" "${3-$cur}" ""

	# use a hack to enable file mode in bash < 4
	compopt -o filenames +o nospace 2>/dev/null ||
	compgen -f /non-existing-dir/ > /dev/null
}

__tig_ls_files_helper ()
{
	if [ "$2" == "--committable" ]; then
		__tig_git -C "$1" diff-index --name-only --relative HEAD
	else
		# NOTE: $2 is not quoted in order to support multiple options
		__tig_git -C "$1" ls-files --exclude-standard $2
	fi
}

__tig_index_files ()
{
	local root="${2-.}" file

	__tig_ls_files_helper "$root" "$1" |
	while read -r file; do
		case "$file" in
		?*/*) echo "${file%%/*}" ;;
		*) echo "$file" ;;
		esac
	done | sort | uniq
}

__tig_refs ()
{
	local i hash dir track="${2-}"
	local list_refs_from=path remote="${1-}"
	local format refs
	local pfx="${3-}" cur_="${4-$cur}" sfx="${5-}"
	local match="${4-}"
	local fer_pfx="${pfx//\%/%%}" # "escape" for-each-ref format specifiers

	__tig_find_repo_path
	dir="$__tig_repo_path"

	if [ -z "$remote" ]; then
		if [ -z "$dir" ]; then
			return
		fi
	else
		if __tig_is_configured_remote "$remote"; then
			# configured remote takes precedence over a
			# local directory with the same name
			list_refs_from=remote
		elif [ -d "$remote/.git" ]; then
			dir="$remote/.git"
		elif [ -d "$remote" ]; then
			dir="$remote"
		else
			list_refs_from=url
		fi
	fi

	if [ "$list_refs_from" = path ]; then
		if [[ "$cur_" == ^* ]]; then
			pfx="$pfx^"
			fer_pfx="$fer_pfx^"
			cur_=${cur_#^}
			match=${match#^}
		fi
		case "$cur_" in
		refs|refs/*)
			format="refname"
			refs=("$match*" "$match*/**")
			track=""
			;;
		*)
			for i in HEAD FETCH_HEAD ORIG_HEAD MERGE_HEAD; do
				case "$i" in
				$match*)
					if [ -e "$dir/$i" ]; then
						echo "$pfx$i$sfx"
					fi
					;;
				esac
			done
			format="refname:strip=2"
			refs=("refs/tags/$match*" "refs/tags/$match*/**"
				"refs/heads/$match*" "refs/heads/$match*/**"
				"refs/remotes/$match*" "refs/remotes/$match*/**")
			;;
		esac
		__tig_dir="$dir" __tig_git for-each-ref --format="$fer_pfx%($format)$sfx" \
			"${refs[@]}"
		if [ -n "$track" ]; then
			# employ the heuristic used by git checkout
			# Try to find a remote branch that matches the completion word
			# but only output if the branch name is unique
			__tig_git for-each-ref --format="$fer_pfx%(refname:strip=3)$sfx" \
				--sort="refname:strip=3" \
				"refs/remotes/*/$match*" "refs/remotes/*/$match*/**" | \
			uniq -u
		fi
		return
	fi
	case "$cur_" in
	refs|refs/*)
		__tig_git ls-remote "$remote" "$match*" | \
		while read -r hash i; do
			case "$i" in
			*^{}) ;;
			*) echo "$pfx$i$sfx" ;;
			esac
		done
		;;
	*)
		if [ "$list_refs_from" = remote ]; then
			case "HEAD" in
			$match*)	echo "${pfx}HEAD$sfx" ;;
			esac
			__tig_git for-each-ref --format="$fer_pfx%(refname:strip=3)$sfx" \
				"refs/remotes/$remote/$match*" \
				"refs/remotes/$remote/$match*/**"
		else
			local query_symref
			case "HEAD" in
			$match*)	query_symref="HEAD" ;;
			esac
			__tig_git ls-remote "$remote" $query_symref \
				"refs/tags/$match*" "refs/heads/$match*" \
				"refs/remotes/$match*" |
			while read -r hash i; do
				case "$i" in
				*^{})	;;
				refs/*)	echo "$pfx${i#refs/*/}$sfx" ;;
				*)	echo "$pfx$i$sfx" ;;  # symbolic refs
				esac
			done
		fi
		;;
	esac
}

__tig_complete_refs ()
{
	local remote track pfx cur_="$cur" sfx=" "

	while test $# != 0; do
		case "$1" in
		--remote=*)	remote="${1##--remote=}" ;;
		--track)	track="yes" ;;
		--pfx=*)	pfx="${1##--pfx=}" ;;
		--cur=*)	cur_="${1##--cur=}" ;;
		--sfx=*)	sfx="${1##--sfx=}" ;;
		*)		return 1 ;;
		esac
		shift
	done

	__tigcomp_direct "$(__tig_refs "$remote" "$track" "$pfx" "$cur_" "$sfx")"

}

__tig_remotes ()
{
	__tig_find_repo_path
	test -d "$__tig_repo_path/remotes" && ls -1 "$__tig_repo_path/remotes"
	__tig_git remote
}

__tig_is_configured_remote ()
{
	local remote
	for remote in $(__tig_remotes); do
		if [ "$remote" = "$1" ]; then
			return 0
		fi
	done
	return 1
}

__tig_complete_revlist_file ()
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

		__tigcomp_nl "$(__tig_git ls-tree "$ls" \
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
		__tig_complete_refs --pfx="$pfx" --cur="$cur_"
		;;
	*..*)
		pfx="${cur_%..*}.."
		cur_="${cur_#*..}"
		__tig_complete_refs --pfx="$pfx" --cur="$cur_"
		;;
	*)
		__tig_complete_refs
		;;
	esac
}

__tig_complete_index_file ()
{
	local pfx="" cur_="$cur"

	case "$cur_" in
	?*/*)
		pfx="${cur_%/*}"
		cur_="${cur_##*/}"
		pfx="${pfx}/"
		;;
	esac

	__tigcomp_file "$(__tig_index_files "$1" ${pfx:+"$pfx"})" "$pfx" "$cur_"
}

__tig_complete_file ()
{
	__tig_complete_revlist_file
}

__tig_complete_revlist ()
{
	__tig_complete_revlist_file
}

__tig_get_config_variables ()
{
	local section="$1" i IFS=$'\n'
	for i in $(__tig_git config --name-only --get-regexp "^$section\..*"); do
		echo "${i#$section.}"
	done
}

__tig_pretty_aliases ()
{
	__tig_get_config_variables "pretty"
}

__tig_get_option_value ()
{
	local c short_opt long_opt val
	local result= values config_key word

	short_opt="$1"
	long_opt="$2"
	values="$3"
	config_key="$4"

	((c = $cword - 1))
	while [ $c -ge 0 ]; do
		word="${words[c]}"
		for val in $values; do
			if [ "$short_opt$val" = "$word" ] ||
			   [ "$long_opt$val"  = "$word" ]; then
				result="$val"
				break 2
			fi
		done
		((c--))
	done

	if [ -n "$config_key" ] && [ -z "$result" ]; then
		result="$(__tig_git config "$config_key")"
	fi

	echo "$result"
}

__tig_find_on_cmdline ()
{
	local word subcommand c=1
	while [ $c -lt $cword ]; do
		word="${words[c]}"
		for subcommand in $1; do
			if [ "$subcommand" = "$word" ]; then
				echo "$subcommand"
				return
			fi
		done
		((c++))
	done
}

__tig_has_doubledash ()
{
	local c=1
	while [ $c -lt $cword ]; do
		if [ "--" = "${words[c]}" ]; then
			return 0
		fi
		((c++))
	done
	return 1
}

__tig_match_ctag () {
	awk -v pfx="${3-}" -v sfx="${4-}" "
		/^${1//\//\\/}/ { print pfx \$1 sfx }
		" "$2"
}

__tig_complete_symbol () {
	local tags=tags pfx="" cur_="${cur-}" sfx=" "

	while test $# != 0; do
		case "$1" in
		--tags=*)	tags="${1##--tags=}" ;;
		--pfx=*)	pfx="${1##--pfx=}" ;;
		--cur=*)	cur_="${1##--cur=}" ;;
		--sfx=*)	sfx="${1##--sfx=}" ;;
		*)		return 1 ;;
		esac
		shift
	done

	if test -r "$tags"; then
		__tigcomp_direct "$(__tig_match_ctag "$cur_" "$tags" "$pfx" "$sfx")"
	fi
}

__tig_diff_algorithms="myers minimal patience histogram"

__tig_diff_submodule_formats="diff log short"

__tig_diff_common_options="--stat --numstat --shortstat --summary
			--patch-with-stat --name-only --name-status --color
			--no-color --color-words --no-renames --check
			--full-index --binary --abbrev --diff-filter=
			--find-copies-harder
			--text --ignore-space-at-eol --ignore-space-change
			--ignore-all-space --ignore-blank-lines --exit-code
			--quiet --ext-diff --no-ext-diff
			--no-prefix --src-prefix= --dst-prefix=
			--inter-hunk-context=
			--patience --histogram --minimal
			--raw --word-diff --word-diff-regex=
			--dirstat --dirstat= --dirstat-by-file
			--dirstat-by-file= --cumulative
			--diff-algorithm=
			--submodule --submodule=
"

__tig_log_common_options="
	--not --all
	--branches --tags --remotes
	--first-parent --merges --no-merges
	--max-count=
	--max-age= --since= --after=
	--min-age= --until= --before=
	--min-parents= --max-parents=
	--no-min-parents --no-max-parents
"
__tig_log_gitk_options="
	--dense --sparse --full-history
	--simplify-merges --simplify-by-decoration
	--left-right --notes --no-notes
"
__tig_log_shortlog_options="
	--author= --committer= --grep=
	--all-match --invert-grep
"

__tig_log_pretty_formats="oneline short medium full fuller email raw format:"
__tig_log_date_formats="relative iso8601 rfc2822 short local default raw"

_tig_log ()
{
	__tig_has_doubledash && return
	__tig_find_repo_path

	local merge=""
	if [ -f "$__tig_repo_path/MERGE_HEAD" ]; then
		merge="--merge"
	fi
	case "$prev,$cur" in
	-L,:*:*)
		return	# fall back to Bash filename completion
		;;
	-L,:*)
		__tig_complete_symbol --cur="${cur#:}" --sfx=":"
		return
		;;
	-G,*|-S,*)
		__tig_complete_symbol
		return
		;;
	esac
	case "$cur" in
	--pretty=*|--format=*)
		__tigcomp "$__tig_log_pretty_formats $(__tig_pretty_aliases)
			" "" "${cur#*=}"
		return
		;;
	--date=*)
		__tigcomp "$__tig_log_date_formats" "" "${cur##--date=}"
		return
		;;
	--decorate=*)
		__tigcomp "full short no" "" "${cur##--decorate=}"
		return
		;;
	--diff-algorithm=*)
		__tigcomp "$__tig_diff_algorithms" "" "${cur##--diff-algorithm=}"
		return
		;;
	--submodule=*)
		__tigcomp "$__tig_diff_submodule_formats" "" "${cur##--submodule=}"
		return
		;;
	--*)
		__tigcomp "
			$__tig_log_common_options
			$__tig_log_shortlog_options
			$__tig_log_gitk_options
			--root --topo-order --date-order --reverse
			--follow --full-diff
			--abbrev-commit --abbrev=
			--relative-date --date=
			--pretty= --format= --oneline
			--show-signature
			--cherry-mark
			--cherry-pick
			--graph
			--decorate --decorate=
			--walk-reflogs
			--parents --children
			$merge
			$__tig_diff_common_options
			--pickaxe-all --pickaxe-regex
			"
		return
		;;
	-L:*:*)
		return	# fall back to Bash filename completion
		;;
	-L:*)
		__tig_complete_symbol --cur="${cur#-L:}" --sfx=":"
		return
		;;
	-G*)
		__tig_complete_symbol --pfx="-G" --cur="${cur#-G}"
		return
		;;
	-S*)
		__tig_complete_symbol --pfx="-S" --cur="${cur#-S}"
		return
		;;
	esac
	__tig_complete_revlist
}

_tig_show ()
{
	__tig_has_doubledash && return

	case "$cur" in
	--pretty=*|--format=*)
		__tigcomp "$__tig_log_pretty_formats $(__tig_pretty_aliases)
			" "" "${cur#*=}"
		return
		;;
	--diff-algorithm=*)
		__tigcomp "$__tig_diff_algorithms" "" "${cur##--diff-algorithm=}"
		return
		;;
	--submodule=*)
		__tigcomp "$__tig_diff_submodule_formats" "" "${cur##--submodule=}"
		return
		;;
	--*)
		__tigcomp "--pretty= --format= --abbrev-commit --oneline
			--show-signature
			$__tig_diff_common_options
			"
		return
		;;
	esac
	__tig_complete_revlist_file
}

__tig_untracked_file_modes="all no normal"

_tig_status ()
{
	local complete_opt
	local untracked_state

	case "$cur" in
	--ignore-submodules=*)
		__tigcomp "none untracked dirty all" "" "${cur##--ignore-submodules=}"
		return
		;;
	--untracked-files=*)
		__tigcomp "$__tig_untracked_file_modes" "" "${cur##--untracked-files=}"
		return
		;;
	--column=*)
		__tigcomp "
			always never auto column row plain dense nodense
			" "" "${cur##--column=}"
		return
		;;
	--*)
		__tigcomp "
			--short --branch --porcelain --long --verbose
			--untracked-files= --ignore-submodules= --ignored
			--column= --no-column
			"
		return
		;;
	esac

	untracked_state="$(__tig_get_option_value "-u" "--untracked-files=" \
		"$__tig_untracked_file_modes" "status.showUntrackedFiles")"

	case "$untracked_state" in
	no)
		# --ignored option does not matter
		complete_opt=
		;;
	all|normal|*)
		complete_opt="--cached --directory --no-empty-directory --others"

		if [ -n "$(__git_find_on_cmdline "--ignored")" ]; then
			complete_opt="$complete_opt --ignored --exclude=*"
		fi
		;;
	esac

	__git_complete_index_file "$complete_opt"
}

# main tig completion

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

		__tigcomp_nl "$(__tig_git ls-tree "$ls" \
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
		__tigcomp_nl "$(__tig_refs)" "$pfx" "$cur_"
		;;
	*..*)
		pfx="${cur_%..*}.."
		cur_="${cur_#*..}"
		__tigcomp_nl "$(__tig_refs)" "$pfx" "$cur_"
		;;
	*)
		__tigcomp "$__tig_cmds"
		__tigcomp_nl_append "$(__tig_refs)"
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
			_tig_log
			;;
		-*)
			__tigcomp "-v -h"
			;;
		*)
			__tig_complete_revlist_or_cmd ;;
		esac
		return
	fi

	case "$command" in
	show)
		_tig_show ;;
	log)
		_tig_log ;;
	status)
		_tig_status;;
	blame)
		__tigcomp "$(__tig_complete_file)";;
	*)
		_tig_log ;;
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
