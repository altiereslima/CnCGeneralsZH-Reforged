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
# A harness whose work folder cannot be made must stop, not go on with the folder's name empty: "$WORK/root"
# is then /root, and every path it builds sits at the file system's root.  A script without set -e goes on
# past a failed mktemp, and most of ours have no set -e.
#
#   1. Every tracked shell script's `mktemp -d` either runs under set -e, or falls back to an empty name
#      (`|| NAME=""`) that the script then checks (`[ -z "$NAME" ]` or `[ -n "$NAME" ]`).  Armed: a made-up
#      script with a bare mktemp must be flagged.
#   2. The harnesses that root the game at a farm, run with TMPDIR=/nonexistent and just enough (a stand-in
#      executable, a data folder with an empty zerohour/, a stand-in compiler) to get as far as their
#      mktemp: each must exit 2 and say it cannot make a work folder, and do nothing else.
#
# Exit status: 0 on a pass, 1 otherwise.
set -u
CODE="$(cd "$(dirname "$0")/.." && pwd)"
REPO="$(cd "$CODE/../.." && pwd)"
failed=0
pass() { echo "ok: $*"; }
fail() { echo "FAIL: $*"; failed=1; }

SCRATCH="$(mktemp -d "${TMPDIR:-/tmp}/test_workfolder_guard.XXXXXX")" || SCRATCH=""
if [ -z "$SCRATCH" ] || [ ! -d "$SCRATCH" ]; then
	echo "test_workfolder_guard: cannot make a work folder under ${TMPDIR:-/tmp}" >&2
	exit 1
fi
trap 'rm -rf -- "${SCRATCH:?}"' EXIT

# ---- 1. the lint ---------------------------------------------------------------------------------------
# unguarded <script>: prints each line number whose mktemp -d is neither under set -e nor checked
unguarded() {
	local script="$1"
	grep -q -E '^set -[a-z]*e' "$script" && return 0
	grep -n 'mktemp -d' "$script" | grep -v '^[0-9]*:\s*#' | while IFS=: read -r number line; do
		local name
		name="$(printf '%s\n' "$line" | sed -n 's/.*|| \([A-Za-z_][A-Za-z_0-9]*\)="".*/\1/p')"
		if [ -z "$name" ] || ! grep -q -E "\[ -[zn] \"\\\$$name\" \]" "$script"; then
			echo "$number"
		fi
	done
}

printf '#!/usr/bin/env bash\nset -u\nWORK="$(mktemp -d "${TMPDIR:-/tmp}/x.XXXXXX")"\nmkdir -p "$WORK/root"\n' > "$SCRATCH/bare.sh"
if [ -n "$(unguarded "$SCRATCH/bare.sh")" ]; then pass "the lint flags a bare mktemp -d (armed)"; else fail "the lint missed a bare mktemp -d"; fi

if command -v git >/dev/null 2>&1 && git -C "$REPO" rev-parse --git-dir >/dev/null 2>&1; then
	checked=0
	while IFS= read -r script; do
		[ -f "$REPO/$script" ] || continue
		[ "$script" = "GeneralsMD/Code/Tests/test_workfolder_guard.sh" ] && continue		# its text names the pattern on purpose
		grep -q 'mktemp -d' "$REPO/$script" || continue
		checked=$((checked + 1))
		bad="$(unguarded "$REPO/$script" | paste -sd, -)"
		[ -n "$bad" ] && fail "$script: mktemp -d unguarded at line $bad"
	done < <(git -C "$REPO" ls-files '*.sh')
	pass "linted every tracked shell script with a mktemp -d ($checked of them)"
else
	echo "SKIP (part 1): no git checkout at $REPO to list the scripts"
fi

# ---- 2. the harnesses, with no work folder to be had ---------------------------------------------------
FAKE="$SCRATCH/fake"
mkdir -p "$FAKE/data/zerohour" "$FAKE/bin"
printf '#!/bin/sh\nexit 0\n' > "$FAKE/generals"
printf '#!/bin/sh\nexit 0\n' > "$FAKE/bin/x86_64-w64-mingw32-g++"
chmod +x "$FAKE/generals" "$FAKE/bin/x86_64-w64-mingw32-g++"
ASAN=""
if command -v cc >/dev/null 2>&1 && printf 'void __asan_init(void){}\nint main(void){return 0;}\n' | cc -x c - -o "$FAKE/asan-generals" 2>/dev/null; then
	ASAN="$FAKE/asan-generals"		# a stand-in asan-check takes for an ASan build (it looks for __asan_init)
fi
NOWHERE=/nonexistent/zh-workfolder-control

# expect_stop <label> <command...>: the command, with TMPDIR nowhere, must exit 2 saying so, within a minute
expect_stop() {
	local label="$1"; shift
	local out="$SCRATCH/$label.out"
	TMPDIR="$NOWHERE" PATH="$FAKE/bin:$PATH" ZH_DATA_DIR="$FAKE/data" \
		perl -e 'setpgrp(0, 0); $SIG{ALRM} = sub { kill "KILL", -$$; exit 124 }; alarm 60; system @ARGV; exit($? >> 8)' "$@" \
		> "$out" 2>&1
	local status=$?
	if [ "$status" -eq 2 ] && grep -q "cannot make a work folder under $NOWHERE" "$out"; then
		pass "$label stops without a work folder"
	else
		fail "$label went on without a work folder (exit $status): $(head -c 300 "$out" | tr '\n' ' ')"
	fi
}

expect_stop replay-check "$CODE/Tools/replay-check.sh" --generals "$FAKE/generals" --data "$FAKE/data"
expect_stop lod-first-run-check "$CODE/Tools/lod-first-run-check.sh" --generals "$FAKE/generals" --data "$FAKE/data"
expect_stop packaging-resolution-check "$CODE/Tools/packaging-resolution-check.sh" --generals "$FAKE/generals" --data "$FAKE/data"
expect_stop fingerprint-check "$CODE/Tools/fingerprint-check.sh" "$FAKE/generals" "$FAKE/nothing.h"
expect_stop ffprogram-values-check "$CODE/Tools/ffprogram-values-check.sh"
if [ -n "$ASAN" ]; then
	expect_stop asan-check "$CODE/Tools/asan-check.sh" --generals "$ASAN" --data "$FAKE/data"
else
	echo "SKIP: asan-check (no C compiler for its stand-in executable)"
fi

[ "$failed" -eq 0 ] && echo "PASS" || echo "FAILED"
exit $failed
