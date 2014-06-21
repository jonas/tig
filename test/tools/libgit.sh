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

work_dir="work dir"
mkdir -p "$output_dir/$work_dir"

author_date=1234567890
author_date_delta=735730

IDENT_A="A. U. Thor <a.u.thor@example.com>"
IDENT_B="René Lévesque <rene.levesque@example.qc.ca>"
IDENT_C="作者 <zuozhea@example.ch>"
IDENT_D="Jørgen Thygesen Brahe <brache@example.dk>"
IDENT_E="Max Power <power123@example.org>"

in_work_dir()
{
	(cd "$work_dir" && $@)
}

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
	[ -z "$GIT_COMMITTER_DATE" ] &&
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

create_repo_one()
{
	for i in $(seq 1 10); do
		git_commit --author="$IDENT_A" --message="Commit $i A"
		git_commit --author="$IDENT_B" --message="Commit $i B"
		git_commit --author="$IDENT_C" --message="Commit $i C"
		git_commit --author="$IDENT_D" --message="Commit $i D"
		git_commit --author="$IDENT_E" --message="Commit $i E"
	done

	remote=upstream
	git remote add $remote http://example.org/repo.git
	mkdir -p .git/refs/remotes/$remote
	echo 5633519083f21695dda4fe1f546272abb80668cd > .git/refs/remotes/$remote/master

	tagged=957f2b368e6fa5c0757f36b1441e32729ee5e9c7
	git tag v1.0 $tagged
}

git_clone()
{
	cwd="$(pwd)"
	name="$1"
	repo="$tmp_dir/git-repos/$name"

	if [ ! -d "$repo" ]; then
		git_init "$repo"
		cd "$repo"
		case "$name" in
			repo-one) create_repo_one ;;
			*) exit 1 ;;
		esac
		cd "$cwd"
	fi
	git clone -q "$repo" "$work_dir"
	cd "$work_dir"
	git_config
	cd "$cwd"
}
