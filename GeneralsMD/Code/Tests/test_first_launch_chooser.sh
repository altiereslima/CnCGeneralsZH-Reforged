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
# The first launch's folder question, end to end and headless (no test run may need a human).
# PosixMain's test stand-in for SDL's dialog, ZH_TEST_CHOOSER_ANSWERS, feeds the real generals a list of
# answers; each refusal's reason goes to stderr where the dialog's message box would have shown it.
#
#   (a) an empty folder and a folder that is not Zero Hour: each refused with its own reason and asked again;
#       then a farm of the install: accepted, remembered in Registry.ini, and the game runs its frames from it;
#   (b) an empty folder, then cancel: refused, asked again, then the "will close" message, a failed start, and
#       nothing remembered;
#   (c) a second start with (a)'s Registry.ini and no answers at all: nothing asked, the game runs;
#   (d) Zero Hour and Generals in two separate folders (a CD or First Decade install, or what a Flatpak sees):
#       Zero Hour alone is followed by the Generals question; a folder that is not Generals is refused and it
#       is asked again; the Generals folder is accepted, both are remembered (InstallPath and Generals'
#       InstallPath), and the game runs its frames from them.
#
# The root is always a farm of ZH_DATA_DIR's zerohour (rule 9), HOME an empty folder (no known place can answer),
# the user data folder a scratch one.  Skipped (77) without ZH_DATA_DIR.  What it cannot see: the SDL panel
# itself, which was checked by hand on a MacBook Air on 2026-09-27; this is everything around it.
#
# Each start plays a seeded two-player AI skirmish to 30 frames (so -maxframes ends it), with the build's staged
# overlay as a package carries it, under a 300 s alarm: nothing can wait on a person.
#
# Usage: test_first_launch_chooser.sh <generals> <staged overlay>

set -u
GENERALS="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
[ -n "${ZH_DATA_DIR:-}" ] && [ -d "$ZH_DATA_DIR/zerohour" ] || { echo "skip: ZH_DATA_DIR names no zerohour/"; exit 77; }
[ -d "${2:-}" ] || { echo "skip: no staged overlay at ${2:-} (build zh_overlay)"; exit 77; }
OVERLAY="$(cd "$2" && pwd)"
DATA="$(cd "$ZH_DATA_DIR" && pwd -P)"
EXEDIR="$(dirname "$GENERALS")"
failures=0
check() {	# check <condition> <what>
	if eval "$1"; then echo "ok: $2"; else echo "FAIL: $2"; failures=$((failures + 1)); fi
}
T="$(mktemp -d "${TMPDIR:-/tmp}/test_first_launch_chooser.XXXXXX")" || T=""
if [ -z "$T" ] || [ ! -d "$T" ]; then
	echo "test_first_launch_chooser: cannot make a work folder under ${TMPDIR:-/tmp}" >&2
	exit 2
fi
T="$(cd "$T" && pwd -P)"
TAG="chooser$$"
trap 'rm -rf -- "$T"; rm -f -- "$EXEDIR/$TAG"*' EXIT

# the farm, and the wrong answers
FARM="$T/farm"
( cd "$DATA/zerohour" && find . -type d ! -name '._*' ) | while IFS= read -r d; do mkdir -p "$FARM/$d"; done
( cd "$DATA/zerohour" && find . -type f ! -name '._*' ) | while IFS= read -r f; do ln -s "$DATA/zerohour/${f#./}" "$FARM/$f"; done
mkdir -p "$T/empty" "$T/notzh" "$T/home"
echo readme > "$T/notzh/readme.txt"
# (d)'s two folders: Zero Hour without ZH_Generals, and Generals on its own (the install's ZH_Generals)
ZHONLY="$T/zh-only" GENONLY="$T/generals-only"
( cd "$FARM" && find . -path ./ZH_Generals -prune -o -print ) | while IFS= read -r f; do
	if [ -d "$FARM/$f" ] && [ ! -L "$FARM/$f" ]; then mkdir -p "$ZHONLY/$f"; else ln -s "$(readlink "$FARM/$f")" "$ZHONLY/$f"; fi
done
cp -R -P "$FARM/ZH_Generals" "$GENONLY"

