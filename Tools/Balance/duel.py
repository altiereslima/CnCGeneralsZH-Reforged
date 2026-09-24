"""Equal-money armies of two unit types meet on open ground; the money each side has left is the answer.

Every cross-faction pair fights twice with the two starting points swapped: on generated map 1 the
side starting at 1000,1150 won 118 of 183 decided fights, so one orientation measures the ground.
duel_report.py averages the two.

    python duel.py <exe name in Run> <tag> [budget] [pair filter]
"""

import concurrent.futures
import itertools
import json
import re
import subprocess
import sys

import siege

BUDGET = 4800
MAX_FRAMES = 3000
TALLY_FRAME = 2950
SPAWN_FRAME = 30
ATTACK_FRAME = 60
LEFT_AT = (760, 800)
RIGHT_AT = (1000, 1150)
MEET_AT = (880, 975)

# the direct-fire ground core each side builds most of
UNITS = {
    "AmericaTankCrusader": 900, "AmericaTankPaladin": 1100, "AmericaVehicleHumvee": 700,
    "AmericaInfantryRanger": 225, "AmericaInfantryMissileDefender": 300,
    "ChinaTankBattleMaster": 800, "ChinaTankOverlord": 2000, "ChinaTankGattling": 800, "ChinaTankDragon": 800,
    "ChinaInfantryRedguard": 300, "ChinaInfantryTankHunter": 300,
    "GLATankScorpion": 600, "GLATankMarauder": 800, "GLAVehicleTechnical": 500, "GLAInfantryRebel": 150,
    "GLAInfantryTunnelDefender": 300, "GLAVehicleQuadCannon": 700,
}


def run_one(exe: str, tag: str, index: int, left: str, right: str, budget: int, swapped: bool) -> dict:
    left_at, right_at = (RIGHT_AT, LEFT_AT) if swapped else (LEFT_AT, RIGHT_AT)
    left_count = max(1, round(budget / UNITS[left]))
    right_count = max(1, round(budget / UNITS[right]))
    name = "dl%s%03d" % (tag, index)
    lines = [
        "%d spawn 0 %s %d %d %d 35" % (SPAWN_FRAME, left, left_count, *left_at),
        "%d spawn 1 %s %d %d %d 35" % (SPAWN_FRAME, right, right_count, *right_at),
        "%d attackmove 0 %s %d %d" % (ATTACK_FRAME, siege.selector(left), *right_at),
        "%d attackmove 1 %s %d %d" % (ATTACK_FRAME, siege.selector(right), *left_at),
    ]
    for frame in (ATTACK_FRAME, TALLY_FRAME):
        lines.append("%d tally 0 %s" % (frame, siege.selector(left)))
        lines.append("%d tally 1 %s" % (frame, siege.selector(right)))
    scenario = siege.SCENARIO_DIR / (name + ".txt")
    scenario.write_text("\n".join(lines) + "\n", encoding="ascii")
    log = siege.RUN_DIR / (name + "_DebugLogFile.txt")
    log.unlink(missing_ok=True)
    arguments = [str(siege.RUN_DIR / exe), "-headless", "-quickstart", "-noshellmap", "-observer", "-multiInstance",
                 "-noFPSLimit", "-randommap", "1", "2", "small", "-autoskirmish", "2", "-takeover",
                 "-side", "0", "FactionAmerica", "-side", "1", "FactionChina", "-seed", "1",
                 "-maxframes", str(MAX_FRAMES), "-scenario", name, "-logPrefix", name + "_"]
    process = subprocess.Popen(arguments, cwd=siege.RUN_DIR, creationflags=subprocess.ABOVE_NORMAL_PRIORITY_CLASS)
    try:
        process.wait(timeout=siege.TIMEOUT_SECONDS)
    except subprocess.TimeoutExpired:
        process.kill()
    scenario.unlink()
    tallies = {}
    for match in siege.TALLY.finditer(log.read_text(encoding="latin-1", errors="replace")):
        # counted in units, not worth: a Technical's chassis templates carry no build cost
        slot = int(match.group(2))
        tallies[(int(match.group(1)), slot)] = int(match.group(4)) * UNITS[left if slot == 0 else right]
    log.unlink()
    return {"left": left, "right": right, "swapped": swapped, "leftSpent": tallies.get((ATTACK_FRAME, 0)),
            "rightSpent": tallies.get((ATTACK_FRAME, 1)), "leftLeft": tallies.get((TALLY_FRAME, 0)),
            "rightLeft": tallies.get((TALLY_FRAME, 1))}


def main() -> int:
    exe, tag = sys.argv[1], sys.argv[2]
    budget = int(sys.argv[3]) if len(sys.argv) > 3 else BUDGET
    faction = lambda name: re.match(r"America|China|GLA", name).group(0)
    pairs = [(a, b, swapped) for a, b in itertools.combinations(UNITS, 2) if faction(a) != faction(b)
             for swapped in (False, True)]
    if len(sys.argv) > 4:
        pairs = [pair for pair in pairs if re.search(sys.argv[4], pair[0] + " " + pair[1])]
    results = []
    with concurrent.futures.ThreadPoolExecutor(siege.PARALLEL_RUNS) as pool:
        for row in pool.map(lambda item: run_one(exe, tag, item[0], item[1][0], item[1][1], budget, item[1][2]), enumerate(pairs)):
            results.append(row)
            print("%-32s %s/%s  vs  %-32s %s/%s" % (row["left"], row["leftLeft"], row["leftSpent"], row["right"],
                                                   row["rightLeft"], row["rightSpent"]), flush=True)
    siege.OUT_DIR.mkdir(exist_ok=True)
    (siege.OUT_DIR / ("duel_" + tag + ".json")).write_text(json.dumps(results, indent=1), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
