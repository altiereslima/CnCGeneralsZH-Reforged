"""Stop the skirmish AI from selling its whole base.

Every player list in the retail file has a "<Side> Sell Off" group of two scripts. "<Side> Sell off
check" is active and fires once when the AI is down to no dozer, no command centre and no factory,
or under 2,000 cash with no harvester, supply building or oil derrick left; it starts a ten second
timer and enables "<Side> Sell off", which runs PLAYER_SELL_EVERYTHING when the timer expires. This
marks the twelve checks inactive. The sell scripts already start inactive and only the checks
enable them, so neither runs.

usage: sell_off.py <in.scb> <out.scb>

The master is the retail file through opening.py H 1 4 5 6, then infantry_drop.py, then
ai_bonus.py, then this.
"""
import sys

import scbtool

ACTIVE = 4
EXPECTED = 12


def main():
    source, target = sys.argv[1], sys.argv[2]
    table, chunks = scbtool.load(source)
    list_chunk = next(chunk for chunk in chunks if chunk.name == "PlayerScriptsList")
    count = 0
    for index, script_list in enumerate(list_chunk.children):
        for group, script in scbtool.walk_scripts(script_list.children):
            if not group.endswith(" Sell Off"):
                continue
            actions = [child.fields[1] for child in script.children if child.name != "OrCondition"]
            if script.fields[0].endswith(" Sell off"):
                if actions != ["PLAYER_SELL_EVERYTHING"] or script.fields[ACTIVE]:
                    raise SystemExit(f"[{index}] {script.fields[0]!r} is not an inactive PLAYER_SELL_EVERYTHING")
                continue
            if actions != ["SET_MILLISECOND_TIMER", "ENABLE_SCRIPT"]:
                raise SystemExit(f"[{index}] {script.fields[0]!r} does {actions}, not the sell off check")
            if not script.fields[ACTIVE]:
                raise SystemExit(f"[{index}] {script.fields[0]!r} is already inactive; run this on ai_bonus.py's output")
            script.fields[ACTIVE] = 0
            count += 1
    if count != EXPECTED:
        raise SystemExit(f"found {count} sell off checks, expected {EXPECTED}")
    print(f"{count} sell off checks switched off")
    scbtool.save(target, table, chunks)


if __name__ == "__main__":
    main()
