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
# Tools/stage-overlay.sh keeps the HUD overlay's default a build's own (off in Release, on in Debug: a
# project rule, 2026-09-29): staging refuses a GameData.ini line that forces ShowHudOverlay on, loose or
# inside an archive the overlay links, and stages everything else. Made-up Code/Data and Run folders; no
# game data.
#   1. ShowHudOverlay = No, and a commented-out Yes        -> staged
#   2. a loose Data/INI/GameData.ini with "ShowHudOverlay = Yes" -> refused, exit 1, nothing left behind
#   3. the same line in a Reforged*.big the overlay links  -> refused
#   4. "ShowHudOverlay = 1" and "= true", in any case      -> refused
set -u
STAGE="$(cd "$(dirname "$0")/../Tools" && pwd)/stage-overlay.sh"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/test_stage_overlay_hud.XXXXXX")" || WORK=""
if [ -z "$WORK" ] || [ ! -d "$WORK" ]; then
	echo "test_stage_overlay_hud: cannot make a work folder under ${TMPDIR:-/tmp}" >&2
	exit 2
fi
trap 'rm -rf -- "${WORK:?}"' EXIT
failed=0
check() { if eval "$1"; then echo "ok: $2"; else echo "FAIL: $2"; failed=1; fi; }

data() {	# data <folder> <GameData.ini text>: a made-up Code/Data
	mkdir -p "$1/INI"
	printf '%s\n' "$2" > "$1/INI/GameData.ini"
	printf 'Patch\n' > "$1/Patch.str"
}

data "$WORK/off" $'GameData\n  ShowHudOverlay = No\n  ;ShowHudOverlay = Yes\nEnd'
"$STAGE" "$WORK/off" "" "$WORK/out1" 2> "$WORK/err1"; status=$?
check '[ $status -eq 0 ] && [ -f "$WORK/out1/Data/INI/GameData.ini" ] && [ -f "$WORK/out1.staged" ]' "No, and a commented-out Yes: staged (exit $status)"

data "$WORK/on" $'GameData\n  ShowHudOverlay = Yes\nEnd'
"$STAGE" "$WORK/on" "" "$WORK/out2" 2> "$WORK/err2"; status=$?
check '[ $status -eq 1 ] && grep -q "forces the HUD overlay on" "$WORK/err2" && [ ! -e "$WORK/out2" ] && [ ! -e "$WORK/out2.staged" ]' \
	"a loose ShowHudOverlay = Yes: refused, nothing left (exit $status)"

mkdir -p "$WORK/run"
printf 'BIGF....\0\0Data\\INI\\GameData.ini\0GameData\r\n  ShowHudOverlay = Yes\r\nEnd\r\n' > "$WORK/run/ReforgedTest.big"
"$STAGE" "$WORK/off" "$WORK/run" "$WORK/out3" 2> "$WORK/err3"; status=$?
check '[ $status -eq 1 ] && grep -q "ReforgedTest.big" "$WORK/err3"' "the line inside a linked Reforged*.big: refused (exit $status)"

for value in 1 TRUE; do
	data "$WORK/v$value" "  showhudoverlay=$value"
	"$STAGE" "$WORK/v$value" "" "$WORK/out_$value" 2> /dev/null; status=$?
	check '[ $status -eq 1 ]' "ShowHudOverlay = $value, in another case: refused (exit $status)"
done

exit $failed
