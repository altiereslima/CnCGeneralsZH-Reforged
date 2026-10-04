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
# L1's POSIX harness (Tools/net-check.sh) checked on itself, with the game's data.
#   0. On macOS, the firewall gate's control: socketfilterfw fakes reporting the firewall, stealth mode
#      or block-all on each make the harness skip (77) before any copy or the probe listens.
#   L. The machine-wide lock: held by someone else past NET_CHECK_LOCK_WAIT, the harness skips (77)
#      naming the holder and starts no copy; held for a few seconds, it waits and then goes ahead.
#   F. The descriptor guard: a stand-in second copy that opens sockets until it is refused.  Under the
#      harness's ulimit it is refused at the limit, not at the machine's file table, and the watchdog
#      stops both copies and fails the run with "DESCRIPTOR LEAK".
#   1. Two headless copies on this machine play seed 3 on Golden Oasis with two AIs to frame 1800 over
#      127.0.0.1 and a second local address: neither logs a CRC mismatch, both stop on one CRC, the AI
#      built something, and each copy's replay plays back to that CRC, aligned "recorded from frame 0" and
#      never out of sync.
#   2. The armed control: the same with the second copy on the next seed.  The harness must report the
#      match FAILED with the game's own CRC mismatch, and exit 1 - so a pass in 1 is a comparison that
#      can fail.
#   3. The playback check, live to the end and aligned to both kinds of recording: copy 0's replay has
#      its CRCs at frame 1500 changed by one bit and must be reported out of sync at exactly frame 1500,
#      while copy 1's has its frame 0 CRC records removed (shaped like a retail or pre-d9eccdda replay)
#      and must align as "legacy: frame 0 missing" and play back clean.
#
# WHAT THIS PROVES: two processes of this build keep one world over the real network code on ONE
# machine.  Nothing about a Windows peer, a real network, or the LAN lobby: see the harness's header.
#
# Skips (77) as the harness does: no ZH_DATA_DIR, no second local address, a game port in use, the
# macOS application firewall on (it would ask in a dialog), no python3.
# Usage: run_net_check.sh <generals>
set -u
GENERALS="$1"
HARNESS="$(cd "$(dirname "$0")/../Tools" && pwd)/net-check.sh"
if [ -z "${ZH_DATA_DIR:-}" ]; then
	echo "skip: no game data (ZH_DATA_DIR)"
	exit 77
fi
failed=0
check() { if eval "$1"; then echo "ok: $2"; else echo "FAIL: $2"; failed=1; fi; }

# 0. The firewall gate's control (macOS): fakes of socketfilterfw reporting the firewall on, stealth mode on
#    and block-all on must each make the harness skip before anything listens - no copy, no probe.
if [ "$(uname -s)" = "Darwin" ]; then
	FAKE="$(mktemp -d "${TMPDIR:-/tmp}/net-check-fw.XXXXXX")" || FAKE=""
	if [ -z "$FAKE" ] || [ ! -d "$FAKE" ]; then
		echo "run_net_check: cannot make a work folder under ${TMPDIR:-/tmp}" >&2
		exit 2
	fi
	fake() {	# fake <name> <global> <stealth> <blockall>
		printf '#!/bin/sh\ncase "$1" in --getglobalstate) echo "%s";; --getstealthmode) echo "%s";; --getblockall) echo "%s";; esac\n' \
			"$2" "$3" "$4" > "$FAKE/$1"; chmod +x "$FAKE/$1"
	}
	fake on "Firewall is enabled. (State = 1)" "Firewall stealth mode is off" "Firewall has block all state set to disabled."
	fake stealth "Firewall is disabled. (State = 0)" "Firewall stealth mode is on" "Firewall has block all state set to disabled."
	fake blockall "Firewall is disabled. (State = 0)" "Firewall stealth mode is off" "Firewall has block all state set to enabled."
	for f in on stealth blockall; do
		out="$(NET_CHECK_FIREWALL_TOOL="$FAKE/$f" bash "$HARNESS" --generals "$GENERALS" --frames 30 2>&1)"
		status=$?
		check '[ $status -eq 77 ] && printf "%s" "$out" | grep -q "^skip: .*firewall" && ! printf "%s" "$out" | grep -q "^net-check:"' \
			"a firewall reported $f makes the harness skip before any copy starts (exit $status: $out)"
	done
	rm -rf -- "${FAKE:?}"
fi

# L. the lock.  A private lock file for the case where nothing starts; the real one for the case that
#    goes on to start copies, so those still take their turn on the machine.
LT="$(mktemp -d "${TMPDIR:-/tmp}/net-check-lock.XXXXXX")" || LT=""
if [ -z "$LT" ] || [ ! -d "$LT" ]; then
	echo "run_net_check: cannot make a work folder under ${TMPDIR:-/tmp}" >&2
	exit 2
