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
# The INI masters' line ends and the multiplayer INI CRC. INI::readLine turns each carriage return into a
# space and folds the line into the CRC, so an LF copy of the masters gives another INI CRC than the CRLF
# copy a Windows checkout has, and a Mac or Linux build made from it could not join a Windows player's
# game. .gitattributes checks the masters out CRLF everywhere, and Tools/stage-overlay.sh writes them CRLF
# whatever the checkout holds. Checked here:
#   1. without game data: the overlay staged from an LF copy of Code/Data and from a CRLF copy holds the
#      same Data/INI bytes, and every line of every one ends in CRLF;
#   2. with game data: the game logs the same INI CRC from both overlays, and the armed control, the LF
#      overlay as a build made it before the fix, gives another.
# RULE 9: the game runs on a farm of the install (a folder of links), never on the install, which is
# listed before and after and must be unchanged (Tools/install-guard.sh).
# Usage: run_ini_crlf_check.sh <generals> [<folder holding zerohour/>]   (ZH_DATA_DIR otherwise)
# Exit status: 0 passed; 1 failed; 77 when part 2 had no game data (part 1 passed).

set -u

GENERALS="${1:?usage: run_ini_crlf_check.sh <generals> [<data>]}"
DATA="${2:-${ZH_DATA_DIR:-}}"
CODE="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/ini-crlf-check.XXXXXX")" || WORK=""
if [ -z "$WORK" ] || [ ! -d "$WORK" ]; then
	echo "ini_crlf_check: cannot make a work folder under ${TMPDIR:-/tmp}" >&2
	exit 1
fi
EXEDIR="$(cd "$(dirname "$GENERALS")" && pwd)"
GENERALS="$EXEDIR/$(basename "$GENERALS")"
TAG="ic$$_"
cleanup() {
	rm -rf -- "${WORK:?}"
	rm -f -- "$EXEDIR/${TAG}"*DebugLogFile*.txt
}
trap cleanup EXIT

status=0
check() {	# check <test expression> <what it shows>
	if eval "$1"; then echo "ok: $2"; else echo "FAIL: $2"; status=1; fi
}

# ---- 1. the staging, from either checkout ----------------------------------------------------------
# Two copies of Code/Data, their INI masters LF in one and CRLF in the other, as a POSIX checkout made
# before the attribute and a Windows one hold them.
for ends in lf crlf; do
	mkdir -p "$WORK/$ends"
	cp -R "$CODE/Data" "$WORK/$ends/Data"
done
find "$WORK/lf/Data/INI" -type f -exec perl -pi -e 's/\r\n/\n/' {} +
find "$WORK/crlf/Data/INI" -type f -exec perl -pi -e 's/(?<!\r)\n/\r\n/' {} +
cr="$(printf '\r')"
check '! grep -rlq "$cr" "$WORK/lf/Data/INI"' "the LF copy of the masters holds no carriage return (the control's input)"
for ends in lf crlf; do
	"$CODE/Tools/stage-overlay.sh" "$WORK/$ends/Data" "" "$WORK/overlay-$ends" > "$WORK/stage-$ends.log" 2>&1 \
		|| { echo "FAIL: stage-overlay.sh from the $ends copy: $(tail -1 "$WORK/stage-$ends.log")"; exit 1; }
done
check 'diff -r "$WORK/overlay-lf/Data/INI" "$WORK/overlay-crlf/Data/INI" > /dev/null' \
	"the overlay staged from the LF masters and the one from the CRLF masters hold the same Data/INI bytes"
bare=0
for f in $(find "$WORK/overlay-lf/Data/INI" -type f); do
	[ "$(tr -cd '\n' < "$f" | wc -c)" -eq "$(tr -cd '\r' < "$f" | wc -c)" ] || { bare=1; echo "  bare LF in $f"; }
done
check '[ $bare -eq 0 ]' "every staged INI line ends in CRLF ($(find "$WORK/overlay-lf/Data/INI" -type f | wc -l | tr -d ' ') files)"

# ---- 2. the game's INI CRC -------------------------------------------------------------------------
if [ -z "$DATA" ] || [ ! -d "$DATA/zerohour" ]; then
	echo "skip: part 2, no game data (a folder holding zerohour/, as the second argument or ZH_DATA_DIR)"
	[ $status -eq 0 ] && exit 77
	exit 1
fi
if [ ! -x "$GENERALS" ]; then
	echo "FAIL: $GENERALS is not the POSIX generals executable"
	exit 1
fi
INSTALL="$(cd "$DATA/zerohour" && pwd)"
. "$CODE/Tools/install-guard.sh"	# install_snapshot, install_verify
if ! install_snapshot "$INSTALL" "$WORK/install.before"; then
	echo "FAIL: COULD NOT VERIFY the install: its listing before the run could not be made; nothing was run"
	exit 1
fi
ROOT="$WORK/farm"
mkdir -p "$ROOT"
( cd "$INSTALL" && find . -type d ! -name '._*' ) | while IFS= read -r d; do mkdir -p "$ROOT/$d"; done
( cd "$INSTALL" && find . -type f ! -name '._*' ) | while IFS= read -r f; do ln -s "$INSTALL/${f#./}" "$ROOT/$f"; done
# The control: the overlay with its INI files back to LF, as stage-overlay.sh staged them before the fix.
cp -R "$WORK/overlay-lf" "$WORK/overlay-control"
find "$WORK/overlay-control/Data/INI" -type f -exec perl -pi -e 's/\r\n/\n/' {} +

ini_crc() {	# ini_crc <name> <overlay>: the INI CRC the game logs at start with that overlay over the farm
	local name="$1" log="$EXEDIR/${TAG}$1DebugLogFile.txt"
	mkdir -p "$WORK/user-$name"
	( cd "$ROOT" && ZH_UNATTENDED=1 ZH_USER_DATA_DIR="$WORK/user-$name" "$GENERALS" -headless -noaudio -root "$ROOT" \
		-overlay "$2" -quickstart -noshellmap -multiInstance -noFPSLimit -maxframes 30 -logPrefix "${TAG}$name" \
		-randommap 0 2 128 -autoskirmish 2 -aidiff brutal -seed 0 -observer > "$WORK/$name.out" 2>&1 )
	grep -a 'INI CRC is 0x' "$log" 2>/dev/null | tail -1 | sed 's/.*INI CRC is \(0x[0-9A-Fa-f]*\).*/\1/'
}
FROM_LF="$(ini_crc lf "$WORK/overlay-lf")"
FROM_CRLF="$(ini_crc crlf "$WORK/overlay-crlf")"
CONTROL="$(ini_crc control "$WORK/overlay-control")"
echo "INI CRC: staged from LF masters ${FROM_LF:-none}, from CRLF masters ${FROM_CRLF:-none}, LF control ${CONTROL:-none}"
check '[ -n "$FROM_LF" ] && [ "$FROM_LF" = "$FROM_CRLF" ]' "the game's INI CRC is the same from either checkout's overlay"
check '[ -n "$CONTROL" ] && [ "$CONTROL" != "$FROM_LF" ]' "armed: LF INI files give another INI CRC (the defect, which the check can see)"

if ! install_verify "$INSTALL" "$WORK/install.before" "$WORK/install.after"; then
	echo "FAIL: the install changed during the run"
	status=1
fi
exit $status
