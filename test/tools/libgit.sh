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

create_repo_one()
{
	git_init "$1"
	(cd "$1" && {

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
	})
}

submodule_pull()
{
	repo_name="$(basename "$1")"
	cd "$1"; shift

	git submodule foreach git checkout master
	git submodule foreach git pull

	repos=""
	repos_sep=
	clog=""
	for repo in $@; do
		repo="$(basename "$repo")"
		repos="$repos$repos_sep$repo"
		repos_sep=", "

		summary="$(git submodule summary "$repo")"
		revspec="$(git diff --submodule=log "$repo" | sed -n '/Submodule/,1 s/Submodule [^ ]* \([^:]*\):/\1/p')"
		diffstat="$(cd "$repo" && git diff --stat "$revspec")"
		clog="$clog

$summary

$diffstat"
		git add "$repo"
	done

	git_commit --author="$IDENT_A" --message="[$repo_name] Integrate feature from $repos"
}

submodule_commit()
{
	repo_name="$(basename "$1")"
	cd "$1"; shift

	for file in $@; do
		mkdir -p "$(dirname "$file")"
		echo "$file" >> "$file"
		git add "$file"
	done
	git_commit --author="$IDENT_A" --message="[$repo_name] Commit $(git rev-list HEAD 2> /dev/null | wc -l | sed 's/ *//')"
}

submodule_create()
{
	repo_name="$(basename "$1")"
	cd "$1"; shift

	git submodule init
	for repo in $@; do
		git submodule add "../../git-repos/$(basename "$repo")"
	done

	git_commit --author="$IDENT_A" --message="[$repo_name] Creating repository"
}

create_repo_two()
{
	repo_main="$1"
	repo_a="$1-a"
	repo_b="$1-b"
	repo_c="$1-c"

	for repo in "$repo_main" "$repo_a" "$repo_b" "$repo_c"; do
		git_init "$repo"

		submodule_commit "$repo" Makefile
		submodule_commit "$repo" include/api.h
		submodule_commit "$repo" src/impl.c
	done

	submodule_create "$repo_main" "$repo_a" "$repo_b" "$repo_c"

	submodule_commit "$repo_a" include/api.h
	submodule_commit "$repo_a" src/impl.c

	submodule_commit "$repo_c" README
	submodule_commit "$repo_c" INSTALL

	submodule_pull "$repo_main" "$repo_a" "$repo_c"

	submodule_commit "$repo_b" README
	submodule_commit "$repo_b" Makefile

	submodule_pull "$repo_main" "$repo_b"

	submodule_commit "$repo_a" README
	submodule_commit "$repo_c" src/impl.c
	submodule_commit "$repo_b" include/api.h

	submodule_pull "$repo_main" "$repo_a" "$repo_b" "$repo_c"
}

create_repo()
{
	if [ ! -d "$1" ]; then
		case "$(basename "$1")" in
			repo-one) create_repo_one "$1" ;;
			repo-two) (create_repo_two "$1") ;;
			*) die "No generator for $(basname "$1")" ;;
		esac
	fi
}

create_repo_from_tgz()
{
	git_init .
	tar zxf "$1"
	git reset -q --hard
}

git_clone()
{
	create_repo "$tmp_dir/git-repos/$1"

	clone_dir="${2:-$work_dir}"
	git clone -q "$tmp_dir/git-repos/$1" "$clone_dir"
	(cd "$clone_dir" && git_config)
}
