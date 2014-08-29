#!/bin/sh

. libgit.sh

git reset --hard

change() {
	echo "$@" > conflict-file
	git add conflict-file
	git_commit --message="Change: $@"
}

git checkout -b conflict-master
change "a"

git checkout -b conflict-branch
change "b"

git checkout conflict-master
change "c"

git merge conflict-branch
