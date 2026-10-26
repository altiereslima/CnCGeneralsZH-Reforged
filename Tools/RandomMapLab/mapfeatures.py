#!/usr/bin/env python3
"""The large features of a shipped map, as an inventory the random map generator can rebuild in a
new layout: its levels and high ground, its ways down, canyons, mesas, rivers and lakes, bridges,
roads and towns, each with its size and where it sits against the starts.

mapstyle.py measures a map's character cell by cell; this one finds the things in it. synthmap.py
reads the inventory to reshape a skeleton before it quilts the detail in.

    python mapfeatures.py <file.map> [-o inventory.json]
    python mapfeatures.py <archive.big> "maps\\Silent River\\Silent River.map"
    python mapfeatures.py scan <archive.big> [...]
    python mapfeatures.py selfcheck

Schema (heights are height map bytes; "dz_start" is against the median start, "dz_middle" against
the median of the middle quarter, as mapstyle measures it; cells are map cells; "centre_share" is
the distance from the map's middle over half the map, 0 the middle, 1 the edge):

    source, size_cells [x, y], players
    levels       start_bytes, middle_bytes, peaks [{dz_start, share}] (height bands holding 3% or more)
    high_ground  {dz_start, share} of ground 40 or more above the starts, or null
    base         mapstyle's ramp_down_width_cells, ramps_per_base, base_plateau_radius_cells,
                 start_raise_bytes
    canyons      [{floor_dz_middle, width, length, winding, wall_cliff_share, centre_share,
                   reaches_edge}]  dry ground 25 below the middle, long and thin
    mesas        [{area, radius, top_dz_middle, rim_cliff_share, exits, centre_share,
                   nearest_start}]  ground between the middle and the starts that no base touches
    water        [{kind river|lake|sea|coast, river_flag, spans_map, area, length, width, winding,
                   depth_bytes, bank_bytes, centre_share, nearest_start, reaches_edge,
                   surface_bytes, start_clear, edges, share, joins}]  kind by the shape of the wet
                   cells (sea on every edge, an island map; a coast along 40% of an edge or more;
                   a river four times longer than wide; a sea 30% of the map); river_flag
                   is WorldBuilder's, which is unreliable; spans_map a polygon over 90% of the map
                   both ways; start_clear the nearest start's distance to it over the least
                   distance between two starts; edges the wet share of each playable edge (low y,
                   high x, high y, low x); share of the playable cells; joins the index of the
                   longer river a river runs into, its junction
    bridges      [{template, length, over water|gap|ground, ends_dz_middle [a, b], centre_share,
                   on_mesa, water}]  a FLAG_BRIDGE_POINT1 object and the FLAG_BRIDGE_POINT2 one
                   after it; water the index of the water it crosses
    roads        [{template, segments, length, outside_segments, outside_length, segment_length,
                   flags [first, second]}]  FLAG_ROAD_POINT1/2 pairs, by template; outside is away
                   from the towns; flags the end flags (corner types) most of its pairs carry
    towns        [{buildings, templates {name: n}, radius, centre_share, nearest_start, box}]
                 clusters of five or more civilian buildings (civilianbuilding.ini), 20 cells apart
    tech         [{template, centre_share, nearest_start, on_mesa}]  tech buildings but derricks
    piles        {count, dz_start, company {template: per pile}}  what stands within 10 cells of a pile
    starts_raised  mapstyle's call: whether the bases stand on plateaus above the middle
    split_without_bridges  whether a start reaches another only over a bridge
    road_exits   how many places roads leave the map at
"""

import json
import math
import os
import re
import sys
from collections import Counter

import numpy as np
from scipy import ndimage

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "..", "GeneralsMD", "Code", "Tools"))

import bigfile                                       # noqa: E402
import mapstyle                                      # noqa: E402
from mapscore import ChunkReader, MAP_XY_FACTOR, MAP_HEIGHT_SCALE  # noqa: E402

