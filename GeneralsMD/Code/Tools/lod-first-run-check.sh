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
# Decision 2 for the GPU, run for real: a first launch chooses the top detail preset the CPU and memory
# rules allow, where before every Mac's chose LOW, and so did a Windows machine whose card is newer than
# W3DShaderManager::getChipset's table.
#
# Before the rule, the POSIX device answered no vendor or device ID W3DShaderManager::getChipset knows,
# so it said DC_UNKNOWN; GameLOD presumed a TNT2, and every LODPreset in the shipped GameLODPresets.ini,
# LOW included, asks for a GF3 or a GF4, so none matched and the default LOW stood. Now
# testMinimumRequirements reports an unplaced device as the top of the chipset table. The CPU half is
# decision 2's first rule: an unknown CPU type runs the benchmark, whose POSIX stub beats every
# BenchProfile, and GameLOD adopts the last profile a preset's speed takes (K7 1500 in the shipped file),
# which reaches HIGH ("HIGH K7 1500 GF3 512") with 512 MB or more.
#
# Three short headless skirmishes, each rooted at a farm of the install with the staged overlay:
#   1. a first launch, from an empty user data folder: Options.ini must say IdealStaticGameLOD = High
#      and StaticGameLOD = High;
#   2. a later launch from the same folder: both still High (the later launch reads them, and does not
#      choose again);
#   3. the armed control: a first launch with -noshaders, which forces the chipset to a Voodoo2 (an
#      override, which the rule leaves alone). Below every preset, it must choose Low - so a High in 1
#      is this rule's doing, and this check can see a Low.
#
# WHAT THIS DOES NOT SEE: a machine with less than 512 MB (every Mac the port supports has more), and
# Windows. The rule is the same code there (chipsetForPresets has no platform guard), but this script
# drives the POSIX executable only. On Windows the CPU half reaches HIGH by its own road: a CPU the
# cpudetect tables cannot name (every Ryzen, most Core) is XX and takes the same benchmark stub, and one
# they misname as a P3 or P4 is matched by its measured MHz, above the lowest HIGH preset's (P3 1400, P4 1500).
#
# RULE 9: the root is a farm, never the install, and the install is listed before and after
# (Tools/install-guard.sh).
#
# Usage: lod-first-run-check.sh --generals <path> [--data <dir>] [--keep]
# Exit status: 0 on a pass; 1 otherwise; 77 without game data.

set -u

GENERALS=""
DATA="${ZH_DATA_DIR:-}"
KEEP=0
while [ $# -gt 0 ]; do
	case "$1" in
		--generals) GENERALS="$2"; shift 2;;
		--data) DATA="$2"; shift 2;;
		--keep) KEEP=1; shift;;
		*) echo "lod-first-run-check: unknown argument $1" >&2; exit 2;;
	esac
done
if [ -z "$GENERALS" ] || [ ! -x "$GENERALS" ]; then
	echo "lod-first-run-check: --generals must name the POSIX generals executable" >&2
	exit 2
fi
if [ -z "$DATA" ] || [ ! -d "$DATA/zerohour" ]; then
	echo "skip: no game data (--data or ZH_DATA_DIR, a folder holding zerohour/)"
	exit 77
fi

CODE="$(cd "$(dirname "$0")/.." && pwd)"
RUNDIR="$(cd "$CODE/../Run" 2>/dev/null && pwd || true)"
INSTALL="$(cd "$DATA/zerohour" && pwd)"
EXEDIR="$(cd "$(dirname "$GENERALS")" && pwd)"
GENERALS="$EXEDIR/$(basename "$GENERALS")"
# A work folder that could not be made is the end of the run: going on with WORK empty would put
# "$WORK/..." at the file system's root.
WORK="$(mktemp -d "${TMPDIR:-/tmp}/lod-first-run-check.XXXXXX")" || WORK=""
if [ -z "$WORK" ] || [ ! -d "$WORK" ]; then
	echo "lod-first-run-check: cannot make a work folder under ${TMPDIR:-/tmp}" >&2
	exit 2
fi
TAG="lod$$_"

cleanup() {
	if [ "$KEEP" -eq 1 ]; then
		echo "kept: $WORK, and the logs $EXEDIR/${TAG}*"
		return
	fi
	rm -rf -- "${WORK:?}"
	rm -f -- "$EXEDIR/${TAG}"*DebugLogFile*.txt
}
trap cleanup EXIT

. "$CODE/Tools/install-guard.sh"	# install_snapshot, install_verify
if ! install_snapshot "$INSTALL" "$WORK/install.before"; then
	echo "FAIL: COULD NOT VERIFY the install: its listing before the run could not be made; nothing was run"
	exit 1
fi

farm() {	# farm <root>: the install's folders made anew, every file a link
	mkdir -p "$1"
	( cd "$INSTALL" && find . -type d ! -name '._*' ) | while IFS= read -r d; do mkdir -p "$1/$d"; done
	( cd "$INSTALL" && find . -type f ! -name '._*' ) | while IFS= read -r f; do ln -s "$INSTALL/${f#./}" "$1/$f"; done
}

run() {	# run <name> <user data folder> <switches...>: a fresh farm each time (the engine unlinks INIZH.big)
	local name="$1" user="$2"; shift 2
	rm -rf -- "${WORK:?}/root"
	farm "$WORK/root"
	mkdir -p "$user"
	( cd "$WORK/root" && ZH_USER_DATA_DIR="$user" "$GENERALS" -headless -root "$WORK/root" -overlay "$WORK/overlay" \
		-quickstart -noshellmap -multiInstance -noFPSLimit -maxframes 30 -logPrefix "$TAG$name" \
		-randommap 0 2 128 -autoskirmish 2 -aidiff brutal -seed 0 -observer "$@" \
		> "$WORK/$name.out" 2> "$WORK/$name.err" )
	RUN_STATUS=$?
}

option() {	# option <user data folder> <key>: the value Options.ini holds, or "(none)"
	local value
	value="$(sed -n "s/^$2 = //p" "$1/Options.ini" 2>/dev/null | tr -d '\r' | tail -1)"
	echo "${value:-(none)}"
}

expect() {	# expect <what> <user data folder> <ideal> <static>
	local ideal static
	ideal="$(option "$2" IdealStaticGameLOD)"; static="$(option "$2" StaticGameLOD)"
	if [ "$ideal" = "$3" ] && [ "$static" = "$4" ]; then
		echo "ok: $1: IdealStaticGameLOD = $ideal, StaticGameLOD = $static"
	else
		echo "FAIL: $1: IdealStaticGameLOD = $ideal, StaticGameLOD = $static (want $3, $4; exit $RUN_STATUS)"
		status=1
	fi
}

"$CODE/Tools/stage-overlay.sh" "$CODE/Data" "$RUNDIR" "$WORK/overlay"
status=0

run first "$WORK/user"
expect "a first launch" "$WORK/user" High High
run later "$WORK/user"
expect "a later launch, from the same folder" "$WORK/user" High High
run control "$WORK/user_control" -noshaders
expect "the control, a first launch with -noshaders (a Voodoo2)" "$WORK/user_control" Low Low

install_verify "$INSTALL" "$WORK/install.before" "$WORK/install.after" || status=1
exit $status
