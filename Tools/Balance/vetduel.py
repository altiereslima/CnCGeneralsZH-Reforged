"""A ranked army against a bigger regular one; run it once with each rank ladder and compare.

Every side's veterans fight regular armies of both other sides, tanks against tanks and infantry
against infantry, at each experience point, from both start points and on two seeds. A point is N
same-class kills of experience (the scenario's `rank ... xp N`, docs/plans/RANK-LADDER.md's
experience duels), auras off. The regular army's money is the ranked army's times three quarters of
the ladder's stat worth at that point (rate of fire x damage x health, 1 + 1.4 sqrt(N / 120)), so a
fight is close when the grade gives what the curve says and a change to the split moves it. Every
side should agree within 0.1 at the same point.

The answer per fight is the ranked army's health left as a fraction of its own at the start, minus
the regular army's: +1 the ranked army lost nothing and wiped the other, -1 the reverse. The report
averages it per side and point over the side's 16 fights (4 pairs, 2 start points, 2 seeds).

    python vetduel.py <exe name in Run> <tag> [pair filter]
    python vetduel.py --report <tag> [<tag> ...]
"""

import concurrent.futures
import json
import re
import subprocess
import sys

import siege

TANK_BUDGET = 4800
INFANTRY_BUDGET = 1800
MAX_FRAMES = 3000 * siege.LOGIC_FPS // siege.SCENARIO_FPS
SPAWN_FRAME = 30
RANK_FRAME = 40
ATTACK_FRAME = 60
TALLY_FRAME = 2950
LEFT_AT = (500, 1100)   # open ground on generated map 1 small: a block of 42 Rebels or 19 Overlords
RIGHT_AT = (1250, 1100)  # stands whole at either
SPACING = 25
HANDICAP = 0.75  # the regular army's money as a share of an even fight's
SEEDS = (1, 2)

RANKS = {"xp%d" % kills: 1 + 1.4 * (kills / 120) ** 0.5 for kills in (2, 5, 10, 20, 30, 45, 60, 80, 100, 120)}
UNITS = {  # side: (tank, its price, infantry, its price)
    "USA": ("AmericaTankCrusader", 900, "AmericaInfantryRanger", 225),
    "China": ("ChinaTankBattleMaster", 800, "ChinaInfantryRedguard", 300),
    "GLA": ("GLATankScorpion", 600, "GLAInfantryRebel", 150),
}
PAIRS = [(side, UNITS[side][kind], UNITS[side][kind + 1], UNITS[other][kind], UNITS[other][kind + 1])
         for side in UNITS for other in UNITS if other != side for kind in (0, 2)]


