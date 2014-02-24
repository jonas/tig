#!/bin/sh
#
# Prepare documentation for http://jonas.nitro.dk/tig/
#
# Copyright (c) 2009-2014 Jonas Fonseca <jonas.fonseca@gmail.com>

LATEST="$(git describe | sed 's/-[0-9]*-g[0-9a-f]*//')"
DOCDIR="doc/release-docs/"

TRACKER="
<script type=\"text/javascript\">
var gaJsHost = ((\"https:\" == document.location.protocol) ? \"https://ssl.\" : \"http://www.\");
document.write(unescape(\"%3Cscript src='\" + gaJsHost + \"google-analytics.com/ga.js' type='text/javascript'%3E%3C/script%3E\"));
</script>
<script type=\"text/javascript\">
try {
var pageTracker = _gat._getTracker(\"UA-7492166-1\");
pageTracker._trackPageview();
} catch(err) {}</script>
"

mkdir -p "$DOCDIR/releases"

for file in doc/tig.1.html doc/tigrc.5.html doc/manual.html README.html NEWS.html INSTALL.html; do
	{
		git cat-file blob release:$file > tmp
		grep -v '</body>' < tmp | grep -v '</html>'
		echo "$TRACKER"
		if grep -q '</body>' tmp; then
			echo "</body></html>"
		fi
	} > "$DOCDIR/$(basename "$file")"
done

mv "$DOCDIR/README.html" tmp

{
	head -n 1 < tmp
	cat <<EOF
<h1>Tig Releases</h1>
<p>Latest version of tig is <b>
<a href="$LATEST.tar.gz">$LATEST</a>
<a href="$LATEST.tar.gz.md5">(md5)</a></b>.</p>
<p>See <a href="../NEWS.html">the release notes</a>
for information about each release.</p>
</div>
$TRACKER
EOF
} > "$DOCDIR/releases/HEADER.html"

head -n 2 < tmp > "$DOCDIR/HEADER.html"
sed -n '3,$p' < tmp > "$DOCDIR/README.html"
rm tmp