RUN = os.path.join(HERE, "..", "..", "GeneralsMD", "Run")
FLAG_ROAD_POINT1, FLAG_ROAD_POINT2 = 0x02, 0x04         # MapObject.h
FLAG_BRIDGE_POINT1, FLAG_BRIDGE_POINT2 = 0x10, 0x20
CANYON_DROP = 25            # bytes below the middle a canyon's floor lies
CANYON_CELLS = 400          # the least floor a canyon has
LONG = 3.0                  # length over width that makes a piece of ground a channel
MESA_RISE = 25              # bytes above the middle and below the starts a mesa's top keeps
MESA_CELLS = 300
MESA_CLIFF = 0.3            # of a mesa's rim that is cliff, at least
HIGH_RISE = 40
TOWN_GAP = 20               # cells between two buildings of one town
TOWN_BUILDINGS = 5
SEA_SHARE = 0.9             # of the map a water polygon spans both ways to be laid over all of it
SEA_WET = 0.3               # of the playable cells wet that make a sea rather than a lake
JOIN_CELLS = 3              # cells between two rivers' water that make one the other's branch
COAST_EDGE = 0.4            # of a playable edge wet that makes water the sea's coast
FULL_EDGE = 0.9             # of every playable edge wet: the sea all round, an island map
EXIT_CELLS = 4              # cells off the playable edge a road's end leaves the map at


def objects_in_order(data):
    """Every object as (template, x cells, y cells, flags), in file order: a bridge or a road is two
    objects one after the other."""
    reader = ChunkReader(mapstyle.unpack_map(data))
    out = []
    for name, version, end in reader.chunks():
        if name != "ObjectsList":
            continue
        for sub, sub_version, sub_end in reader.chunks(end):
            if sub != "Object":
                continue
            x = reader.read_real() / MAP_XY_FACTOR
            y = reader.read_real() / MAP_XY_FACTOR
            reader.read_real()
            reader.read_real()
            flags = reader.read_int()
            out.append((reader.read_ascii(), x, y, flags))
    return out


def river_flags(data):
    """PolygonTriggers' water areas in order, each True when WorldBuilder marked it a river."""
    reader = ChunkReader(mapstyle.unpack_map(data))
    out = []
    for name, version, end in reader.chunks():
        if name != "PolygonTriggers":
            continue
        for _ in range(reader.read_int()):
            reader.read_ascii()
            if version >= 4:
                reader.read_ascii()
            reader.read_int()
            water = reader.read_byte() != 0 if version >= 2 else False
            river = False
            if version >= 3:
                river = reader.read_byte() != 0
                reader.read_int()
            points = reader.read_int()
            reader.pos += 12 * points
            if water and points > 2:
                out.append(river)
    return out


_civilian = set()


def civilian_buildings():
    if not _civilian:
        for big in (os.path.join(RUN, "INIZH.big"), os.path.join(RUN, "ZH_Generals", "INI.big")):
            if not os.path.exists(big):
                continue
            with open(big, "rb") as handle:
                for stored, offset, size in list(bigfile._read_index(handle)):
                    if stored.lower().replace("\\", "/").endswith("object/civilianbuilding.ini"):
                        handle.seek(offset)
                        text = handle.read(size).decode("latin-1")
                        for name, body in re.findall(r"^Object\s+(\S+)(.*?)(?=^Object\s|\Z)", text, re.M | re.S):
                            kinds = re.search(r"KindOf\s*=\s*(.*)", body)
                            if kinds and "STRUCTURE" in kinds.group(1) and "BRIDGE" not in kinds.group(1) \
                                    and not re.match(r"(supply|tech)", name.lower()):
                                _civilian.add(name.lower())
    return _civilian


def shape(cells):
    """Length along the main axis, mean width, and how far the cells wander off a straight line:
    the principal axis of the cells, the spread along it, the area over that length."""
    ys, xs = cells
    pts = np.stack([xs, ys], 1).astype(np.float64)
    mean = pts.mean(0)
    _, vectors = np.linalg.eigh(np.cov((pts - mean).T))
    axis = vectors[:, -1]
    along = (pts - mean) @ axis
    across = (pts - mean) @ np.array([-axis[1], axis[0]])
    length = float(np.ptp(along)) + 1.0
    # the centre line: the mean across offset in each tenth of the length
    bins = np.clip(((along - along.min()) / length * 10).astype(int), 0, 9)
    centre = np.array([across[bins == b].mean() for b in range(10) if (bins == b).any()])
    return length, len(xs) / length, float(np.std(centre)), mean


