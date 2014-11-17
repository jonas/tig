#!/bin/sh

. libgit.sh

git reset --hard

change() {
	echo "$@" > conflict-file
	git add conflict-file
	git_commit --message="Change: $@"
}

change "a"
change "b"

git branch conflict-master
git branch conflict-branch

git checkout conflict-master
change "c"
change "c'"

git checkout conflict-branch
change "d"
change "d'"

git checkout conflict-master
git merge conflict-branch || true
