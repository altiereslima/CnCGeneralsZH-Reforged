"""Give the Infantry General's skirmish AI its Mini-Gunner paradrop on Normal.

EA wrote the paradrop scripts for player [9], SkirmishChinaInfantryGeneral, on Hard alone ("China
Infantry Drop Fire - H" and "China Infantry Drop AI - H"), although both of its skill sets buy the
three Infa_SCIENCE_InfantryParadrop ranks, so on Normal it spends three points and never drops. The
Tank General has a Normal pair, "China Tank Drop Fire" and "China Tank Drop AI", that waits for the
power and nothing else. This copies the Hard pair to Normal in that shape: the _LAUNCH_ATTACK flag
left out of the condition, the difficulty flags Normal only, and the script names they enable and
disable renamed to match.

usage: infantry_drop.py <in.scb> <out.scb>

The master is the retail file through opening.py H 1 4 5 6, then through this, then ai_bonus.py.
"""
import copy
import sys

import scbtool

PLAYER = 9
HARD_SUFFIX = " - H"
PAIR = ("China Infantry Drop Fire - H", "China Infantry Drop AI - H")
SCRIPT_PARAMETER = 2
EASY, NORMAL, HARD = 6, 7, 8


def find_parent(container, script_name):
    for index, child in enumerate(container.children):
        if child.name == "Script" and child.fields[0] == script_name:
            return container, index
        found = find_parent(child, script_name) if child.name in ("ScriptGroup", "ScriptList") else None
        if found:
            return found
    return None


def normal_copy(hard_script):
    script = copy.deepcopy(hard_script)
    script.fields[0] = script.fields[0][:-len(HARD_SUFFIX)]
    script.fields[EASY], script.fields[NORMAL], script.fields[HARD] = 0, 1, 0
    for condition in (child for child in script.children if child.name == "OrCondition"):
        condition.children = [call for call in condition.children if call.fields[1] != "FLAG"]
    for action in (child for child in script.children if child.name in ("ScriptAction", "ScriptActionFalse")):
        for parameter in action.fields[2]:
            if parameter[0] == SCRIPT_PARAMETER and parameter[3].endswith(HARD_SUFFIX):
                parameter[3] = parameter[3][:-len(HARD_SUFFIX)]
    return script


def main():
    source, target = sys.argv[1], sys.argv[2]
    table, chunks = scbtool.load(source)
    list_chunk = next(chunk for chunk in chunks if chunk.name == "PlayerScriptsList")
    script_list = list_chunk.children[PLAYER]
    for name in PAIR:
        found = find_parent(script_list, name)
        if found is None:
            raise SystemExit(f"player [{PLAYER}] has no script named {name!r}")
        parent, index = found
        if find_parent(script_list, name[:-len(HARD_SUFFIX)]):
            raise SystemExit(f"player [{PLAYER}] already has {name[:-len(HARD_SUFFIX)]!r}; run this on opening.py's output")
        parent.children.insert(index + 1, normal_copy(parent.children[index]))
        print(f"[{PLAYER}] {name[:-len(HARD_SUFFIX)]}")
    scbtool.save(target, table, chunks)


if __name__ == "__main__":
    main()
