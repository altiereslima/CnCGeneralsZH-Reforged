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
# The POSIX generals, started without game data (C2): the whole entry sequence up to the first thing
# that needs data.  Rooted at an empty folder, PosixMain sets the root, starts the log, the memory
# manager and the version, takes the one-copy lock in the user data directory, and runs GameMain;
# SdlGameEngine starts GameEngine::init, which mounts no archives and stops, as generals.exe does on
# Windows, with the "no base game" message on stderr and exit code 2: an unattended run (-headless here)
# stops at that check rather than put up a box or carry on without the art.
#
# What it cannot see: anything past the archive check, which needs the game's data; a run with the
# data is E1's harness's.  Usage: run_generals_smoke.sh <generals>
set -u
GENERALS="$1"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/generals_smoke.XXXXXX")" || WORK=""
if [ -z "$WORK" ] || [ ! -d "$WORK" ]; then
	echo "run_generals_smoke: cannot make a work folder under ${TMPDIR:-/tmp}" >&2
	exit 2
fi
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/root" "$WORK/user"
failed=0
check() { if ! eval "$1"; then echo "FAIL: $2"; failed=1; else echo "ok: $2"; fi; }

cd "$WORK"
ZH_USER_DATA_DIR="$WORK/user" "$GENERALS" -headless -root "$WORK/root" > "$WORK/out.txt" 2> "$WORK/err.txt"
status=$?
check '[ $status -eq 2 ]' "rooted at an empty folder it exits 2 (was $status)"
check 'grep -q "none of the base game'"'"'s .big files could be found" "$WORK/err.txt"' "it stops at the base game check and says why"
check '[ -e "$WORK/user/Generals-685EAFF2-3216-4265-B047-251C5F4B82F3.lock" ]' "the one-copy lock is in ZH_USER_DATA_DIR"

# A root that cannot be entered is refused before anything else starts.
ZH_USER_DATA_DIR="$WORK/user" "$GENERALS" -headless -root "$WORK/no such folder" > /dev/null 2> "$WORK/err2.txt"
status=$?
check '[ $status -eq 1 ] && grep -q "cannot use .* as the install root" "$WORK/err2.txt"' "a root that does not exist is refused"

exit $failed
