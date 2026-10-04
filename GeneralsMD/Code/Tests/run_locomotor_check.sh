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
# The load-time module checks, one short run for both:
#   port defect 33: no override may leave a unit without the locomotors EA gave it;
#   port defect 34: no ObjectReskin may end up without a behaviour module its source has.
#
# An object's "Locomotor = SET_..." lines are stored in its AI module's data, so a ReplaceModule of that
# module discards them unless the block re-states them. Upstream's fbe8dc6f and 179e1f65 did that to 12
# units (three Chinooks, three Humvees, three ECM tanks, three Nuke Cannons), and the first Chinook a
# Supply Center made crashed every platform. ThingTemplate::parseReplaceModule records what a replacement
# discards, and ThingFactory's checkLocomotors logs, once everything has loaded:
#   LocomotorCheck: <name> lost its <n> locomotor set(s) to a ReplaceModule of its AI module, ...
#   LocomotorCheck: <r> things had an AI module with locomotors replaced, <m> of them left without a SET_NORMAL
# And (port defect 34) a reskin copies its source's modules; a module added to one in a normal INI load used to erase
# every copied module sharing an interface with it (upstream's fbe8dc6f left the Demolition General's
# Technical reskins with no AI, and the first AI that recruited one crashed). ThingFactory's checkReskins logs
#   ReskinCheck: <name> lacks behaviour modules its source <source> has: ...
#   ReskinCheck: <r> reskins, <m> of them lacking a behaviour module their source has
# This loads the shipped data and the overlay through the game itself (one 30-frame run of
# Tools/replay-check.sh, rule 9 as ever) and requires both summary lines, with m = 0 and no "lost" or
# "lacks" line.
# replay-check's own verdict is not this test's: 30 frames is an idle match, which it rightly calls IDLE.
#
# WHAT THIS CANNOT SEE: a unit that loses a locomotor some other way (a RemoveModule of its AI module, or
# an override that re-states the wrong locomotor); a locomotor set other than SET_NORMAL that a
# replacement drops while SET_NORMAL is re-stated (reported in the count of replacements, not failed);
# and a map's own map.ini overrides, which load with the map, not here.
#
# Needs ZH_DATA_DIR (a folder holding zerohour/); without it, exit 77, which ctest reports as Skipped.
# Usage: run_locomotor_check.sh <generals>
set -u
GENERALS="$1"
HARNESS="$(cd "$(dirname "$0")/../Tools" && pwd)/replay-check.sh"
if [ -z "${ZH_DATA_DIR:-}" ]; then
	echo "skip: no game data (ZH_DATA_DIR)"
	exit 77
fi

out="$(bash "$HARNESS" --generals "$GENERALS" --seeds "0" --maxframes 30 --log-lines 'LocomotorCheck:|ReskinCheck:' 2>&1)"
status=$?
printf '%s\n' "$out"
if [ "$status" -eq 99 ]; then
	echo "FAIL: the install check failed (replay-check exit 99)"
	exit 1
fi

summary="$(printf '%s\n' "$out" | grep -m1 'log: LocomotorCheck: [0-9]* things had an AI module')"
if [ -z "$summary" ]; then
	echo "FAIL: no LocomotorCheck summary in the run's log: the check did not run, or the game did not load its data"
	exit 1
fi
replaced="$(printf '%s' "$summary" | sed -n 's/.*LocomotorCheck: \([0-9]*\) things had.*/\1/p')"
lost="$(printf '%s' "$summary" | sed -n 's/.*, \([0-9]*\) of them left.*/\1/p')"
failed=0
if [ "${lost:-x}" != "0" ]; then
	echo "FAIL: $lost unit(s) lost their locomotors to a ReplaceModule of their AI module (port defect 33):"
	printf '%s\n' "$out" | grep 'log: LocomotorCheck: .* lost its' | sed 's/.*LocomotorCheck: /  /'
	failed=1
fi
if printf '%s\n' "$out" | grep -q 'log: LocomotorCheck: .* lost its'; then
	[ "$failed" -eq 1 ] || echo "FAIL: a 'lost' line with a summary that says none"
	failed=1
fi
[ "$failed" -eq 0 ] && echo "ok: $replaced things had their AI module replaced with locomotors; every one still has a SET_NORMAL"

reskin="$(printf '%s\n' "$out" | grep -m1 'log: ReskinCheck: [0-9]* reskins')"
if [ -z "$reskin" ]; then
	echo "FAIL: no ReskinCheck summary in the run's log"
	exit 1
fi
reskins="$(printf '%s' "$reskin" | sed -n 's/.*ReskinCheck: \([0-9]*\) reskins.*/\1/p')"
lacking="$(printf '%s' "$reskin" | sed -n 's/.*, \([0-9]*\) of them lacking.*/\1/p')"
if [ "${lacking:-x}" != "0" ] || printf '%s\n' "$out" | grep -q 'log: ReskinCheck: .* lacks'; then
	echo "FAIL: $lacking reskin(s) lack behaviour modules their source has (port defect 34):"
	printf '%s\n' "$out" | grep 'log: ReskinCheck: .* lacks' | sed 's/.*ReskinCheck: /  /'
	failed=1
else
	echo "ok: $reskins reskins, each with every behaviour module its source has"
fi
exit $failed
