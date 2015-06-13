#!/bin/sh
#
# Git test helpers.
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

author_date=1234567890
author_date_delta=735730

IDENT_A="A. U. Thor <a.u.thor@example.com>"
IDENT_B="René Lévesque <rene.levesque@example.qc.ca>"
IDENT_C="作者 <zuozhea@example.ch>"
IDENT_D="Jørgen Thygesen Brahe <brache@example.dk>"
IDENT_E="Max Power <power123@example.org>"

git_config()
{
	git config --local user.name "Committer"
	git config --local user.email "c.ommitter@example.net"
}

git_init()
{
	dir="${1:-$work_dir}"

	if [ ! -d "$dir/.git" ]; then
		git init -q "$dir"
		(cd "$dir" && git_config)
	fi
}

git_add()
{
	[ -d .git ] || die "git_add called outside of git repo"
	path="$1"; shift

	mkdir -p "$(dirname "$path")"
	file "$path" "$@"
	git add "$path"
}

git_commit()
{
	[ -d .git ] || die "git_commit called outside of git repo"

	GIT_COMMITTER_NAME="$0"
	GIT_COMMITTER_EMAIL="$0"

	export GIT_AUTHOR_DATE="$author_date"
	author_date="$(expr $author_date + $author_date_delta)"
	[ -z "${GIT_COMMITTER_DATE:-}" ] &&
		export GIT_COMMITTER_DATE="$GIT_AUTHOR_DATE"

	git commit -q --allow-empty "$@"
	unset GIT_COMMITTER_DATE
}

create_dirty_workdir()
{
	git init -q .
	git_config

	echo "*.o" > .gitignore
	echo "*~" > .git/info/exclude

	for file in a b.c "d~" e/f "g h" i.o .j "h~/k"; do
		dir="$(dirname "$file")"
		[ -n "$dir" ] && mkdir -p "$dir"
		printf "%s\n%s" "$file" "$(seq 1 10)" > "$file"
	done

	git add .
	git_commit --author="$IDENT_A" --message="Initial commit"

	for file in a b.c "d~" e/f "g h" i.o .j "h~/k"; do
		printf "%s\n%s" "$file CHANGED" "$(seq 1 8)" > "$file"
	done
}

create_repo_from_tgz()
{
	git_init .
	tar zxf "$1"
	git reset -q --hard
}

git_clone()
{
	repo_tgz="$base_dir/files/$1.tgz"
	if [ -e "$repo_tgz" ]; then
		clone_dir="${2:-$work_dir}"
		(cd "$clone_dir" && {
			git_init .
			tar zxf "$repo_tgz"
			git reset -q --hard
		})
	else
		die "No generator for $(basename "$1")"
	fi
}
