"""Switch off the Hard skirmish AI's free "AI Bonus" cash.

Every player list in the retail file has four scripts in its "<Side> World State Detection" group,
"<Side> AI Bonus Early", "Mid", "Late" and "Really Late", Hard only, that wait for the _ESCALATION
counter to pass a threshold and then hand the AI 5,000 to 20,000 with PLAYER_GIVE_MONEY. This marks
all of them inactive. Nothing enables a script by those names, so they never run. The other grants
(base building, "Give AI money to cover bug") stay as they are.

usage: ai_bonus.py <in.scb> <out.scb>

The master is the retail file through opening.py H 1 4 5 6, then infantry_drop.py, then this.
"""
import sys

import scbtool

GROUP_SUFFIX = " World State Detection"
STAGES = ("Early", "Mid", "Late", "Really Late")
ACTIVE = 4
EXPECTED = 48


def is_bonus(group, script):
    side = group[:-len(GROUP_SUFFIX)]
    return group.endswith(GROUP_SUFFIX) and script.fields[0] in [f"{side} AI Bonus {stage}" for stage in STAGES]


def main():
    source, target = sys.argv[1], sys.argv[2]
    table, chunks = scbtool.load(source)
    list_chunk = next(chunk for chunk in chunks if chunk.name == "PlayerScriptsList")
    count = 0
    for index, script_list in enumerate(list_chunk.children):
        for group, script in scbtool.walk_scripts(script_list.children):
            if not is_bonus(group, script):
                continue
            actions = [child.fields[1] for child in script.children if child.name != "OrCondition"]
            if actions != ["PLAYER_GIVE_MONEY"]:
                raise SystemExit(f"[{index}] {script.fields[0]!r} does {actions}, not one PLAYER_GIVE_MONEY")
            if not script.fields[ACTIVE]:
                raise SystemExit(f"[{index}] {script.fields[0]!r} is already inactive; run this on infantry_drop.py's output")
            script.fields[ACTIVE] = 0
            count += 1
    if count != EXPECTED:
        raise SystemExit(f"found {count} AI Bonus scripts, expected {EXPECTED}")
    print(f"{count} AI Bonus scripts switched off")
    scbtool.save(target, table, chunks)


if __name__ == "__main__":
    main()