fi
hold() {	# hold <lock file> <seconds>: holds it (waiting for it first) in the background; prints once held
	python3 - "$1" "$2" <<'HOLD_EOF' &
import fcntl, sys, time
f = open(sys.argv[1], "a")
fcntl.flock(f, fcntl.LOCK_EX)
f.seek(0); f.truncate(); f.write("the test's holder"); f.flush()
print("held", flush=True)
time.sleep(float(sys.argv[2]))
HOLD_EOF
}
hold "$LT/lock" 30 > "$LT/held" ; HOLDER=$!
until grep -q held "$LT/held" 2>/dev/null; do sleep 0.2; done
out="$(NET_CHECK_LOCK_FILE="$LT/lock" NET_CHECK_LOCK_WAIT=3 bash "$HARNESS" --generals "$GENERALS" --frames 30 2>&1)"
status=$?
kill $HOLDER 2>/dev/null; wait $HOLDER 2>/dev/null
check '[ $status -eq 77 ] && printf "%s" "$out" | grep -q "^skip: another net-check held the machine-wide lock for 3 s (the test.s holder)" && ! printf "%s" "$out" | grep -q "^net-check: 127"' \
	"a lock held past the wait skips, naming its holder, before any copy starts (exit $status)"
hold /tmp/zhr-net-check.lock 6 > "$LT/held2" ; HOLDER=$!
until grep -q held "$LT/held2" 2>/dev/null; do sleep 0.2; done
out="$(bash "$HARNESS" --generals "$GENERALS" --frames 30 2>&1)"
wait $HOLDER 2>/dev/null
check 'printf "%s" "$out" | grep -q "^net-check: waiting up to [0-9]* s for another net-check on this machine (the test.s holder)" && printf "%s" "$out" | grep -q "^net-check: 127\.0\.0\.1,"' \
	"a lock held for a few seconds is waited for, and then the harness goes ahead"

# F. the descriptor guard: a stand-in second copy that leaks sockets
cat > "$LT/leaker" <<'LEAK_EOF'
#!/usr/bin/env python3
import socket, sys, time
held = []
try:
    while True:
        held.append(socket.socket(socket.AF_INET, socket.SOCK_DGRAM))
except OSError as e:
    held.pop().close()		# one descriptor back, or the count file cannot be opened either
    open(sys.argv[0] + ".count", "w").write("%d %s" % (len(held) + 1, e.strerror))
time.sleep(120)			# and hold the rest, as a real leak would
LEAK_EOF
chmod +x "$LT/leaker"
out="$(NET_CHECK_LIVE_TIMEOUT=120 bash "$HARNESS" --generals "$GENERALS" --peer1 "$LT/leaker" --frames 1800 2>&1)"
status=$?
printf '%s\n' "$out" | grep -E "match|LEAK" | head -3
leaked="$(cat "$LT/leaker.count" 2>/dev/null)"
check '[ $status -eq 1 ] && printf "%s" "$out" | grep -q "DESCRIPTOR LEAK: process [0-9]* held [0-9]* descriptors"' \
	"a copy leaking descriptors is stopped and the run fails saying so (exit $status)"
check '[ -n "$leaked" ] && [ "${leaked%% *}" -le 4096 ]' \
	"and the leak ended at the harness's limit, not the machine's (the stand-in got $leaked)"
check '! pgrep -f "$LT/leaker" >/dev/null' "and the stand-in is gone"
rm -rf -- "${LT:?}"

out="$(bash "$HARNESS" --generals "$GENERALS" --frames 1800 2>&1)"
status=$?
printf '%s\n' "$out"
if [ $status -eq 77 ]; then
	exit 77		# the harness said why
fi
check '[ $status -eq 0 ]' "two copies agree over the network to frame 1800 (exit $status)"
check 'printf "%s" "$out" | grep -q "^AGREED: .* to frame 1800"' "and say so"

out="$(NET_CHECK_LIVE_TIMEOUT=300 bash "$HARNESS" --generals "$GENERALS" --frames 1800 --control 2>&1)"
status=$?
printf '%s\n' "$out"
check '[ $status -eq 1 ]' "the control (the second copy on another seed) fails the harness (exit $status)"
check 'printf "%s" "$out" | grep -q "logged [1-9][0-9]* CRC mismatches"' "through the game's own CRC mismatch"

# The frame comparison's control: copies that run past the limit, as two did under load (1801 and 1802),
# are still compared at the limit's own frame, and still agree.
out="$(bash "$HARNESS" --generals "$GENERALS" --frames 1800 --overshoot-control 2>&1)"
status=$?
printf '%s\n' "$out"
check '[ $status -eq 0 ] && printf "%s" "$out" | grep -q "^AGREED: .* to frame 1800"' \
	"copies that ran one and two frames past the limit still agree at frame 1800 (exit $status)"
check 'printf "%s" "$out" | grep -q "copy 0: HEADLESS CRC AT LIMIT 0x[0-9A-Fa-f]* at frame 1800, stopped at frame 1801" && printf "%s" "$out" | grep -q "copy 1: HEADLESS CRC AT LIMIT 0x[0-9A-Fa-f]* at frame 1800, stopped at frame 1802"' \
	"and they did stop on 1801 and 1802, so the comparison was at the limit and not where they stopped"

out="$(bash "$HARNESS" --generals "$GENERALS" --frames 1800 --corrupt-at 1500 --legacy-replay 2>&1)"
status=$?
printf '%s\n' "$out"
check '[ $status -eq 1 ] && printf "%s" "$out" | grep -q "^FAILED: copy 0.s replay: out of sync on frame 1500: [^;]*;$"' \
	"a recorded CRC corrupted at frame 1500 is reported at frame 1500, and nothing else fails (exit $status)"
check 'printf "%s" "$out" | grep -q "copy 1.s replay played back: .*its CRCs: legacy: frame 0 missing$"' \
	"a replay without its frame 0 CRC aligns as legacy and plays back clean"

exit $failed