def run_one(exe: str, tag: str, index: int, pair: tuple, rank: str, swapped: bool, seed: int) -> dict:
    side, ranked, ranked_price, regular, regular_price = pair
    ranked_at, regular_at = (RIGHT_AT, LEFT_AT) if swapped else (LEFT_AT, RIGHT_AT)
    budget = INFANTRY_BUDGET if "Infantry" in ranked else TANK_BUDGET
    ranked_count = round(budget / ranked_price)
    regular_count = round(budget * RANKS[rank] * HANDICAP / regular_price)
    name = "vd%s%03d" % (tag, index)
    lines = [
        "%d spawn 0 %s %d %d %d %d" % (SPAWN_FRAME, ranked, ranked_count, *ranked_at, SPACING),
        "%d spawn 1 %s %d %d %d %d" % (SPAWN_FRAME, regular, regular_count, *regular_at, SPACING),
        "%d rank 0 %s xp %s" % (RANK_FRAME, siege.selector(ranked), rank[2:]),
        "%d attackmove 0 %s %d %d" % (ATTACK_FRAME, siege.selector(ranked), *regular_at),
        "%d attackmove 1 %s %d %d" % (ATTACK_FRAME, siege.selector(regular), *ranked_at),
    ]
    for frame in (ATTACK_FRAME, TALLY_FRAME):
        lines.append("%d tally 0 %s" % (frame, siege.selector(ranked)))
        lines.append("%d tally 1 %s" % (frame, siege.selector(regular)))
    scenario = siege.SCENARIO_DIR / (name + ".txt")
    scenario.write_text("\n".join(lines) + "\n", encoding="ascii")
    log = siege.RUN_DIR / (name + "_DebugLogFile.txt")
    log.unlink(missing_ok=True)
    arguments = [str(siege.RUN_DIR / exe), "-headless", "-quickstart", "-noshellmap", "-observer", "-multiInstance",
                 "-noFPSLimit", "-randommap", "1", "2", "small", "-autoskirmish", "2", "-takeover",
                 "-side", "0", "FactionAmerica", "-side", "1", "FactionChina", "-seed", str(seed),
                 "-maxframes", str(MAX_FRAMES), "-scenario", name, "-logPrefix", name + "_"]
    process = subprocess.Popen(arguments, cwd=siege.RUN_DIR, creationflags=subprocess.ABOVE_NORMAL_PRIORITY_CLASS)
    try:
        process.wait(timeout=siege.TIMEOUT_SECONDS)
    except subprocess.TimeoutExpired:
        process.kill()
    scenario.unlink()
    health = {}
    for match in siege.TALLY.finditer(log.read_text(encoding="latin-1", errors="replace")):
        health[(int(match.group(1)), int(match.group(2)))] = int(match.group(5))
    log.unlink()
    return {"side": side, "ranked": ranked, "regular": regular, "rank": rank, "swapped": swapped, "seed": seed,
            "rankedStart": health.get((ATTACK_FRAME, 0)), "rankedEnd": health.get((TALLY_FRAME, 0)),
            "regularStart": health.get((ATTACK_FRAME, 1)), "regularEnd": health.get((TALLY_FRAME, 1))}


def net(row: dict) -> float:
    return row["rankedEnd"] / row["rankedStart"] - row["regularEnd"] / row["regularStart"]


def report(tags: list) -> None:
    tables = {}
    for tag in tags:
        cells = {}
        for row in json.loads((siege.OUT_DIR / ("vetduel_" + tag + ".json")).read_text()):
            if None not in (row["rankedStart"], row["rankedEnd"], row["regularStart"], row["regularEnd"]):
                cells.setdefault((row["side"], row["rank"]), []).append(net(row))
        tables[tag] = {key: (sum(v) / len(v), len(v)) for key, v in cells.items()}
    print("%-6s %-8s" % ("side", "rank") + "".join("%14s" % tag for tag in tags))
    for side in UNITS:
        for rank in RANKS:
            print("%-6s %-8s" % (side, rank) + "".join(
                "%14s" % ("%+.2f (%d)" % tables[tag][(side, rank)] if (side, rank) in tables[tag] else "-")
                for tag in tags))


def main() -> int:
    if sys.argv[1] == "--report":
        report(sys.argv[2:])
        return 0
    exe, tag = sys.argv[1], sys.argv[2]
    runs = [(pair, rank, swapped, seed) for pair in PAIRS for rank in RANKS for swapped in (False, True)
            for seed in SEEDS if len(sys.argv) < 4 or re.search(sys.argv[3], pair[1] + " " + pair[3])]
    results = []
    with concurrent.futures.ThreadPoolExecutor(siege.PARALLEL_RUNS) as pool:
        for row in pool.map(lambda item: run_one(exe, tag, item[0], *item[1]), enumerate(runs)):
            results.append(row)
            print("%-24s %-8s %s/%s  vs  %-24s %s/%s" % (row["ranked"], row["rank"], row["rankedEnd"], row["rankedStart"],
                                                       row["regular"], row["regularEnd"], row["regularStart"]), flush=True)
    siege.OUT_DIR.mkdir(exist_ok=True)
    (siege.OUT_DIR / ("vetduel_" + tag + ".json")).write_text(json.dumps(results, indent=1), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
