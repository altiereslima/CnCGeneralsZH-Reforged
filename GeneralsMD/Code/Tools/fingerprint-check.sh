#!/usr/bin/env bash
#	Copyright 2026 İlyas Akın
#	Additional terms under GNU GPL section 7 apply: see LICENSE.md.
#
#	This program is free software: you can redistribute it and/or modify
#	it under the terms of the GNU General Public License as published by
#	the Free Software Foundation, either version 3 of the License, or
#	(at your option) any later version.
#
#	This program is distributed in the hope that it will be useful,
#	but WITHOUT ANY WARRANTY; without even the implied warranty of
#	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#	GNU General Public License for more details.
#
#	You should have received a copy of the GNU General Public License
#	along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
# N1's check: the build fingerprint is the same for two checkouts of one commit that differ only in
# line endings, changes with any one byte of a source, and is what the build put in its header.
#
#   fingerprint-check.sh <build_fingerprint> <generated BuildFingerprint.h>
#
#   1. the header the build generated holds the value --print gives over this source tree;
#   2. a copy of every listed file with its text converted to CRLF, as a Windows checkout with
#      core.autocrlf makes it (a file with a NUL in its first 8000 bytes is binary and left alone, as
#      git decides), gives the same value;
#   3. the LF copy with one byte of one source changed gives a different value, and so does a
#      listed file renamed (its path is hashed too).
# What it cannot see: a real Windows checkout. The CRLF tree is made here the way git would make it.
#
# Exit status: 0 on a pass, 1 otherwise, 2 without a work folder.

set -u
TOOL="$1"; HEADER="$2"
CODE="$(cd "$(dirname "$0")/.." && pwd)"
MANIFEST="$CODE/BuildFingerprint.manifest"
# A work folder that could not be made is the end of the run: going on with WORK empty would put
# "$WORK/..." at the file system's root.
WORK="$(mktemp -d "${TMPDIR:-/tmp}/fingerprint-check.XXXXXX")" || WORK=""
if [ -z "$WORK" ] || [ ! -d "$WORK" ]; then
	echo "fingerprint-check: cannot make a work folder under ${TMPDIR:-/tmp}" >&2
	exit 2
fi
trap 'rm -rf -- "${WORK:?}"' EXIT
status=0

value() { "$TOOL" --print "$1" "$MANIFEST" | awk '{print $1}'; }

here=$(value "$CODE")
built=$(sed -n 's/^#define ZH_BUILD_FINGERPRINT \(0x[0-9A-F]*\)u$/\1/p' "$HEADER")
if [ -n "$here" ] && [ "$here" = "$built" ]; then
	echo "ok: the build's header holds this tree's fingerprint ($here)"
else
	echo "FAIL: the header says ${built:-nothing}, this tree gives ${here:-nothing}"; status=1
fi

# Two copies of the listed files, in one perl: LF (every run of CRs before an LF removed), and the text
# ones as a Windows checkout with core.autocrlf makes them (every lone LF made CRLF).  A file with a NUL
# in its first 8000 bytes is binary, as git decides, and copied as it is.
perl -e '
	my ($code, $work, $manifest) = @ARGV;
	open( my $list, "<", $manifest ) or die "$manifest: $!";
	while (my $f = <$list>) {
		chomp $f; next unless length $f;
		open( my $in, "<:raw", "$code/$f" ) or die "$code/$f: $!";
		local $/; my $bytes = <$in>; close $in;
		for my $side ("lf", "crlf") {
			my $dir = "$work/$side/$f"; $dir =~ s{/[^/]*$}{};
			system( "mkdir", "-p", $dir ) unless -d $dir;
			my $out = $bytes;
			if ($side eq "lf") { $out =~ s/\r+\n/\n/g; }
			elsif (substr( $bytes, 0, 8000 ) !~ /\x00/) { $out =~ s/(?<!\r)\n/\r\n/g; }
			open( my $o, ">:raw", "$work/$side/$f" ) or die "$work/$side/$f: $!";
			print $o $out; close $o;
		}
	}' "$CODE" "$WORK" "$MANIFEST" || { echo "FAIL: could not copy the tree"; exit 1; }
lf=$(value "$WORK/lf"); crlf=$(value "$WORK/crlf")
if [ "$lf" = "$here" ] && [ "$crlf" = "$here" ]; then
	echo "ok: an LF copy and a CRLF copy (as autocrlf makes it) of all $(wc -l < "$MANIFEST" | tr -d ' ') files give the same value"
else
	echo "FAIL: LF copy $lf, CRLF copy $crlf, the tree $here"; status=1
fi

# One byte of one source changed
victim="GameEngine/Source/Common/GlobalData.cpp"
perl -pi -e 's/Whee!/Whee?/' "$WORK/lf/$victim"
changed=$(value "$WORK/lf")
if [ "$changed" != "$here" ]; then
	echo "ok: one byte of $victim changed gives another value ($changed)"
else
	echo "FAIL: one byte changed and the value did not"; status=1
fi
perl -pi -e 's/Whee\?/Whee!/' "$WORK/lf/$victim"
[ "$(value "$WORK/lf")" = "$here" ] || { echo "FAIL: restoring the byte did not restore the value"; status=1; }

# A listed file renamed: the paths are hashed with the content
renamed="$WORK/manifest.renamed"
sed "s|^$victim\$|${victim%.cpp}2.cpp|" "$MANIFEST" > "$renamed"
cp "$WORK/lf/$victim" "$WORK/lf/${victim%.cpp}2.cpp"
moved=$("$TOOL" --print "$WORK/lf" "$renamed" | awk '{print $1}')
if [ "$moved" != "$here" ]; then
	echo "ok: the same bytes under another name give another value"
else
	echo "FAIL: a rename left the value as it was"; status=1
fi
exit $status
