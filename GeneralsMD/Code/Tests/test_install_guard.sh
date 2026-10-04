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
# Tools/install-guard.sh on a made-up install, no game data: each of its three verdicts is reached,
# and the one that cannot verify is never reported as a change.
#   1. nothing touched                              -> 0, "ok: the install is as it was"
#   2. one byte of a file changed, size and time kept -> 1, "THE INSTALL CHANGED" with that file named
#   3. the before-listing gone                      -> 2, "COULD NOT VERIFY", not "CHANGED"
#   4. the work folder gone, so the after-listing cannot be written (the 2026-09 false alarm)
#                                                   -> 2, "COULD NOT VERIFY", not "CHANGED"
#   5. an empty install                             -> install_snapshot fails
set -u
GUARD="$(cd "$(dirname "$0")/../Tools" && pwd)/install-guard.sh"
. "$GUARD"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/test_install_guard.XXXXXX")" || WORK=""
if [ -z "$WORK" ] || [ ! -d "$WORK" ]; then
	echo "test_install_guard: cannot make a work folder under ${TMPDIR:-/tmp}" >&2
	exit 2
fi
trap 'rm -rf -- "${WORK:?}"' EXIT
failed=0
check() { if eval "$1"; then echo "ok: $2"; else echo "FAIL: $2"; failed=1; fi; }

install="$WORK/install"
mkdir -p "$install/Data/INI" "$WORK/a"
printf 'abcdef' > "$install/Data/INI/INIZH.big"
printf 'textures' > "$install/Textures.big"
printf 'reference' > "$WORK/reference"
touch -r "$WORK/reference" "$install/Data/INI/INIZH.big"	# its time, to put back after an edit

check 'install_snapshot "$install" "$WORK/a/before"' "the made-up install is listed"
out="$(install_verify "$install" "$WORK/a/before" "$WORK/a/after")"; status=$?
check '[ $status -eq 0 ] && printf "%s" "$out" | grep -q "^ok: the install is as it was"' "untouched: ok (exit $status)"

printf 'abcdeX' > "$install/Data/INI/INIZH.big"
touch -r "$WORK/reference" "$install/Data/INI/INIZH.big"
check 'grep "^Data/INI/INIZH.big " "$WORK/a/before" | cut -d" " -f1-3 | cmp -s - <(install_snapshot "$install" /dev/stdout | grep "^Data/INI/INIZH.big " | cut -d" " -f1-3)' \
	"the edit kept the size and the time, so only the hash can see it"
out="$(install_verify "$install" "$WORK/a/before" "$WORK/a/after")"; status=$?
check '[ $status -eq 1 ] && printf "%s" "$out" | grep -q "THE INSTALL CHANGED" && printf "%s" "$out" | grep -q "Data/INI/INIZH.big"' \
	"one byte changed, the size kept: CHANGED, naming the file (exit $status)"
printf 'abcdef' > "$install/Data/INI/INIZH.big"
install_snapshot "$install" "$WORK/a/before"

out="$(install_verify "$install" "$WORK/a/missing" "$WORK/a/after")"; status=$?
check '[ $status -eq 2 ] && printf "%s" "$out" | grep -q "COULD NOT VERIFY" && ! printf "%s" "$out" | grep -q "CHANGED"' \
	"no before-listing: COULD NOT VERIFY, not CHANGED (exit $status)"

cp "$WORK/a/before" "$WORK/before.kept"
rm -rf -- "$WORK/a"
out="$(install_verify "$install" "$WORK/before.kept" "$WORK/a/after" 2>/dev/null)"; status=$?
check '[ $status -eq 2 ] && printf "%s" "$out" | grep -q "COULD NOT VERIFY" && ! printf "%s" "$out" | grep -q "CHANGED"' \
	"the work folder gone: COULD NOT VERIFY, not CHANGED (exit $status)"

mkdir -p "$WORK/empty"
check '! install_snapshot "$WORK/empty" "$WORK/empty.list" 2>/dev/null' "an empty install cannot be listed"

exit $failed
