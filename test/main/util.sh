#!/bin/sh

# Utilities to replace the date displayed as part of the index changes
# to a system independent dummy string.

YYY_MM_DD_HH_MM="YYYY_MM_DD_HH_MM"

main_replace_index_changes_date()
{
	D="[0-9][0-9]"

	for screen in $(ls *.screen); do
		mv "$screen" "$screen.orig"
		sed "s/^20$D-$D-$D $D:$D\( Unknown\)/$YYY_MM_DD_HH_MM\1/" < "$screen.orig" > "$screen"
	done
}