start() {	# start <label> <user data> <answers file>: a headless start through the stand-in; sets STATUS
	( cd "$T" && HOME="$T/home" ZH_USER_DATA_DIR="$2/" ZH_TEST_CHOOSER_ANSWERS="$3" perl -e 'alarm shift; exec @ARGV' 300 \
		"$GENERALS" -headless -overlay "$OVERLAY" -quickstart -noshellmap -multiInstance -noFPSLimit -maxframes 30 \
		-randommap 0 2 -autoskirmish 2 -aidiff brutal -seed 0 -observer -logPrefix "$TAG$1" > "$T/$1.out" 2> "$T/$1.err" )
	STATUS=$?
}
installPath() { sed -n 's/^InstallPath *= *//p' "$1/Registry.ini" 2>/dev/null; }
generalsPath() { sed -n 's/^Generals\\InstallPath *= *//p' "$1/Registry.ini" 2>/dev/null; }
ran() { grep -a -q 'HEADLESS CRC: 0x' "$EXEDIR/$TAG$1DebugLogFile.txt" 2>/dev/null; }

# (a)
printf '%s\n' "$T/empty" "$T/notzh" "$FARM" > "$T/a.answers"
mkdir -p "$T/user-a"
start a "$T/user-a" "$T/a.answers"
refusals="$(grep -c 'chooser (test answers): refused, asking again' "$T/a.err")"
check '[ "$refusals" -eq 2 ]' "(a) two answers refused, each asked again ($refusals)"
check 'grep -q "refused, asking again: \"$T/empty\" is not a Command & Conquer Generals Zero Hour folder" "$T/a.err"' \
	"(a) the empty folder: not a Zero Hour folder"
check 'grep -q "refused, asking again: \"$T/notzh\" is not a Command & Conquer Generals Zero Hour folder" "$T/a.err"' \
	"(a) a folder that is not Zero Hour: the same reason, naming it"
check '! grep -q "asked for the Generals folder" "$T/a.err" && [ -z "$(generalsPath "$T/user-a")" ]' \
	"(a) Zero Hour with its base game inside: Generals is not asked for, nor written"
check '[ "$(installPath "$T/user-a")" = "$FARM" ]' "(a) the farm accepted and remembered: Registry.ini InstallPath = $(installPath "$T/user-a")"
check '[ $STATUS -eq 0 ] && ran a' "(a) the game ran its frames from it (exit $STATUS)"

# (b)
printf '%s\n' "$T/empty" cancel > "$T/b.answers"
mkdir -p "$T/user-b"
start b "$T/user-b" "$T/b.answers"
check '[ "$(grep -c "refused, asking again" "$T/b.err")" -eq 1 ] && grep -q "chooser (test answers): cancel" "$T/b.err"' \
	"(b) the empty folder refused, asked again, then cancelled"
check 'grep -q "No Zero Hour folder was chosen, so the game will close" "$T/b.err" && [ $STATUS -ne 0 ]' \
	"(b) the \"will close\" message, and the start fails (exit $STATUS)"
check '[ -z "$(installPath "$T/user-b")" ] && ! ran b' "(b) nothing remembered, no game"

# (c)
: > "$T/c.answers"
start c "$T/user-a" "$T/c.answers"
check '! grep -q "chooser (test answers)" "$T/c.err"' "(c) with Registry.ini, nothing is asked"
check '[ $STATUS -eq 0 ] && ran c' "(c) the game runs from the remembered folder (exit $STATUS)"

# (d)
printf '%s\n' "$ZHONLY" "$T/notzh" "$GENONLY" > "$T/d.answers"
mkdir -p "$T/user-d"
start d "$T/user-d" "$T/d.answers"
check '[ "$(grep -c "asked for the Generals folder" "$T/d.err")" -eq 2 ]' \
	"(d) Zero Hour alone: Generals asked for, the wrong folder refused, asked again"
check 'grep -q "asked for the Generals folder: \"$ZHONLY\" holds Zero Hour" "$T/d.err" && grep -q "asked for the Generals folder: \"$T/notzh\" is not the original Command & Conquer Generals folder" "$T/d.err"' \
	"(d) each question with its reason"
check '[ "$(installPath "$T/user-d")" = "$ZHONLY" ] && [ "$(generalsPath "$T/user-d")" = "$GENONLY" ]' \
	"(d) both remembered: InstallPath = $(installPath "$T/user-d"), Generals InstallPath = $(generalsPath "$T/user-d")"
check '[ $STATUS -eq 0 ] && ran d' "(d) the game ran its frames with Generals from its own folder (exit $STATUS)"

[ $failures -eq 0 ] || { echo "--- (a) stderr"; head -20 "$T/a.err"; echo "--- (b) stderr"; head -10 "$T/b.err"; echo "--- (d) stderr"; head -10 "$T/d.err"; }
echo "$failures failure(s)"
[ $failures -eq 0 ]