def inventory(data, source=""):
    m = mapstyle.StyleMap(data)
    grid = m.passable_grid()
    water = np.frombuffer(bytes(m.water), np.uint8).reshape(m.height, m.width).astype(bool)
    h = np.frombuffer(bytes(m.heights), np.uint8).reshape(m.height, m.width).astype(np.int32)
    b = m.border
    play = np.zeros(h.shape, bool)
    play[b:b + m.playable_y, b:b + m.playable_x] = True
    passable = np.frombuffer(bytes(grid), np.uint8).reshape(m.height, m.width).astype(bool)
    cliff = np.zeros(h.shape, bool)
    for y in range(m.height - 1):
        row = [m.cliff_at(x, y) for x in range(m.width - 1)]
        cliff[y, :-1] = row
    yy, xx = np.mgrid[0:m.height, 0:m.width]
    cy, cx = b + m.playable_y / 2.0, b + m.playable_x / 2.0
    half = m.playable / 2.0
    starts = [(int(y + 0.5) + b, int(x + 0.5) + b) for x, y in m.starts]
    start_h = float(np.median([np.median(h[y - 3:y + 4, x - 3:x + 4]) for y, x in starts]))
    middle = play & (np.hypot(xx + 0.5 - cx, yy + 0.5 - cy) <= 0.25 * m.playable)
    middle_h = float(np.median(h[middle]))
    dry = play & ~water

    def where(ys, xs):
        y, x = float(np.mean(ys)), float(np.mean(xs))
        near = min(math.hypot(y - sy, x - sx) for sy, sx in starts)
        return round(math.hypot(y - cy, x - cx) / half, 2), round(near, 1)

    def edge(ys, xs):
        return bool(ys.min() <= b + 2 or xs.min() <= b + 2 or ys.max() >= b + m.playable_y - 3
                    or xs.max() >= b + m.playable_x - 3)

    # levels
    dz = (h[dry] - start_h).astype(int)
    bands = Counter((dz // 8) * 8)
    peaks = [{"dz_start": int(k), "share": round(v / float(dz.size), 3)}
             for k, v in sorted(bands.items()) if v >= 0.03 * dz.size]
    high = dry & (h >= start_h + HIGH_RISE)
    high_ground = ({"dz_start": float(np.median(h[high]) - start_h), "share": round(high.sum() / float(dry.sum()), 3)}
                   if high.sum() > 0.02 * dry.sum() else None)

    profile = mapstyle.profile(data, source)
    g = profile["generator"]
    base = {k: g[k] for k in ("ramp_down_width_cells", "ramps_per_base", "base_plateau_radius_cells",
                              "start_raise_bytes")}

    # canyons: dry floor well below the middle, long and thin
    canyons = []
    labels, count = ndimage.label(dry & (h < middle_h - CANYON_DROP))
    for i in range(1, count + 1):
        cells = np.nonzero(labels == i)
        if len(cells[0]) < CANYON_CELLS:
            continue
        length, width, winding, _ = shape(cells)
        if length < LONG * width:
            continue
        piece = labels == i
        wall = ndimage.binary_dilation(piece, iterations=4) & ~piece & play
        cs, near = where(*cells)
        canyons.append({"floor_dz_middle": float(np.median(h[piece]) - middle_h),
                        "width": round(width, 1), "length": round(length, 1),
                        "winding": round(winding, 1),
                        "wall_cliff_share": round(float(cliff[wall].mean()), 3),
                        "centre_share": cs, "reaches_edge": edge(*cells)})

    # mesas: ground between the middle and the starts that no base plateau touches
    plateau = dry & (np.abs(h - start_h) <= 6)
    band = dry & (h > middle_h + MESA_RISE) & (h < start_h - MESA_RISE)
    labels, count = ndimage.label(band)
    touched = set(np.unique(labels[ndimage.binary_dilation(plateau, iterations=2) & band]))
    mesas = []
    for i in range(1, count + 1):
        piece = labels == i
        if i in touched or piece.sum() < MESA_CELLS:
            continue
        cells = np.nonzero(piece)
        rim = ndimage.binary_dilation(piece, iterations=2) & ~piece & play
        if cliff[rim].mean() < MESA_CLIFF:             # a hump in the middle ground, not a mesa
            continue
        ways = rim & passable & ~cliff
        _, exits = ndimage.label(ways)
        cs, near = where(*cells)
        mesas.append({"area": int(piece.sum()), "radius": round(math.sqrt(piece.sum() / math.pi), 1),
                      "top_dz_middle": float(np.median(h[piece]) - middle_h),
                      "rim_cliff_share": round(float(cliff[rim].mean()), 3), "exits": int(exits),
                      "centre_share": cs, "nearest_start": near, "_mask": piece})

    # water, area by area; how near it comes to a start is in start spacings, so a map of another
    # size or seat count can keep it as far off
    spacing = min([math.hypot(p[0] - q[0], p[1] - q[1]) for i, p in enumerate(starts) for q in starts[i + 1:]]
                  or [float(m.playable)])
    flags = river_flags(data)
    bodies = []
    wet_masks = []
    areas = m.water_areas
    for index, area in enumerate(areas):
        m.water_areas = [area]
        wet = np.frombuffer(bytes(m.water_grid()), np.uint8).reshape(m.height, m.width).astype(bool) & play
        if wet.sum() < 50:
            continue
        cells = np.nonzero(wet)
        length, width, winding, _ = shape(cells)
        surface = area[1] / MAP_HEIGHT_SCALE
        bank = ndimage.binary_dilation(wet, iterations=6) & ~wet & play
        # by the shape of what is wet: WorldBuilder's river flag is missing on real rivers and set
        # on shallow streams, and a polygon over the whole map often floods only a river's bed
        xs_, ys_ = [p[0] for p in area[0]], [p[1] for p in area[0]]
        spans = bool(np.ptp(xs_) >= SEA_SHARE * m.playable_x and np.ptp(ys_) >= SEA_SHARE * m.playable_y)
        sides = [wet[b, b:b + m.playable_x].mean(), wet[b:b + m.playable_y, b + m.playable_x - 1].mean(),
                 wet[b + m.playable_y - 1, b:b + m.playable_x].mean(), wet[b:b + m.playable_y, b].mean()]
        kind = ("sea" if min(sides) >= FULL_EDGE else
                "coast" if max(sides) >= COAST_EDGE else
                "river" if length >= 4.0 * width else
                "sea" if wet.sum() >= SEA_WET * play.sum() else "lake")
        cs, near = where(*cells)
        gap = ndimage.distance_transform_edt(~wet)
        bodies.append({"kind": kind, "river_flag": bool(flags[index]) if index < len(flags) else False,
                       "start_clear": round(min(float(gap[p]) for p in starts) / spacing, 3),
                       "spans_map": spans, "area": int(wet.sum()),
                       "length": round(length, 1), "width": round(width, 1),
                       "winding": round(winding, 1),
                       "depth_bytes": round(surface - float(np.median(h[wet])), 1),
                       "bank_bytes": round(float(np.median(h[bank])) - surface, 1) if bank.any() else 0.0,
                       "surface_bytes": round(surface, 1), "centre_share": cs, "nearest_start": near,
                       "reaches_edge": edge(*cells),
                       "edges": [round(float(side), 2) for side in sides],
                       "share": round(float(wet.sum()) / play.sum(), 3)})
        wet_masks.append(wet)
    m.water_areas = areas
    # a river that runs into a longer one is its branch: the junction is where their water meets
    for index, body in enumerate(bodies):
        body["joins"] = None
        if body["kind"] != "river":
            continue
        grown = ndimage.binary_dilation(wet_masks[index], iterations=JOIN_CELLS)
        longer = [j for j, other in enumerate(bodies) if other["kind"] == "river" and j != index and
                  (other["length"], -j) > (body["length"], -index) and (grown & wet_masks[j]).any()]
        if longer:
            body["joins"] = max(longer, key=lambda j: bodies[j]["length"])

    # bridges and roads, as the engine pairs them (W3DBridgeBuffer::loadBridges)
    objects = objects_in_order(data)
    bridges, roads, segments = [], {}, []
    for first, second in zip(objects, objects[1:]):
        (template, x1, y1, f1), (_, x2, y2, f2) = first, second
        a = (int(y1) + b, int(x1) + b)
        z = (int(y2) + b, int(x2) + b)
        length = math.hypot(x2 - x1, y2 - y1)
        if f1 & FLAG_BRIDGE_POINT1 and f2 & FLAG_BRIDGE_POINT2:
            steps = max(int(length), 1)
            line = [(int(a[0] + (z[0] - a[0]) * s / steps), int(a[1] + (z[1] - a[1]) * s / steps))
                    for s in range(steps + 1)]
            under = min(h[p] for p in line)
            over = ("water" if any(water[p] for p in line) else
                    "gap" if under < min(h[a], h[z]) - 15 else "ground")
            on = [i for i, mesa in enumerate(mesas)
                  if ndimage.binary_dilation(mesa["_mask"], iterations=4)[a] or
                  ndimage.binary_dilation(mesa["_mask"], iterations=4)[z]]
            cs, _ = where(np.array([a[0], z[0]]), np.array([a[1], z[1]]))
            crossed = [sum(1 for p in line if mask[p]) for mask in wet_masks]
            bridges.append({"template": template, "length": round(length, 1), "over": over,
                            "ends_dz_middle": [float(h[a] - middle_h), float(h[z] - middle_h)],
                            "centre_share": cs, "on_mesa": on[0] if on else None,
                            "water": int(np.argmax(crossed)) if over == "water" else None})
        elif f1 & FLAG_ROAD_POINT1 and f2 & FLAG_ROAD_POINT2:
            segments.append((template, x1 + b, y1 + b, x2 + b, y2 + b, f1, f2))
            entry = roads.setdefault(template, {"template": template, "segments": 0, "length": 0.0})
            entry["segments"] += 1
            entry["length"] = round(entry["length"] + length, 1)
    masks = [ndimage.binary_dilation(mesa.pop("_mask"), iterations=4) for mesa in mesas]
    for index, mesa in enumerate(mesas):
        mesa["bridges"] = sum(1 for bridge in bridges if bridge["on_mesa"] == index)

    # towns: civilian buildings no more than TOWN_GAP apart
    civil = civilian_buildings()
    houses = [(t, x, y) for t, x, y, f in objects if t.lower() in civil and f == 0]
    towns = []
    if houses:
        marks = np.zeros(h.shape, bool)
        for _, x, y in houses:
            marks[int(y) + b, int(x) + b] = True
        labels, count = ndimage.label(ndimage.binary_dilation(marks, iterations=TOWN_GAP // 2))
        for i in range(1, count + 1):
            members = [(t, x, y) for t, x, y in houses if labels[int(y) + b, int(x) + b] == i]
            if len(members) < TOWN_BUILDINGS:
                continue
            xs = np.array([x for _, x, _ in members]) + b
            ys = np.array([y for _, _, y in members]) + b
            cs, near = where(ys, xs)
            radius = float(np.max(np.hypot(xs - xs.mean(), ys - ys.mean())))
            towns.append({"buildings": len(members), "templates": dict(Counter(t for t, _, _ in members).most_common()),
                          "radius": round(radius, 1), "centre_share": cs, "nearest_start": near,
                          "box": [float(xs.min() - b), float(ys.min() - b), float(xs.max() - b), float(ys.max() - b)]})

    # the roads outside the towns, the network a generator lays between the places: their length
    # and segment count, the segment length, and the end flags (corner type) a segment mostly has
    def in_town(x, y):
        return any(t["box"][0] - TOWN_GAP <= x - b <= t["box"][2] + TOWN_GAP and
                   t["box"][1] - TOWN_GAP <= y - b <= t["box"][3] + TOWN_GAP for t in towns)

    # where roads leave the map: road ends at the playable edge, those within 20 cells one exit
    ends = [(x, y) for s in segments for x, y in ((s[1], s[2]), (s[3], s[4]))
            if min(x - b, y - b, b + m.playable_x - 1 - x, b + m.playable_y - 1 - y) <= EXIT_CELLS]
    exits = []
    for x, y in ends:
        if all(math.hypot(x - ex, y - ey) > TOWN_GAP for ex, ey in exits):
            exits.append((x, y))
    for entry in roads.values():
        outside = [s for s in segments if s[0] == entry["template"] and not in_town((s[1] + s[3]) / 2, (s[2] + s[4]) / 2)]
        entry["outside_segments"] = len(outside)
        entry["outside_length"] = round(sum(math.hypot(s[3] - s[1], s[4] - s[2]) for s in outside), 1)
        entry["segment_length"] = round(entry["length"] / entry["segments"], 1)
        entry["flags"] = list(Counter((s[5], s[6]) for s in segments if s[0] == entry["template"]).most_common(1)[0][0])

    # the tech buildings but the derricks, which are money, and what stands with the small piles
    tech = []
    for t, x, y, f in objects:
        if t.lower().startswith("tech") and t != "TechOilDerrick" and f == 0:
            cell = (int(y) + b, int(x) + b)
            cs, near = where(np.array([cell[0]]), np.array([cell[1]]))
            tech.append({"template": t, "centre_share": cs, "nearest_start": near,
                         "on_mesa": next((i for i, mesa in enumerate(masks) if mesa[cell]), None)})
    company = Counter()
    for px, py in m.piles:
        company.update(t for t, x, y in houses if math.hypot(x - px, y - py) <= 10)
    piles = {"count": len(m.piles),
             "dz_start": float(np.median([h[int(y) + b, int(x) + b] - start_h for x, y in m.piles])) if m.piles else None,
             "company": {t: round(n / float(max(len(m.piles), 1)), 2) for t, n in company.most_common()}}

    # whether some start reaches another only over a bridge (the grid has none of them)
    ground, _ = ndimage.label(passable)
    split = len(set(int(ground[p]) for p in starts)) > 1

    return {"source": source, "size_cells": [m.playable_x, m.playable_y], "players": len(starts),
            "split_without_bridges": split, "road_exits": len(exits),
            "levels": {"start_bytes": start_h, "middle_bytes": middle_h, "peaks": peaks},
            "high_ground": high_ground, "base": base, "canyons": canyons, "mesas": mesas,
            "water": bodies, "bridges": bridges, "roads": list(roads.values()), "towns": towns,
            "tech": tech, "piles": piles, "starts_raised": profile["generator"]["starts_raised"]}


def summary(inv):
    return ("%-28s %dp %3dx%-3d high %-5s canyons %d mesas %d rivers %d lakes %d bridges %d (%s) "
            "roads %d towns %s" % (
                inv["source"].split("\\")[-1][:28], inv["players"], inv["size_cells"][0], inv["size_cells"][1],
                "%+d" % inv["high_ground"]["dz_start"] if inv["high_ground"] else "-",
                len(inv["canyons"]), len(inv["mesas"]),
                sum(1 for w in inv["water"] if w["kind"] == "river"),
                sum(1 for w in inv["water"] if w["kind"] == "lake"), len(inv["bridges"]),
                ",".join(sorted(set(x["over"] for x in inv["bridges"]))),
                sum(r["segments"] for r in inv["roads"]),
                [t["buildings"] for t in inv["towns"]]))


def selfcheck():
    ys, xs = np.nonzero(np.pad(np.ones((4, 40), bool), 5))
    length, width, winding, _ = shape((ys, xs))
    assert abs(length - 40) < 1e-6 and abs(width - 4) < 1e-6 and winding < 1e-6
    print("mapfeatures selfcheck OK")


def main():
    if sys.argv[1:] == ["selfcheck"]:
        selfcheck()
        return 0
    if sys.argv[1:2] == ["scan"]:
        for big in sys.argv[2:]:
            with open(big, "rb") as handle:
                names = [n for n, _, _ in bigfile._read_index(handle) if n.lower().endswith(".map")]
            for name in names:
                data = mapstyle.load_bytes(big, name)
                try:
                    print(summary(inventory(data, name)))
                except (ValueError, IndexError, ZeroDivisionError) as error:
                    print("%-28s not a skirmish map (%s)" % (name.split("\\")[-1][:28], error))
        return 0
    path = sys.argv[1]
    inner = sys.argv[2] if len(sys.argv) > 2 and not sys.argv[2].startswith("-") else None
    result = inventory(mapstyle.load_bytes(path, inner), inner or os.path.basename(path))
    text = json.dumps(result, indent=1)
    if "-o" in sys.argv:
        with open(sys.argv[sys.argv.index("-o") + 1], "w") as handle:
            handle.write(text + "\n")
    else:
        print(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
