#!/usr/bin/env python3
"""Add the campaign mission panel to the main menu layout.

Picking USA, GLA or China in the single player menu opens a panel with a button for each of that
campaign's missions, so a campaign can be started at any mission; the difficulty panel follows the
pick as it always did.  The panel is the single player menu's own (MapBorder: six buttons in a
frame) cloned whole and renamed, which is how it keeps EA's frame, button art, font and sounds:

    MapBorder5 / EarthMap5
        ButtonMission1 .. ButtonMission5     the campaign's missions, top to bottom
        ButtonMissionBack                    back to the single player menu

It sits exactly where the single player menu does, which is hidden while it shows.  The labels are
set by MainMenu.cpp from GUI:CampaignMission; the transitions that flash it in and out are in
Data/INI/WindowTransitionsReforged.ini.

    python bigfile.py extract ../../Run/WindowZH.big "Window\\Menus\\MainMenu.wnd" -o wnd
    python mainmenu_layout.py wnd/Window/Menus ../Data/Window/Menus

The output is a tracked master under Code/Data; the build copies it to Run/Window/Menus/, where a
loose file beats WindowZH.big.

    python mainmenu_layout.py selfcheck

reads the tracked files back and checks they agree: the layout carries every window the code and
the Reforged transitions name, and Patch.str carries the label.  Run by CTest as
mainmenu_selfcheck.
"""

import os
import re
import sys

import wndlayout
from wndlayout import clone


HERE = os.path.dirname(os.path.abspath(__file__))
MENUS = os.path.join(HERE, "..", "Data", "Window", "Menus")
PATCH_STR = os.path.join(HERE, "..", "Data", "Patch.str")
TRANSITIONS = os.path.join(HERE, "..", "Data", "INI", "WindowTransitionsReforged.ini")

MENU = "MainMenu"

# the single player menu's windows -> their names in the clone, top button first
RENAME = [
    ("MapBorder", "MapBorder5"),
    ("EarthMap", "EarthMap5"),
    ("ButtonUSA", "ButtonMission1"),
    ("ButtonGLA", "ButtonMission2"),
    ("ButtonChina", "ButtonMission3"),
    ("ButtonChallenge", "ButtonMission4"),
    ("ButtonSkirmish", "ButtonMission5"),
    ("ButtonSingleBack", "ButtonMissionBack"),
]

STRINGS = ["GUI:CampaignMission"]


def drop_prop(window, key):
    """Remove a one-line statement, if the window carries it."""
    i = window.prop_index(key)
    if i >= 0:
        assert ";" in window.props[i], (window.name, key)
        del window.props[i]


def build(layout):
    template = layout.find("MapBorder")
    if template is None:
        raise SystemExit("%s has no MapBorder to clone" % MENU)

    panel = clone(template)
    for old, new in RENAME:
        window = panel.find(old)
        if window is None:
            raise SystemExit("%s's MapBorder has no %s" % (MENU, old))
        window.name = "%s.wnd:%s" % (MENU, new)
        if new.startswith("ButtonMission") and new != "ButtonMissionBack":
            window.set_prop("TEXT", '"GUI:CampaignMission"')
            drop_prop(window, "TOOLTIPTEXT")

    leftover = [w.name for w in panel.walk() if w.name not in
                ["%s.wnd:%s" % (MENU, new) for _, new in RENAME]]
    if leftover:
        raise SystemExit("MapBorder has windows this tool does not rename: %s" % leftover)

    layout.root.children.append(panel)
    return layout


def read_strings():
    keys = set()
    with open(PATCH_STR, "r", encoding="utf-8") as fp:
        for line in fp:
            line = line.strip()
            if line and not line.startswith("//") and not line.startswith('"') and line != "END":
                keys.add(line)
    return keys


def transition_windows():
    with open(TRANSITIONS, "r") as fp:
        return set(re.findall(r"WinName\s*=\s*%s\.wnd:(\w+)" % MENU, fp.read()))


def selfcheck():
    problems = []

    keys = read_strings()
    for key in STRINGS:
        if key not in keys:
            problems.append("%s is not in Patch.str" % key)

    path = os.path.join(MENUS, MENU + ".wnd")
    names = set()
    if not os.path.exists(path):
        problems.append("%s.wnd is not tracked under Data/Window/Menus" % MENU)
    else:
        layout = wndlayout.load(path)
        names = set((w.name or "").split(":")[-1] for w in layout.root.walk())

    wanted = set(new for _, new in RENAME) | transition_windows()
    for name in sorted(wanted - names):
        problems.append("%s.wnd has no %s" % (MENU, name))

    for problem in problems:
        print("  %s" % problem)
    if problems:
        print("%d problem(s)" % len(problems))
        return 1

    print("main menu: %d windows checked, %d strings" % (len(wanted), len(STRINGS)))
    return 0


def main(argv):
    if len(argv) == 2 and argv[1] == "selfcheck":
        return selfcheck()

    if len(argv) != 3:
        print(__doc__)
        return 2

    indir, outdir = argv[1], argv[2]
    layout = build(wndlayout.load(os.path.join(indir, MENU + ".wnd")))
    out = os.path.join(outdir, MENU + ".wnd")
    wndlayout.save(layout, out)
    print("%s: %d windows" % (out, sum(1 for _ in layout.root.walk())))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
