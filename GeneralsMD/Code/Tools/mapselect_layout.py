#!/usr/bin/env python3
"""Lay the skirmish, LAN and online map pickers into their room, in place of the seat table.

EA drew the three pickers at 800x600 as a see-through strip over the top of the room. On the rooms'
1920x1080 grid (lobbyroom_layout.py) that strip landed over half the seat table and half the map
column at once. Here the picker swaps panels with the room instead: the map list takes the seat
table's rectangle, its preview takes the room's own map preview's rectangle, and Accept and Cancel
take the row the map's name and Select Map stand on. The room hides those while the picker is open
(the menus' ShowUnderlyingGUIElements lists), so nothing on the right moves or changes size; the
match rules page under it stays where it is.

The geometry is read from the tracked room layouts, so a room laid out again by lobbyroom_layout.py
only needs this script run after it. Every control keeps its name and EA's art; only rectangles
move, plus the panel's colours. The Official / Unofficial radio buttons are drawn as UIStyle tabs by
the menu code (DrawMapListTab in GUIUtil.cpp).

    python mapselect_layout.py <retail Menus dir> <Menus dir out>

reads SkirmishMapSelectMenu.wnd, LanMapSelectMenu.wnd and WOLMapSelectMenu.wnd as WindowZH.big
ships them. Running it on its own output changes nothing.

    python mapselect_layout.py selfcheck

checks the tracked pickers: every window the menu code names is there, the script reproduces the
file byte for byte, everything is authored at 1920x1080, the list stays inside the seat table, and
the preview and the two buttons sit exactly where the room's own preview and Select Map do.
Run by CTest as mapselect_selfcheck. Layouts are not in the multiplayer INI checksum.
"""

import os
import sys

import wndlayout
import lobbyroom_layout as room

HERE = os.path.dirname(os.path.abspath(__file__))
MENUS = os.path.join(HERE, "..", "Data", "Window", "Menus")

PICKERS = {
    "SkirmishMapSelectMenu": "SkirmishGameOptionsMenu",
    "LanMapSelectMenu": "LanGameOptionsMenu",
    "WOLMapSelectMenu": "GameSpyGameOptionsMenu",
}
NAMED = ["ButtonOK", "ButtonBack", "ListboxMap", "RadioButtonSystemMaps", "RadioButtonUserMaps",
         "WinMapPreview", "WinGadgetBorder"] + ["ButtonMapStartPosition%d" % seat for seat in range(room.SEAT_COUNT)]

PADDING = 12
TITLE_HEIGHT = 32
TAB_HEIGHT = 36


def box(window):
    """(left, top, width, height) of a window's rectangle."""
    left, top, right, bottom = window.rect[:4]
    return left, top, right - left, bottom - top


def room_geometry(room_layout):
    """The seat table, the map preview and the Select Map button, as the room lays them out."""
    last_card = room.need(room_layout, "SeatCard%d" % (room.SEAT_COUNT - 1))
    table_bottom = last_card.rect[3]
    table = (room.INNER_LEFT, room.HEADER_TOP, room.SEAT_RIGHT - room.INNER_LEFT, table_bottom - room.HEADER_TOP)
    return table, box(room.need(room_layout, "MapWindow")), box(room.need(room_layout, "ButtonSelectMap"))


def title_of(layout):
    """EA left the picker's heading unnamed; it is the one unnamed window."""
    unnamed = [child for child in layout.root.children if not room.short(child)]
    if len(unnamed) != 1:
        raise ValueError("expected one unnamed heading in %s, found %d" % (room.short(layout.root), len(unnamed)))
    return unnamed[0]


