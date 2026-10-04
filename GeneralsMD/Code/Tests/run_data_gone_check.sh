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
# The game's data going away under it (the lid-close crash; PORTING.md, "Defects found while porting").  A player whose install
# is on a drive that is disconnected, ejected or asleep: the archives are open, and the next read of one
# fails.  The game must say so and stop - exit status 3 (GAME_DATA_GONE_EXIT_STATUS, Common/Debug.h), the
# "GAME DATA GONE" line in its log - and never crash.
#   1. The two window archives (WindowZH.big, ZH_Generals/Window.big) are copied onto a disk image of the
#      test's own, and the farm's links for them point there.  A headless skirmish is stopped (SIGSTOP)
#      mid-match, the image is detached, the game continued: its next archive read (a window layout, when
#      the match ends, after its HEADLESS RESULT line) fails with EIO.  Expected: exit 3, the line, no crash
#      report.
#   2. The control: the same, with the copies in a plain folder that is renamed away instead.  A rename
#      leaves an open file readable, so the game must play on and end normally: the detection keys on the
#      device being gone, not on anything that changed under the game.
#
# RULE 9: the game never runs with the real install as its root.  The root is a farm of links into it; the
# two links this test repoints are removed first (rm -f), never written through.  The install is listed
# before the farm is built and checked at the end (Tools/install-guard.sh).  Only the image this test
# attached is ever detached, by the device its own attach reported, and only while it is still mounted
# where the test put it.
#
# Platforms: macOS (hdiutil).  Elsewhere it exits 77, which ctest reports as Skipped.  The two steps that
# differ are attach_image and detach_image; Windows can take a VHD there (diskpart create/attach/detach
# vdisk), which would measure the Windows error codes LocalFile.cpp names but has not seen.
# Needs ZH_DATA_DIR (a folder holding zerohour/); without it, exit 77.
# Usage: run_data_gone_check.sh <generals>
set -u
if [ -z "${ZH_DATA_DIR:-}" ] || [ ! -d "$ZH_DATA_DIR/zerohour" ]; then
	echo "skip: no game data (ZH_DATA_DIR, a folder holding zerohour/)"
	exit 77
fi
if [ "$(uname -s)" != Darwin ] || ! command -v hdiutil > /dev/null; then
	echo "skip: no way to make and detach a disk image here (macOS's hdiutil; Windows' diskpart is not written yet)"
	exit 77
fi
TOOLS="$(cd "$(dirname "$0")/../Tools" && pwd)"
CODE="$(cd "$TOOLS/.." && pwd)"
INSTALL="$(cd "$ZH_DATA_DIR/zerohour" && pwd)"
EXEDIR="$(cd "$(dirname "$1")" && pwd)"
GENERALS="$EXEDIR/$(basename "$1")"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/data-gone-check.XXXXXX")" || WORK=""
if [ -z "$WORK" ] || [ ! -d "$WORK" ]; then
	echo "run_data_gone_check: cannot make a work folder under ${TMPDIR:-/tmp}" >&2
	exit 2
fi
WORK="$(cd "$WORK" && pwd -P)"		# mount lists real paths, and the detach check compares with it
TAG="dg$$_"
STOP_AT=2000		# logic frame to stop at, well before the match ends
MAXFRAMES=20000
LIMIT="${DATA_GONE_CHECK_LIMIT:-300}"		# seconds for one run
DEV=""; MNT=""

# ---- the two platform steps ---------------------------------------------------------------------------
attach_image() {	# attach_image <mount point>: a fresh 40 MB image mounted there; sets DEV
	hdiutil create -quiet -size 40m -fs HFS+ -volname ZHDATAGONE "$WORK/data.dmg" || return 1
	mkdir -p "$1"
	DEV="$(hdiutil attach -nobrowse -mountpoint "$1" "$WORK/data.dmg" | awk -v m="$1" '$NF == m {print $1}')"
	[ -n "$DEV" ]
}
detach_image() {	# only our device, and only while it is mounted where we put it
	[ -n "$DEV" ] && mount | grep -q "^$DEV on $MNT " && hdiutil detach -force "$DEV" > /dev/null
}

. "$TOOLS/install-guard.sh"
cleanup() {
	detach_image 2> /dev/null
	if ! install_verify "$INSTALL" "$WORK/install-before.list" "$WORK/install-after.list"; then
		echo "kept for inspection: $WORK" >&2
		exit 99
	fi
	rm -rf -- "${WORK:?}"
	rm -f -- "$EXEDIR/${TAG}"*DebugLogFile*.txt
}
trap cleanup EXIT
install_snapshot "$INSTALL" "$WORK/install-before.list"

