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
# E1's POSIX harness (Tools/replay-check.sh) checked on itself, with the game's data.
#   1. One seed, 1200 frames: the recording, its playback and a second run from the same seed stop on
#      the same HEADLESS CRC at the same frame.
#   2. The armed control: the same with the kept replay's game seed edited before the playback.  The
#      harness must report that seed FAILED, on the playback and not on the second run, and exit 1 -
#      so a pass in 1 is a comparison that can fail.
#   3. The idle guard's control: seed 1 on a 128-cell map, where the generator puts the two starts
#      124 units apart and neither AI can build.  The harness must call it IDLE and exit 1.
#   4. Port defect 20: seed 1 at full length (12000 frames), a real fight.  The playback takes a checkpoint
#      (a full save) every 900 frames and the recording takes none, so the two agree only if a save
#      leaves the world as it found it.  Before the fix the playback parted at frame 8260.
#   7. Port defect 33: seed 0 with the generals pinned (-side), the Infantry General against the Laser
#      General, 12000 frames.  The seeds above never draw the generals whose units upstream's data had
#      left without locomotors, so E1 never made one.  Here the Laser General's Supply Center must stand
#      - it makes the Chinook that crashed every platform - and its Humvees fight; the Infantry General
#      fields the ECM tank and the Nuke Cannon when its AI chooses to, which no log shows (their
#      locomotors are locomotor_check's to prove).  Before port defect 33's fix this run crashed.
#   8. Port defect 34 and the W2-era baseline: seed 0 at full length (12000 frames).  Seed 0 draws the
#      Demolition General, whose AI recruits the reskinned Technicals that upstream's data had left with no
#      AI module; before port defect 34's fix this run crashed near frame 6600.  The run must agree with itself and
#      show Demolition structures built, so the seed still draws that general.  (One run serves both: a
#      separate -side run would add three more 12000-frame games, and the recruiting cannot be forced.)
#
# WHAT THIS PROVES: same-machine determinism only, on this machine and this build.  Nothing about
# agreement with a Windows build, which needs a replay recorded on Windows; none exists yet.
#
# Needs ZH_DATA_DIR (a folder holding zerohour/); without it, exit 77, which ctest reports as Skipped.
# Usage: run_replay_check.sh <generals>
set -u
GENERALS="$1"
HARNESS="$(cd "$(dirname "$0")/../Tools" && pwd)/replay-check.sh"
if [ -z "${ZH_DATA_DIR:-}" ]; then
	echo "skip: no game data (ZH_DATA_DIR)"
	exit 77
fi
failed=0
check() { if eval "$1"; then echo "ok: $2"; else echo "FAIL: $2"; failed=1; fi; }

out="$(bash "$HARNESS" --generals "$GENERALS" --seeds "0" --maxframes 1200 2>&1)"
status=$?
printf '%s\n' "$out"
check '[ $status -eq 0 ]' "seed 0 agrees with itself (exit $status)"
check 'printf "%s" "$out" | grep -q "the playback and the second run agree at frame 1200"' "at frame 1200, the playback and the second run both"

out="$(bash "$HARNESS" --generals "$GENERALS" --seeds "0" --maxframes 1200 --control 2>&1)"
status=$?
printf '%s\n' "$out"
check '[ $status -eq 1 ]' "the control (the replay's seed edited) fails the harness (exit $status)"
check 'printf "%s" "$out" | grep -q "FAILED, live .* playback 0x"' "and it is the playback that diverged"
check '! printf "%s" "$out" | grep -q "second run 0x"' "while the unedited second run still agreed"
check '! printf "%s" "$out" | grep -q "IDLE"' "and the match was not idle, so the playback is the only failure"

out="$(bash "$HARNESS" --generals "$GENERALS" --seeds "1" --cells 128 --maxframes 1200 2>&1)"
status=$?
printf '%s\n' "$out"
check '[ $status -eq 1 ] && printf "%s" "$out" | grep -q "IDLE: the AI built nothing"' "an idle match (seed 1, 128 cells) is reported IDLE (exit $status)"

out="$(bash "$HARNESS" --generals "$GENERALS" --seeds "1" --maxframes 12000 2>&1)"
status=$?
printf '%s\n' "$out"
check '[ $status -eq 0 ]' "seed 1 at 12000 frames: fourteen checkpoint saves leave the playback on the recording's world (exit $status)"

# 7 (run here, before the install controls): port defect 33's generals, pinned.
out="$(bash "$HARNESS" --generals "$GENERALS" --seeds "0" --maxframes 12000 \
	--extra "-side 0 FactionChinaInfantryGeneral -side 1 FactionAmericaLaserGeneral" 2>&1)"
status=$?
printf '%s\n' "$out"
check '[ $status -eq 0 ]' "seed 0, Infantry against Laser, agrees with itself at 12000 frames (exit $status)"
check 'printf "%s" "$out" | grep -q "built: .*Lazr_AmericaSupplyCenter"' "the Laser General's Supply Center stood, so its Chinook was made (port defect 33)"

# 8: seed 0 at full length, the Demolition General among its sides (port defect 34).
out="$(bash "$HARNESS" --generals "$GENERALS" --seeds "0" --maxframes 12000 2>&1)"
status=$?
printf '%s\n' "$out"
check '[ $status -eq 0 ]' "seed 0 agrees with itself at 12000 frames (exit $status)"
check 'printf "%s" "$out" | grep -q "built: .*Demo_GLA"' "the Demolition General played it, so its reskinned Technicals were in the match (port defect 34)"

# 5. The install check's control: with its snapshot spoiled (the install itself untouched), the harness
#    must report the install changed and exit 99.
out="$(REPLAY_CHECK_CONTROL_INSTALL=changed bash "$HARNESS" --generals "$GENERALS" --seeds "0" --maxframes 30 2>&1)"
status=$?
printf '%s\n' "$out"
check '[ $status -eq 99 ] && printf "%s" "$out" | grep -q "INSTALL CHANGED"' "a changed install fails the run (exit $status)"

# 6. And with its snapshot removed, it must say it COULD NOT VERIFY the install - never that it changed -
#    and still exit 99.  Both controls leave no folder behind.
out="$(REPLAY_CHECK_CONTROL_INSTALL=missing bash "$HARNESS" --generals "$GENERALS" --seeds "0" --maxframes 30 2>&1)"
status=$?
printf '%s\n' "$out"
check '[ $status -eq 99 ] && printf "%s" "$out" | grep -q "COULD NOT VERIFY" && ! printf "%s" "$out" | grep -q "CHANGED"' \
	"an install that cannot be checked fails the run as COULD NOT VERIFY, not as a change (exit $status)"
check '! printf "%s" "$out" | grep -q "kept for inspection"' "and the controls keep no folder"

exit $failed
