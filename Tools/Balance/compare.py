"""Side by side: what attacking a defence cost in two siege runs.

    python compare.py <before tag> <after tag>

Per defence and attacker class, over both budgets: how often the defence stood, and the attacker's
money lost for each dollar of defence (the defence's own price in that run, garrison included).
"""

import json
import sys
from collections import defaultdict

from siege import OUT_DIR
CLASSES = {
    "tank": ("AmericaTankCrusader", "AmericaTankPaladin", "ChinaTankBattleMaster", "ChinaTankOverlord",
             "ChinaTankDragon", "GLATankScorpion", "GLATankMarauder"),
    "light vehicle": ("AmericaVehicleHumvee", "ChinaTankGattling", "GLAVehicleTechnical", "GLAVehicleQuadCannon",
                      "GLAVehicleRocketBuggy"),
    "infantry": ("AmericaInfantryRanger", "AmericaInfantryMissileDefender", "ChinaInfantryRedguard",
                 "ChinaInfantryTankHunter", "GLAInfantryRebel", "GLAInfantryTunnelDefender"),
    "artillery": ("AmericaVehicleTomahawk", "ChinaVehicleInfernoCannon", "ChinaVehicleNukeLauncher",
                  "GLAVehicleScudLauncher"),
}
CLASS_OF = {unit: name for name, units in CLASSES.items() for unit in units}


def load(tag: str) -> list[dict]:
    return [row for row in json.loads((OUT_DIR / (tag + ".json")).read_text()) if "error" not in row]


def price(row: dict) -> int:
    garrison = row["defencePrice"] - {"ChinaBunker": 400, "AmericaFireBase": 1000}.get(row["defence"], row["defencePrice"])
    return row.get("defenceWorth", row["defencePrice"]) + garrison


def summarise(rows: list[dict]) -> dict:
    cells = defaultdict(lambda: {"runs": 0, "stood": 0, "lost": 0, "price": 0})
    for row in rows:
        for key in ((row["defence"], CLASS_OF[row["attacker"]]), (row["defence"], "all")):
            cell = cells[key]
            cell["runs"] += 1
            cell["stood"] += 1 if row["defenceStanding"] else 0
            cell["lost"] += row["attackerLost"]
            cell["price"] += price(row)
    return cells


def main() -> int:
    before, after = summarise(load(sys.argv[1])), summarise(load(sys.argv[2]))
    print("%-22s %-14s %16s %16s" % ("defence", "attackers", sys.argv[1] + " stood  $/$", sys.argv[2] + " stood  $/$"))
    for key in sorted(before):
        b, a = before[key], after.get(key)
        cell = lambda c: "%2d/%-2d  %5.2f" % (c["stood"], c["runs"], c["lost"] / c["price"]) if c else "-"
        print("%-22s %-14s %16s %16s" % (key[0], key[1], cell(b), cell(a)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