def build(layout, room_layout):
    table, preview, select = room_geometry(room_layout)
    left, top, width, height = table
    select_left, select_top, select_width, select_height = select
    cancel_left = room.COLUMN_LEFT

    # every window is placed below; this only moves them onto the 1920x1080 grid first
    sx, sy = room.WIDE[0] / room.OLD[0], room.WIDE[1] / room.OLD[1]
    for node in layout.root.walk():
        l, t, r, b, cw, ch = node.rect
        if (cw, ch) == room.OLD:
            node.rect = (round(l * sx), round(t * sy), round(r * sx), round(b * sy)) + room.WIDE

    # the parent spans the seat table and the map column down to the buttons; it stays see-through
    root = layout.root
    root.place(left, top, room.INNER_RIGHT - left, max(top + height, select_top + select_height) - top)

    border = room.need(layout, "WinGadgetBorder")
    border.place(*table)
    border.set_prop("ENABLEDDRAWDATA", room.draw_data(room.CARD, room.FRAME_COLOR))

    inner_left, inner_width = left + PADDING, width - 2 * PADDING
    title = title_of(layout)
    title.place(inner_left - room.TEXT_NUDGE, top + PADDING, inner_width // 2, TITLE_HEIGHT)
    room.left_align(title)
    # a heading inside a panel is ACTIVE, as the record's is
    title.set_prop("TEXTCOLOR", "ENABLED: %d %d %d %d, ENABLEDBORDER: 0 0 0 255, DISABLED: 192 192 192 255, "
                   "DISABLEDBORDER: 64 64 64 255, HILITE: 128 128 255 255, HILITEBORDER: 0 0 128 255" % room.ACTIVE)

    tab_top = top + PADDING + TITLE_HEIGHT + room.GAP
    tab_width = (inner_width - room.GAP) // 2
    room.need(layout, "RadioButtonSystemMaps").place(inner_left, tab_top, tab_width, TAB_HEIGHT)
    room.need(layout, "RadioButtonUserMaps").place(inner_left + inner_width - tab_width, tab_top, tab_width, TAB_HEIGHT)

    list_top = tab_top + TAB_HEIGHT + room.GAP
    room.need(layout, "ListboxMap").place(inner_left, list_top, inner_width, top + height - PADDING - list_top)

    map_window = room.need(layout, "WinMapPreview")
    map_window.place(*preview)
    # the menu code places the start spots on the map; only their size is the layout's
    for spot in map_window.children:
        spot.place(preview[0], preview[1], room.SPOT, room.SPOT)

    room.need(layout, "ButtonOK").place(*select)
    room.need(layout, "ButtonBack").place(cancel_left, select_top, select_width, select_height)
    if cancel_left + select_width + room.GAP > select_left:
        raise ValueError("Cancel at %d does not fit beside Accept at %d" % (cancel_left, select_left))
    return layout


def load_room(menu):
    return wndlayout.load(os.path.join(MENUS, PICKERS[menu] + ".wnd"))


# ---------------------------------------------------------------------------
# selfcheck
# ---------------------------------------------------------------------------

def inside(inner, outer):
    left, top, width, height = inner
    o_left, o_top, o_width, o_height = outer
    return left >= o_left and top >= o_top and left + width <= o_left + o_width and top + height <= o_top + o_height


def selfcheck():
    problems = []
    for menu in sorted(PICKERS):
        path = os.path.join(MENUS, menu + ".wnd")
        if not os.path.exists(path):
            problems.append("%s.wnd is not tracked" % menu)
            continue
        with open(path, "rb") as fp:
            tracked = fp.read().decode("latin-1")
        layout = wndlayout.parse(tracked)
        missing = [name for name in NAMED if layout.find(name) is None]
        problems.extend("%s.wnd has no %s" % (menu, name) for name in missing)
        stale = [room.short(node) or "(unnamed)" for node in layout.root.walk() if tuple(node.rect[4:]) != room.WIDE]
        if stale:
            problems.append("%s.wnd: %d windows not authored at 1920x1080, %s first" % (menu, len(stale), stale[0]))
        try:
            rebuilt = build(wndlayout.parse(tracked), load_room(menu)).text()
            if rebuilt != tracked:
                problems.append("%s.wnd is not what mapselect_layout.py writes from %s.wnd; regenerate it"
                                % (menu, PICKERS[menu]))
        except (KeyError, ValueError) as error:
            problems.append("%s.wnd no longer builds: %s" % (menu, error))
        if missing:
            continue
        table, preview, select = room_geometry(load_room(menu))
        for name in ("ListboxMap", "RadioButtonSystemMaps", "RadioButtonUserMaps"):
            if not inside(box(layout.find(name)), table):
                problems.append("%s.wnd: %s runs out of the seat table" % (menu, name))
        if box(layout.find("WinMapPreview")) != preview:
            problems.append("%s.wnd: the preview is not where %s.wnd draws its map" % (menu, PICKERS[menu]))
        if box(layout.find("ButtonOK")) != select:
            problems.append("%s.wnd: Accept is not where %s.wnd has Select Map" % (menu, PICKERS[menu]))
        for child in layout.root.children:
            if not inside(box(child), box(layout.root)):
                problems.append("%s.wnd: %s lies outside the picker" % (menu, room.short(child) or "(unnamed)"))

    for problem in problems:
        print("mapselect: %s" % problem)
    if problems:
        return 1
    print("mapselect: %d pickers in their rooms' seat tables" % len(PICKERS))
    return 0


def main(argv):
    if len(argv) == 2 and argv[1] == "selfcheck":
        return selfcheck()
    if len(argv) != 3:
        print(__doc__)
        return 2
    source, target = argv[1], argv[2]
    for menu in sorted(PICKERS):
        layout = wndlayout.load(os.path.join(source, menu + ".wnd"))
        out = os.path.join(target, menu + ".wnd")
        wndlayout.save(build(layout, load_room(menu)), out)
        print("%s: %d windows" % (out, sum(1 for _ in layout.root.walk())))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
