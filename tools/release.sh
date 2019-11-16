#!/bin/sh
#
# Script for preparing a release or updating the release branch.
# Usage: $0 version
#
# Copyright (c) 2009-2014 Jonas Fonseca <jonas.fonseca@gmail.com>
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

set -e
set -x

VERSION="$1"

if which gsed >/dev/null; then SED=gsed; else SED=sed; fi

TAG="tig-$VERSION"
TITLE="$TAG\n$(echo "$TAG" | $SED 's/./-/g')"
NEWS="NEWS.adoc"

# Require a clean repository.
git update-index --refresh
git diff-index --quiet HEAD

if test -n "$VERSION"; then
	# Get a sane starting point.
	test "$(git symbolic-ref HEAD)" = "refs/heads/master" ||
		git checkout master

	# Update files which should reference the version.
	$SED -i "s/VERSION\s*=\s*[0-9.]\+/VERSION	= $VERSION/" Makefile
	$SED -i "s#tig-[0-9.]\+[0-9]\+#tig-$VERSION#g" INSTALL.adoc
	perl -pi -e 's/^master$/RELEASE_TITLE/ms' "$NEWS"
	perl -pi -e 's/^RELEASE_TITLE.*/RELEASE_TITLE/ms' "$NEWS"
	perl -pi -e "s/^RELEASE_TITLE.*/$TITLE/" "$NEWS"

	# Check for typos.
	make spell-check

	# Last review.
	${EDITOR:-vi} "$NEWS"

	# Create release commit and tag.
	git commit -a -m "$TAG"
	git tag -s -m "tig version $VERSION" "$TAG"

	# Prepare release announcement file.
	./tools/announcement.sh "$TAG" > "$TAG.txt"

	# Set version for the Makefile
	export DIST_VERSION="$VERSION"
else
	# Get meaningful version for the update message.
	TAG="$(git describe)"
fi

# Update the release branch.
git checkout release
HEAD="$(git rev-parse release)"
git merge --ff master
if test -n "$(git rev-list -1 release ^$HEAD)"; then
	make distclean doc-man doc-html sysconfdir=++SYSCONFDIR++
	git commit -a -m "Update for version $TAG"
fi

if test -n "$VERSION"; then
	# Create the tarball.
	make dist
fi

# Done.
git checkout master
