#!/bin/sh

create_repo_max_power()
{
	git_clone 'repo-one' "$1"
	(cd "$1" && {
		git branch -q mp/gh-123 HEAD~15
		git tag mp/good HEAD~10
		git checkout -q -b mp/feature master
		git_commit --author="$IDENT_E" --message="WIP: feature"
	})
}

