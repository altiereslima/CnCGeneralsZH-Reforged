"""Read a mirrored duel run: each pair fought twice with the ground swapped, the two money shares averaged.

    python duel_report.py <tag>

A share is the fraction of the money still standing at the end that belongs to that side: 1.0 wiped
the other army without loss, 0.5 is even. Averaging the two orientations cancels whichever side the
terrain favours; the spread between them is printed so a pair the ground decided can be seen.
"""

import json
import sys
from collections import defaultdict

from siege import OUT_DIR
EVEN = 0.5


def share(left_left: int, right_left: int) -> float:
    total = left_left + right_left
    return EVEN if total == 0 else left_left / total


def main() -> int:
    rows = json.loads((OUT_DIR / ("duel_" + sys.argv[1] + ".json")).read_text())
    by_pair = defaultdict(list)
    for row in rows:
        if None in (row["leftLeft"], row["rightLeft"]):
            continue
        by_pair[(row["left"], row["right"])].append(share(row["leftLeft"], row["rightLeft"]))

    per_unit = defaultdict(list)
    decided_by_ground = 0
    matchups = []
    for (left, right), shares in by_pair.items():
        if len(shares) != 2:
            continue
        mean = sum(shares) / 2
        if (shares[0] - EVEN) * (shares[1] - EVEN) < 0:
            decided_by_ground += 1
        per_unit[left].append(mean)
        per_unit[right].append(1 - mean)
        matchups.append((mean, left, right, shares))

    print("unit                             mean share  clear wins  pairs")
    for unit, shares in sorted(per_unit.items(), key=lambda kv: -sum(kv[1]) / len(kv[1])):
        print("%-32s %10.2f  %10d  %5d" % (unit, sum(shares) / len(shares), sum(1 for s in shares if s > 0.75),
                                            len(shares)))
    print("\n%d of %d pairs flipped winner with the ground" % (decided_by_ground, len(matchups)))
    print("\nmost one-sided (both orientations agree):")
    for mean, left, right, shares in sorted(matchups, key=lambda m: -abs(m[0] - EVEN))[:25]:
        winner, loser = (left, right) if mean > EVEN else (right, left)
        print("  %-30s beats %-30s %.2f  (%.2f / %.2f)" % (winner, loser, max(mean, 1 - mean), *shares))
    return 0


if __name__ == "__main__":
    sys.exit(main())