ROOT="$WORK/root"; USERDATA="$WORK/user"
mkdir -p "$ROOT" "$USERDATA"
( cd "$INSTALL" && find . -type d ! -name '._*' ) | while IFS= read -r d; do mkdir -p "$ROOT/$d"; done
( cd "$INSTALL" && find . -type f ! -name '._*' ) | while IFS= read -r f; do ln -s "$INSTALL/${f#./}" "$ROOT/$f"; done
"$TOOLS/stage-overlay.sh" "$CODE/Data" "$CODE/../Run" "$WORK/overlay" > /dev/null

failed=0
check() { if eval "$1"; then echo "ok: $2"; else echo "FAIL: $2"; failed=1; fi; }

# the window archives, copied to <folder> and the farm's links for them pointed there
move_window_archives_to() {
	mkdir -p "$1/ZH_Generals"
	cp "$INSTALL/WindowZH.big" "$1/WindowZH.big" && cp "$INSTALL/ZH_Generals/Window.big" "$1/ZH_Generals/Window.big" || return 1
	rm -f -- "$ROOT/WindowZH.big" "$ROOT/ZH_Generals/Window.big"
	ln -s "$1/WindowZH.big" "$ROOT/WindowZH.big" && ln -s "$1/ZH_Generals/Window.big" "$ROOT/ZH_Generals/Window.big"
}

# run_stopped <n> <what to do while stopped>: sets STATUS, GONE (the GAME DATA GONE line), RESULT, CRASHED
run_stopped() {
	local prefix="$TAG$1" action="$2"
	local log="$EXEDIR/${prefix}DebugLogFile.txt"
	rm -f -- "$log"
	( cd "$ROOT" && ZH_USER_DATA_DIR="$USERDATA" exec perl -e 'alarm shift; exec @ARGV' "$LIMIT" \
		"$GENERALS" -headless -noFPSLimit -root "$ROOT" -overlay "$WORK/overlay" -quickstart -noshellmap -multiInstance \
		-logPrefix "$prefix" -map 'Maps\Tournament Desert\Tournament Desert.map' -autoskirmish 2 -seed 1234 -observer \
		-maxframes "$MAXFRAMES" > /dev/null 2>&1 ) &
	local pid=$! frame=""
	while kill -0 $pid 2> /dev/null; do		# the log's PARTICLES lines, every 100 frames, flushed once a second
		frame="$(grep -a -o 'PARTICLES frame [0-9]*' "$log" 2> /dev/null | tail -1 | awk '{print $3}')"
		[ -n "$frame" ] && [ "$frame" -ge "$STOP_AT" ] && break
		sleep 0.2
	done
	if kill -STOP $pid 2> /dev/null; then
		echo "stopped at logic frame $frame"
		eval "$action"
		kill -CONT $pid
	else
		echo "the game ended before it could be stopped"
	fi
	wait $pid
	STATUS=$?
	GONE="$(grep -a '^GAME DATA GONE' "$log" 2> /dev/null | head -1)"
	RESULT="$(grep -a 'HEADLESS RESULT: ' "$log" 2> /dev/null | tail -1)"
	CRASHED="$(grep -a -c 'Release Crash' "$log" 2> /dev/null)"
	printf '%s\n%s\n' "$GONE" "$RESULT"
}

# 1. the drive goes away
MNT="$WORK/image"
attach_image "$MNT" || { echo "FAIL: could not make and attach the test's disk image"; exit 1; }
echo "attached $DEV at $MNT"
move_window_archives_to "$MNT" || { echo "FAIL: could not put the window archives on the image"; exit 1; }
run_stopped 1 'detach_image && echo "detached $DEV"'
check '[ $STATUS -eq 3 ]' "with the archives' drive gone, the game stops with exit status 3 (exit $STATUS)"
check 'printf "%s" "$GONE" | grep -q "WindowZH.big could not be read"' "and says so, naming the archive"
check '[ "${CRASHED:-0}" -eq 0 ]' "and it is not a crash"
DEV=""

# 2. the control: renamed away, still readable through the open file
MNT="$WORK/folder"
move_window_archives_to "$MNT" || { echo "FAIL: could not copy the window archives"; exit 1; }
run_stopped 2 'mv "$MNT" "$WORK/folder-renamed" && echo "renamed away"'
check '[ $STATUS -eq 0 ] && printf "%s" "$RESULT" | grep -q "frame limit reached on frame $MAXFRAMES"' \
	"the control (renamed, not gone) plays on to frame $MAXFRAMES (exit $STATUS)"
check '[ -z "$GONE" ]' "and does not report the data gone"

exit $failed
