#!/usr/bin/env python3
"""Put the money transfer column into the Tab screen (Diplomacy.wnd).

Each player row of the in-game list gets a minus button, the amount, a plus button and a Send
button, under a "Send money" title.  EA's window had no room right of the status column, so the
whole screen grows 170 pixels to the right (the parent, the in-game page, the briefing page and its
listbox) and the Main Menu button moves with the edge.  Every new control is cloned from one EA
already drew there - the buttons from ButtonHide, the amount and the title from the status column -
so they keep EA's images and colours instead of looking like something a tool made.

    python bigfile.py extract ../../Run/WindowZH.big "*iplomacy*" -o wnd
    python diplomacy_layout.py wnd/Window ../Data/Window

The output is a tracked master under Code/Data; the build copies it to Run/Window, where a loose
file beats WindowZH.big.  Layouts are not in the multiplayer INI checksum, so a stale copy costs a
screen without the column, which is why Diplomacy.cpp null checks every one of these windows.

    python diplomacy_layout.py selfcheck

reads the tracked file back and checks it still agrees with the code: every row carries its four
controls, and every string the game fetches for them is in Patch.str.  Run by CTest as
diplomacy_selfcheck.
"""

import os
import sys

import wndlayout
from wndlayout import clone


HERE = os.path.dirname(os.path.abspath(__file__))
WINDOW = os.path.join(HERE, "..", "Data", "Window")
PATCH_STR = os.path.join(HERE, "..", "Data", "Patch.str")

ROWS = 8
WIDEN = 170
ROW_TOP, ROW_STEP, ROW_HEIGHT = 94, 32, 24

# left edge and width of each control in a row, in the layout's 800x600 creation resolution
MINUS = (566, 24)
AMOUNT = (594, 72)
PLUS = (670, 24)
SEND = (700, 60)
TITLE = (566, 58, 194, 30)

CONTROLS = ["ButtonMoneyMinus", "StaticTextMoney", "ButtonMoneyPlus", "ButtonMoneySend"]

# every string the money column asks TheGameText for
STRINGS = [
    "GUI:MoneyTransferTitle",
    "GUI:MoneyTransferSend",
    "GUI:MoneyTransferMinus",
    "GUI:MoneyTransferPlus",
    "GUI:MoneyTransferAmount",
    "GUI:MoneyTransferSent",
    "GUI:MoneyTransferReceived",
    "TOOLTIP:MoneyTransfer",
]


def widen(window, dx):
    l, t, r, b, cw, ch = window.rect
    window.rect = (l, t, r + dx, b, cw, ch)


def small_button(template, name, left, top, width, text):
    button = clone(template, "Diplomacy.wnd:%s" % name)
    button.place(left, top, width, ROW_HEIGHT)
    # MainButton's header template carries the Generals font at 15, too big for a row
    button.set_prop("HEADERTEMPLATE", '"[NONE]"')
    button.set_prop("FONT", 'NAME: "Arial", SIZE: 10, BOLD: 1')
    button.set_prop("TEXT", '"%s"' % text)
    button.put_prop("TOOLTIPTEXT", '"TOOLTIP:MoneyTransfer"')
    return button


def build(layout):
    for name in ("Parent", "InGameParent", "SoloParent", "ListboxSolo"):
        widen(layout.find(name), WIDEN)
    layout.find("ButtonHide").move_by(WIDEN, 0)

    in_game = layout.find("InGameParent")
    hide = layout.find("ButtonHide")
    status = layout.find("StaticTextStatus0")
    status_title = layout.find("StaticTextStatusTitle")

    title = clone(status_title, "Diplomacy.wnd:StaticTextMoneyTitle")
    title.place(*TITLE)
    title.set_prop("TEXT", '"GUI:MoneyTransferTitle"')
    in_game.children.append(title)

    for row in range(ROWS):
        top = ROW_TOP + row * ROW_STEP
        in_game.children.append(small_button(hide, "ButtonMoneyMinus%d" % row, MINUS[0], top, MINUS[1],
                                             "GUI:MoneyTransferMinus"))

        amount = clone(status, "Diplomacy.wnd:StaticTextMoney%d" % row)
        amount.place(AMOUNT[0], top, AMOUNT[1], ROW_HEIGHT)
        in_game.children.append(amount)

        in_game.children.append(small_button(hide, "ButtonMoneyPlus%d" % row, PLUS[0], top, PLUS[1],
                                             "GUI:MoneyTransferPlus"))
        in_game.children.append(small_button(hide, "ButtonMoneySend%d" % row, SEND[0], top, SEND[1],
                                             "GUI:MoneyTransferSend"))
    return layout


def read_strings():
    keys = set()
    with open(PATCH_STR, "r", encoding="utf-8") as fp:
        for line in fp:
            line = line.strip()
            if line and not line.startswith("//") and not line.startswith('"') and line != "END":
                keys.add(line)
    return keys


def selfcheck():
    problems = []

    keys = read_strings()
    for key in STRINGS:
        if key not in keys:
            problems.append("%s is not in Patch.str" % key)

    path = os.path.join(WINDOW, "Diplomacy.wnd")
    if not os.path.exists(path):
        problems.append("Diplomacy.wnd is not tracked under Data/Window")
    else:
        layout = wndlayout.load(path)
        names = ["StaticTextMoneyTitle"] + ["%s%d" % (c, r) for r in range(ROWS) for c in CONTROLS]
        for name in names:
            if layout.find(name) is None:
                problems.append("Diplomacy.wnd has no %s" % name)

    for problem in problems:
        print("  %s" % problem)
    if problems:
        print("%d problem(s)" % len(problems))
        return 1

    print("diplomacy: %d rows, %d strings" % (ROWS, len(STRINGS)))
    return 0


def main(argv):
    if len(argv) == 2 and argv[1] == "selfcheck":
        return selfcheck()

    if len(argv) != 3:
        print(__doc__)
        return 2

    indir, outdir = argv[1], argv[2]
    layout = build(wndlayout.load(os.path.join(indir, "Diplomacy.wnd")))
    out = os.path.join(outdir, "Diplomacy.wnd")
    wndlayout.save(layout, out)
    print("%s: %d windows" % (out, sum(1 for _ in layout.root.walk())))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
