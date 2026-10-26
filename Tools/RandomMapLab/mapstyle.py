#!/usr/bin/env python3
"""Read a shipped .map and write down its structure, so the random map generator can be asked for
a map in the same style.

What comes out is one JSON document: the size and the seats, how each start sits (how high above
the rest of the map, how much flat ground round it, how many ways down off that flat and which way
they point), where the money is relative to the bases, the water, how much of the map is cliff, how
rough the middle is, the textures folded into the generator's four classes, the scenery that stands
on it and the light. The last block, "generator", restates the numbers in the generator's own terms
(RandomMapGenerator.cpp: RMG_FLAT_RADIUS, RMG_HOME_SUPPLY_MIN/MAX, theMapTypes, theBiomes).

The shipped maps are refpack compressed (they start with "EAR\\0"); generated ones are raw CkMp.
Both are read.

    python mapstyle.py <file.map> [-o profile.json]
    python mapstyle.py <archive.big> "maps\\Twilight Flame\\Twilight Flame.map" [-o profile.json]
    python mapstyle.py <archive.big> "maps\\Twilight Flame\\Twilight Flame.map" --cpp twilight
    python mapstyle.py selfcheck

--cpp prints the entry for theStyles in RandomMapGenerator.cpp instead of the JSON.

Units: cells are map cells (10 world units), heights are height map bytes (MAP_HEIGHT_SCALE world
units each), the generator's own units for both. Bearings are degrees, 0 along +x, counterclockwise,
with +y the map's north (WorldBuilder's top).
"""

import argparse
import json
import math
import os
import statistics
import struct
import sys
from collections import Counter, deque

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "..", "GeneralsMD", "Code", "Tools"))

import bigfile                                       # noqa: E402
from mapscore import (ChunkReader, GeneratedMap, MAP_HEIGHT_SCALE, MAP_XY_FACTOR,  # noqa: E402
                      CLIFF_WORLD_SPAN)

# ponytail: one height band for "the same flat". A base pad that rolls by more than this reads as a
# smaller plateau; a slope gentler than this over the whole radius reads as one big one.
PLATEAU_TOLERANCE = 6                # height bytes either side of the start's own height
PLATEAU_MAX_RADIUS = 70              # cells; past this the flood stops and the edge is "open"
FLAT_PAD_BYTES = 2                   # the pad a base is built on, inside the plateau
RAMP_MIN_CELLS = 2                   # an exit narrower than this is a notch, not a way down
RAMP_PROBE = 10                      # cells past an exit where its drop is measured
RAMP_FOOT_CELLS = 16                 # money this close to the foot of a base's ramp is "ramp_foot"
HOME_SHARE = 0.30                    # of the closest start pair's walk: nearer than this is "home"
CONTESTED_SHARE = 0.15               # walks to the two nearest starts within this share: "mid"
SHORE_CELLS = 20                     # cells from water that count as its shore (a beach is wide)
DIRT_RISE = 12                       # height bytes above the grass a texture sits to be "dirt"
RAISED_BYTES = 8                     # a start this far above the middle of the map is raised

TIME_OF_DAY = {1: "morning", 2: "afternoon", 3: "evening", 4: "night"}


#-----------------------------------------------------------------------------------------------
# Refpack
#-----------------------------------------------------------------------------------------------

def refpack_decode(data):
    """REF_decode (Compression/EAC/refdecode.cpp), on a bare 10fb stream."""
    packtype = (data[0] << 8) | data[1]
    pos = 2
    size_bytes = 4 if packtype & 0x8000 else 3
    if packtype & 0x100:
        pos += size_bytes                            # the compressed size, unused
    ulen = int.from_bytes(data[pos:pos + size_bytes], "big")
    pos += size_bytes

    out = bytearray()

    def copy(offset, run):
        start = len(out) - offset
        if offset >= run:
            out.extend(out[start:start + run])
        else:                                       # overlapping: the copy reads what it writes
            for i in range(run):
                out.append(out[start + i])

    while True:
        first = data[pos]
        if not first & 0x80:                        # short form
            second = data[pos + 1]
            literal = first & 3
            out.extend(data[pos + 2:pos + 2 + literal])
            pos += 2 + literal
            copy(((first & 0x60) << 3) + second + 1, ((first & 0x1c) >> 2) + 3)
        elif not first & 0x40:                      # int form
            second, third = data[pos + 1], data[pos + 2]
            literal = second >> 6
            out.extend(data[pos + 3:pos + 3 + literal])
            pos += 3 + literal
            copy(((second & 0x3f) << 8) + third + 1, (first & 0x3f) + 4)
        elif not first & 0x20:                      # very int form
            second, third, fourth = data[pos + 1], data[pos + 2], data[pos + 3]
            literal = first & 3
            out.extend(data[pos + 4:pos + 4 + literal])
            pos += 4 + literal
            copy((((first & 0x10) >> 4) << 16) + (second << 8) + third + 1,
                 (((first & 0x0c) >> 2) << 8) + fourth + 5)
        else:
            run = ((first & 0x1f) << 2) + 4
            if run <= 112:                          # literal block
                out.extend(data[pos + 1:pos + 1 + run])
                pos += 1 + run
                continue
            literal = first & 3                     # end, with up to three literals
            out.extend(data[pos + 1:pos + 1 + literal])
            break

    if len(out) != ulen:
        raise ValueError("refpack stream gave %d bytes, header said %d" % (len(out), ulen))
    return bytes(out)


def refpack_encode_literal(data):
    """Literals only: no compression, but a valid stream. Enough for the self check."""
    out = bytearray(b"\x10\xfb" + len(data).to_bytes(3, "big"))
    pos = 0
    while len(data) - pos > 3:
        run = min(112, (len(data) - pos) & ~3)
        out.append(0xe0 | ((run - 4) >> 2))
        out.extend(data[pos:pos + run])
        pos += run
    out.append(0xfc | (len(data) - pos))
    out.extend(data[pos:])
    return bytes(out)


def unpack_map(data):
    """CompressionManager's EAR container: "EAR\\0", the raw size, then the refpack stream."""
    if data[:4] == b"EAR\0":
        raw = refpack_decode(data[8:])
        if len(raw) != struct.unpack_from("<i", data, 4)[0]:
            raise ValueError("EAR header size does not match the decoded map")
        return raw
    return data


def load_bytes(path, inner=None):
    if inner is None:
        with open(path, "rb") as handle:
            return handle.read()

    wanted = inner.lower().replace("/", "\\")
    with open(path, "rb") as handle:
        for stored, offset, size in list(bigfile._read_index(handle)):
            if stored.lower().replace("/", "\\") == wanted:
                handle.seek(offset)
                return handle.read(size)
    raise ValueError("%s has no member %s" % (path, inner))


#-----------------------------------------------------------------------------------------------
# The map, read the way the game reads a shipped one
#-----------------------------------------------------------------------------------------------

class StyleMap(GeneratedMap):
    """GeneratedMap's grid helpers (walk_distances, tightest_ring, cell_of) over a map read with
    every chunk version a shipped map uses, its painted cliff bits and its texture classes."""

    def __init__(self, data):
        self.width = self.height = self.border = self.playable = 0
        self.playable_x = self.playable_y = 0
        self.heights = []
        self.tiles = []
        self.cliff_bits = None
        self.texture_classes = []                   # (name, first tile, tile count)
        self.starts = []
        self.supplies = []                          # docks
        self.piles = []                             # the small one-off piles
        self.derricks = []
        self.water_areas = []                       # (points in cells, surface in world units)
        self.objects = []                           # (template, cell x, cell y)
        self.lighting = None
        self.world = {}

        reader = ChunkReader(unpack_map(data))
        for name, version, body_end in reader.chunks():
            if name == "HeightMapData":
                self._read_heights(reader, version)
            elif name == "BlendTileData":
                self._read_tiles(reader, version)
            elif name == "WorldInfo":
                self.world = reader.read_dict()
            elif name == "ObjectsList":
                self._read_objects(reader, body_end)
            elif name == "PolygonTriggers":
                self._read_triggers(reader, version)
            elif name == "GlobalLighting":
                self.lighting = self._read_lighting(reader, version)

        self.starts.sort()
        self.starts = [position for _, position in self.starts]
        self.playable = min(self.playable_x, self.playable_y)

    def _read_heights(self, reader, version):
        """WorldHeightMapData::ParseHeightMapData. Version 1 (half-size cells) is not handled."""
        self.width = reader.read_int()
        self.height = reader.read_int()
        self.border = reader.read_int() if version >= 3 else 0
        if version >= 4:
            boundaries = [(reader.read_int(), reader.read_int()) for _ in range(reader.read_int())]
        else:
            boundaries = [(self.width - 2 * self.border, self.height - 2 * self.border)]
        self.playable_x, self.playable_y = boundaries[0]

        size = reader.read_int()
        self.heights = reader.data[reader.pos:reader.pos + size]
        reader.pos += size

    def _read_tiles(self, reader, version):
        """parseBlendTileCells, then the texture classes of WorldHeightMap::ParseBlendTileData."""
        size = reader.read_int()
        self.tiles = struct.unpack_from("<%dh" % size, reader.data, reader.pos)
        reader.pos += size * 2                      # tiles
        reader.pos += size * 2                      # blends
        if version >= 6:
            reader.pos += size * 2                  # extra blends
        if version >= 5:
            reader.pos += size * 2                  # cliff info
        if version >= 7:
            row = (self.width + 1) // 8 if version == 7 else (self.width + 7) // 8
            bits = reader.data[reader.pos:reader.pos + row * self.height]
            reader.pos += row * self.height
            self.cliff_bits = (bits, row)

        reader.read_int()                           # bitmap tiles
        reader.read_int()                           # blended tiles
        if version >= 5:
            reader.read_int()                       # cliff infos
        for _ in range(reader.read_int()):
            first = reader.read_int()
            count = reader.read_int()
            reader.read_int()                       # width
            reader.read_int()                       # legacy
            self.texture_classes.append((reader.read_ascii(), first, count))

    def _read_objects(self, reader, end):
        for name, version, body_end in reader.chunks(end):
            if name != "Object":
                continue

            x = reader.read_real() / MAP_XY_FACTOR
            y = reader.read_real() / MAP_XY_FACTOR
            reader.read_real()                      # z
            reader.read_real()                      # angle
            reader.read_int()                       # flags
            template = reader.read_ascii()
            entries = reader.read_dict() if version >= 2 else {}

            waypoint = entries.get("waypointName", "")
            if "waypointID" in entries:
                if waypoint.startswith("Player_") and waypoint.endswith("_Start"):
                    self.starts.append((int(waypoint.split("_")[1]), (x, y)))
                continue

            self.objects.append((template, x, y))
            lower = template.lower()
            if "supplydock" in lower:
                self.supplies.append((x, y))
            elif "supplypile" in lower:
                self.piles.append((x, y))
            elif lower == "techoilderrick":
                self.derricks.append((x, y))

    def _read_triggers(self, reader, version):
        """PolygonTrigger::ParsePolygonTriggersDataChunk; only the water areas are kept."""
        for _ in range(reader.read_int()):
            reader.read_ascii()                     # name
            if version >= 4:
                reader.read_ascii()                 # layer
            reader.read_int()                       # id
            water = reader.read_byte() != 0 if version >= 2 else False
            if version >= 3:
                reader.read_byte()                  # river
                reader.read_int()                   # river start
            points = [(reader.read_int(), reader.read_int(), reader.read_int())
                      for _ in range(reader.read_int())]
            if water and len(points) > 2:
                self.water_areas.append(([(px / MAP_XY_FACTOR, py / MAP_XY_FACTOR)
                                          for px, py, _ in points],
                                         max(pz for _, _, pz in points)))

    def _read_lighting(self, reader, version):
        """WorldHeightMap::ParseLightingDataChunk: the current hour's main terrain and object
        lights. Versions 2 and 3 put two extra lights each after the main pair of every hour."""
        current = reader.read_int()
        extras = (2 if version >= 2 else 0) + (2 if version >= 3 else 0)
        hours = {}
        for hour in range(1, 5):
            terrain = [reader.read_real() for _ in range(9)]
            objects = [reader.read_real() for _ in range(9)]
            reader.pos += extras * 9 * 4
            hours[hour] = (terrain, objects)

        def light(values):
            return {"ambient": [round(v, 3) for v in values[0:3]],
                    "diffuse": [round(v, 3) for v in values[3:6]],
                    "direction": [round(v, 3) for v in values[6:9]]}

        terrain, objects = hours[current]
        return {"time_of_day": TIME_OF_DAY.get(current, current),
                "terrain": light(terrain), "objects": light(objects)}

    #-------------------------------------------------------------------------------------------
    # Grid
    #-------------------------------------------------------------------------------------------

    def cliff_at(self, x, y):
        if self.cliff_bits is not None:
            bits, row = self.cliff_bits
            return bool(bits[y * row + (x >> 3)] & (1 << (x & 7)))
        return self.cell_span_world(x, y) > CLIFF_WORLD_SPAN

    def water_grid(self):
        """Cells whose centre is inside a water area and whose ground is under its surface."""
        grid = bytearray(self.width * self.height)
        for points, surface in self.water_areas:
            ys = [p[1] for p in points]
            for cy in range(max(0, int(min(ys))), min(self.height - self.border, int(max(ys)) + 1)):
                py = cy + 0.5
                crossings = []
                for i in range(len(points)):
                    (x1, y1), (x2, y2) = points[i - 1], points[i]
                    if (y1 > py) != (y2 > py):
                        crossings.append(x1 + (py - y1) * (x2 - x1) / (y2 - y1))
                crossings.sort()
                for a, b in zip(crossings[0::2], crossings[1::2]):
                    for cx in range(max(0, int(math.ceil(a - 0.5))), int(math.floor(b - 0.5)) + 1):
                        gx, gy = cx + self.border, cy + self.border
                        if gx >= self.width or gy >= self.height:
                            continue
                        if self.height_at(gx, gy) * MAP_HEIGHT_SCALE < surface:
                            grid[gy * self.width + gx] = 1
        return grid

    def passable_grid(self):
        """Pathfinder::classifyMapCell's call per cell, then the pass after it that grows every
        cliff by one cell all round (pinched cells become cliff), which is what seals a painted
        cliff line one cell thin."""
        self.water = self.water_grid()
        cliff = bytearray(self.width * self.height)
        for y in range(self.height - 1):
            for x in range(self.width - 1):
                cliff[y * self.width + x] = self.cliff_at(x, y)

        grid = bytearray(self.width * self.height)
        for y in range(self.height - 1):
            for x in range(self.width - 1):
                index = y * self.width + x
                grown = any(cliff[ny * self.width + nx]
                            for ny in range(max(y - 1, 0), min(y + 2, self.height))
                            for nx in range(max(x - 1, 0), min(x + 2, self.width)))
                grid[index] = 0 if self.water[index] or grown else 1
        return grid

    def playable_cells(self):
        for y in range(self.border, self.border + self.playable_y):
            for x in range(self.border, self.border + self.playable_x):
                yield x, y


#-----------------------------------------------------------------------------------------------
# Measures
#-----------------------------------------------------------------------------------------------

def median_of(values):
    return round(statistics.median(values), 2) if values else None


def percentile(values, share):
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int(share * len(ordered)))]


def bearing(dx, dy):
    return round(math.degrees(math.atan2(dy, dx)) % 360.0, 1)


def angle_between(a, b):
    difference = abs(a - b) % 360.0
    return round(min(difference, 360.0 - difference), 1)


def plateau_of(m, grid, centre, start_height):
    """Flood of walkable cells within PLATEAU_TOLERANCE of the start's height."""
    cx, cy = centre
    inside = set()
    queue = deque([centre])
    inside.add(centre)
    open_edges = 0
    while queue:
        x, y = queue.popleft()
        for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
            if (nx, ny) in inside or not grid[ny * m.width + nx]:
                continue
            if abs(m.height_at(nx, ny) - start_height) > PLATEAU_TOLERANCE:
                continue
            if (nx - cx) ** 2 + (ny - cy) ** 2 > PLATEAU_MAX_RADIUS ** 2:
                open_edges += 1
                continue
            inside.add((nx, ny))
            queue.append((nx, ny))
    return inside, open_edges


def components_outside(m, grid, plateau):
    """Label the walkable ground off one plateau; the largest piece is the rest of the map."""
    labels = {}
    sizes = []
    for x, y in m.playable_cells():
        if (x, y) in labels or (x, y) in plateau or not grid[y * m.width + x]:
            continue
        label = len(sizes)
        labels[(x, y)] = label
        queue = deque([(x, y)])
        size = 0
        while queue:
            px, py = queue.popleft()
            size += 1
            for nx, ny in ((px + 1, py), (px - 1, py), (px, py + 1), (px, py - 1)):
                if (nx, ny) in labels or (nx, ny) in plateau:
                    continue
                if not (m.border <= nx < m.border + m.playable_x and
                        m.border <= ny < m.border + m.playable_y):
                    continue
                if not grid[ny * m.width + nx]:
                    continue
                labels[(nx, ny)] = label
                queue.append((nx, ny))
        sizes.append(size)
    main = sizes.index(max(sizes)) if sizes else -1
    return labels, main


def clusters(cells):
    """8-connected groups of a set of cells."""
    left = set(cells)
    groups = []
    while left:
        seed = left.pop()
        group = [seed]
        queue = deque([seed])
        while queue:
            x, y = queue.popleft()
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    n = (x + dx, y + dy)
                    if n in left:
                        left.remove(n)
                        group.append(n)
                        queue.append(n)
        groups.append(group)
    return groups


def study_start(m, grid, index, position, median_height, all_heights, centre_cell):
    centre = m.cell_of(position)
    around = [m.height_at(centre[0] + dx, centre[1] + dy)
              for dx in range(-3, 4) for dy in range(-3, 4)]
    start_height = statistics.median(around)

    plateau, open_edges = plateau_of(m, grid, centre, start_height)
    labels, main = components_outside(m, grid, plateau)

    exits = set()
    cliff_edges = 0
    edges = 0
    for x, y in plateau:
        for n in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
            if n in plateau:
                continue
            edges += 1
            if not grid[n[1] * m.width + n[0]]:
                cliff_edges += 1
            elif labels.get(n) == main:
                exits.add(n)

    ramps = []
    for group in clusters(exits):
        if len(group) < RAMP_MIN_CELLS:
            continue
        gx = sum(c[0] for c in group) / float(len(group))
        gy = sum(c[1] for c in group) / float(len(group))
        heading = math.atan2(gy - centre[1], gx - centre[0])
        probe = (int(gx + RAMP_PROBE * math.cos(heading) + 0.5),
                 int(gy + RAMP_PROBE * math.sin(heading) + 0.5))
        probe = (min(max(probe[0], 0), m.width - 1), min(max(probe[1], 0), m.height - 1))
        drop = int(start_height - m.height_at(*probe))
        ramps.append({
            "way": "down" if drop >= 4 else "up" if drop <= -4 else "level",
            "width_cells": len(group),
            "bearing": bearing(gx - centre[0], gy - centre[1]),
            "off_centre_deg": angle_between(bearing(gx - centre[0], gy - centre[1]),
                                            bearing(centre_cell[0] - centre[0],
                                                    centre_cell[1] - centre[1])),
            "distance_cells": round(math.hypot(gx - centre[0], gy - centre[1]), 1),
            "drop_bytes": drop,
            "foot": (gx + RAMP_PROBE * math.cos(heading), gy + RAMP_PROBE * math.sin(heading)),
        })
    ramps.sort(key=lambda r: -r["width_cells"])

    # the pad proper: rings out from the start until one holds a cliff, water or a cell more
    # than FLAT_PAD_BYTES off the start's height (RMG_FLAT_RADIUS is the generator's)
    flat = 0
    while flat < PLATEAU_MAX_RADIUS:
        ring = flat + 1
        steps = max(8, int(2 * math.pi * ring))
        cells = [(int(centre[0] + ring * math.cos(2 * math.pi * i / steps) + 0.5),
                  int(centre[1] + ring * math.sin(2 * math.pi * i / steps) + 0.5))
                 for i in range(steps)]
        if any(not grid[y * m.width + x] or abs(m.height_at(x, y) - start_height) > FLAT_PAD_BYTES
               for x, y in cells):
            break
        flat = ring

    below = sum(1 for h in all_heights if h < start_height)
    return {
        "player": index + 1,
        "cell": [round(position[0], 1), round(position[1], 1)],
        "height_bytes": start_height,
        "height_delta_median": start_height - median_height,
        "height_percentile": round(100.0 * below / len(all_heights), 1),
        "flat_pad_radius_cells": flat,
        "plateau_cells": len(plateau),
        "plateau_radius_cells": round(math.sqrt(len(plateau) / math.pi), 1),
        "plateau_cliff_edge_share": round(cliff_edges / float(max(edges, 1)), 3),
        "plateau_hit_radius_cap": open_edges > 0,
        "ramp_exits": len(ramps),
        "ramp_exits_down": sum(1 for r in ramps if r["way"] == "down"),
        "ramps": ramps,
        "bearing_to_centre": bearing(centre_cell[0] - centre[0], centre_cell[1] - centre[1]),
    }, plateau


def symmetry(m):
    """Which mirror or turn maps the height field onto itself best."""
    px, py, b = m.playable_x, m.playable_y, m.border
    if abs(px - py) <= 2:                           # a map a cell or two off square is square
        px = py = min(px, py)
    transforms = {
        "rotate_180": lambda x, y: (px - 1 - x, py - 1 - y),
        "mirror_x": lambda x, y: (px - 1 - x, y),
        "mirror_y": lambda x, y: (x, py - 1 - y),
    }
    if px == py:
        transforms["mirror_diagonal"] = lambda x, y: (y, x)
        transforms["mirror_antidiagonal"] = lambda x, y: (py - 1 - y, px - 1 - x)
        transforms["rotate_90"] = lambda x, y: (py - 1 - y, x)

    scores = {}
    for name, transform in transforms.items():
        same = total = 0
        for y in range(0, py, 2):
            for x in range(0, px, 2):
                tx, ty = transform(x, y)
                total += 1
                if abs(m.height_at(x + b, y + b) - m.height_at(tx + b, ty + b)) <= 3:
                    same += 1
        scores[name] = round(same / float(total), 3)

    best = max(scores, key=scores.get)
    start_error = None
    if len(m.starts) > 1:
        errors = []
        for sx, sy in m.starts:
            tx, ty = transforms[best](sx, sy)
            errors.append(min(math.hypot(tx - ox, ty - oy) for ox, oy in m.starts))
        start_error = round(max(errors), 1)
    return {"scores": scores, "best": best, "best_score": scores[best],
            "starts_off_by_cells": start_error}


def money_class(site, owner, walks, plateaus, ramps, pair_walk):
    if site in plateaus[owner]:
        return "plateau"
    for ramp in ramps[owner]:
        if math.hypot(site[0] - ramp["foot"][0], site[1] - ramp["foot"][1]) <= RAMP_FOOT_CELLS:
            return "ramp_foot"
    ordered = sorted(w for w in walks if w >= 0)
    if len(ordered) > 1 and ordered[1] - ordered[0] <= CONTESTED_SHARE * pair_walk:
        return "mid"
    if ordered and ordered[0] <= HOME_SHARE * pair_walk:
        return "home"
    return "field"


def texture_profile(m, water_near, spans, median_height):
    """Coverage of each texture over the dry playable area and where it lies, then the
    generator's four classes picked by role: rock where it is steep, sand at the water, grass the
    biggest of the rest, dirt whatever of the rest sits well above the grass (RandomMapGenerator's
    terrain class pass makes the same four calls). A lake bed is under water and not counted."""
    owner = {}
    for name, first, count in m.texture_classes:
        for tile in range(first, first + count):
            owner[tile] = name

    stats = {}
    total = 0
    for x, y in m.playable_cells():
        if m.water[y * m.width + x]:
            continue
        name = owner.get(m.tiles[y * m.width + x] >> 2, "?")
        entry = stats.setdefault(name, [0, 0.0, 0.0, 0])
        entry[0] += 1
        entry[1] += spans[y * m.width + x]
        entry[2] += m.height_at(x, y) - median_height
        entry[3] += water_near[y * m.width + x]
        total += 1

    textures = []
    near_share = sum(entry[3] for entry in stats.values()) / float(max(total, 1))
    for name, (cells, span, height, near) in sorted(stats.items(), key=lambda kv: -kv[1][0]):
        mean_span = span / cells
        shore = near / float(cells)
        # ponytail: fixed role thresholds; a map that paints rock on the flat calls it grass
        if mean_span > CLIFF_WORLD_SPAN * 0.45:
            role = "rock"
        elif shore > max(0.25, 2.0 * near_share) and cells >= 0.005 * total:
            role = "sand"
        else:
            role = None
        textures.append({"name": name, "share": round(cells / float(total), 4),
                         "mean_span_world": round(mean_span, 2),
                         "mean_height_vs_median": round(height / cells, 1),
                         "shore_share": round(shore, 3), "role": role})

    ground = [t for t in textures if t["role"] is None]
    if ground:
        base = ground[0]["mean_height_vs_median"]
        for texture in ground:
            texture["role"] = "dirt" if texture["mean_height_vs_median"] > base + DIRT_RISE \
                else "grass"

    reduced = {}
    for role in ("grass", "sand", "dirt", "rock"):
        members = [t for t in textures if t["role"] == role]
        reduced[role] = {"texture": members[0]["name"] if members else None,
                         "share": round(sum(t["share"] for t in members), 4),
                         "members": [t["name"] for t in members]}
    return textures, reduced


def object_profile(m):
    counts = Counter(template for template, _, _ in m.objects)
    def pick(words):
        return dict(sorted(((k, v) for k, v in counts.items()
                            if any(w in k.lower() for w in words)), key=lambda kv: -kv[1]))
    trees = pick(("tree",))
    rocks = pick(("rock", "boulder", "cliff"))
    shrubs = pick(("shrub", "bush", "grass", "weed"))
    tech = pick(("tech",))
    named = set(trees) | set(rocks) | set(shrubs) | set(tech)
    other = [[k, v] for k, v in counts.most_common() if k not in named][:20]
    return {"total": sum(counts.values()), "templates": len(counts),
            "trees": trees, "rocks": rocks, "shrubs": shrubs, "tech": tech,
            "top_other": other}


def map_type_guess(cliff_share, water_share, river_span, trees_per_kcell, massif_lift, raised):
    # ponytail: first rule that fits, thresholds read off a handful of shipped maps
    if trees_per_kcell > 12:
        return "forest"
    if river_span > 0.6:
        return "river"
    if water_share > 0.04:
        return "lakes"
    if massif_lift > 20:
        return "massif"
    if cliff_share > 0.12:
        return "canyon" if not raised else "highlands"
    if cliff_share > 0.05 or raised:
        return "highlands"
    return "plains"


def profile(data, source):
    m = StyleMap(data)
    grid = m.passable_grid()

    playable = list(m.playable_cells())
    centre_cell = (m.border + m.playable_x / 2.0, m.border + m.playable_y / 2.0)
    walks = [m.walk_distances(grid, position) for position in m.starts]

    # Heights are measured over the ground an army can walk to: a shipped map rings itself with
    # mountains nobody reaches, and counting them puts every base in a hole.
    reached = [(x, y) for x, y in playable if any(w[y * m.width + x] >= 0 for w in walks)] \
        or playable
    all_heights = [m.height_at(x, y) for x, y in playable]
    heights = [m.height_at(x, y) for x, y in reached]
    median_height = statistics.median(heights)

    spans = [0.0] * (m.width * m.height)
    for y in range(m.height - 1):
        for x in range(m.width - 1):
            spans[y * m.width + x] = m.cell_span_world(x, y)

    # starts
    starts = []
    plateaus = []
    for index, position in enumerate(m.starts):
        entry, plateau = study_start(m, grid, index, position, median_height, heights,
                                     centre_cell)
        starts.append(entry)
        plateaus.append(plateau)

    pairs = []
    for a in range(len(m.starts)):
        for b in range(a + 1, len(m.starts)):
            cell = m.cell_of(m.starts[b])
            walk = walks[a][cell[1] * m.width + cell[0]]
            straight = math.hypot(m.starts[a][0] - m.starts[b][0], m.starts[a][1] - m.starts[b][1])
            pairs.append({"players": [a + 1, b + 1], "straight_cells": round(straight, 1),
                          "walk_cells": walk,
                          "detour": round(walk / straight, 2) if walk > 0 else None})
    reachable = [p["walk_cells"] for p in pairs if p["walk_cells"] > 0]
    pair_walk = min(reachable) if reachable else m.playable

    # money
    ramps = [s["ramps"] for s in starts]
    money = []
    for kind, sites in (("dock", m.supplies), ("pile", m.piles), ("derrick", m.derricks)):
        for position in sites:
            cell = m.cell_of(position)
            site_walks = [w[cell[1] * m.width + cell[0]] for w in walks]
            straight = [math.hypot(position[0] - s[0], position[1] - s[1]) for s in m.starts]
            known = [(w, i) for i, w in enumerate(site_walks) if w >= 0]
            owner = min(known)[1] if known else straight.index(min(straight))
            money.append({"kind": kind, "cell": [round(position[0], 1), round(position[1], 1)],
                          "owner": owner + 1,
                          "class": money_class(cell, owner, site_walks, plateaus, ramps, pair_walk),
                          "walk_cells": site_walks,
                          "straight_cells": [round(s, 1) for s in straight]})
    for entry in starts:
        mine = [x for x in money if x["owner"] == entry["player"]]
        entry["money"] = dict(Counter("%s_%s" % (x["kind"], x["class"]) for x in mine))
        supply = [x["walk_cells"][entry["player"] - 1] for x in mine if x["kind"] == "dock"]
        entry["nearest_supply_walk"] = min(supply) if supply else None
        straight = [x["straight_cells"][entry["player"] - 1] for x in mine if x["kind"] == "dock"]
        entry["nearest_supply_straight"] = min(straight) if straight else None
    for ramp_list in ramps:
        for ramp in ramp_list:
            del ramp["foot"]

    # water
    water_cells = sum(m.water[y * m.width + x] for x, y in playable)
    near = bytearray(m.width * m.height)
    shore = deque()
    dist = [-1] * (m.width * m.height)
    for index, wet in enumerate(m.water):
        if wet:
            dist[index] = 0
            shore.append(index)
    while shore:
        index = shore.popleft()
        if dist[index] >= SHORE_CELLS:
            continue
        near[index] = 1
        x, y = index % m.width, index // m.width
        for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
            if 0 <= nx < m.width and 0 <= ny < m.height and dist[ny * m.width + nx] < 0:
                dist[ny * m.width + nx] = dist[index] + 1
                shore.append(ny * m.width + nx)
    areas = []
    river_span = 0.0
    for points, surface in m.water_areas:
        xs = [p[0] for p in points]
        ys = [p[1] for p in points]
        gx, gy = sum(xs) / len(xs), sum(ys) / len(ys)
        span = max(max(xs) - min(xs), max(ys) - min(ys)) / float(max(m.playable_x, m.playable_y))
        river_span = max(river_span, span)
        areas.append({"centroid": [round(gx, 1), round(gy, 1)],
                      "centre_distance_share": round(
                          math.hypot(gx - m.playable_x / 2.0, gy - m.playable_y / 2.0) /
                          (m.playable / 2.0), 2),
                      "nearest_start_cells": round(min(
                          (math.hypot(gx - s[0], gy - s[1]) for s in m.starts), default=0), 1),
                      "extent_share": round(span, 2),
                      "surface_bytes": round(surface / MAP_HEIGHT_SCALE, 1)})

    # ground
    cliff_cells = sum(1 for x, y in playable if not m.water[y * m.width + x] and m.cliff_at(x, y))
    radius = m.playable * 0.25
    middle = [(x, y) for x, y in playable
              if math.hypot(x + 0.5 - centre_cell[0], y + 0.5 - centre_cell[1]) <= radius]
    steps = [abs(m.height_at(x + 1, y) - m.height_at(x, y)) for x, y in middle]
    middle_heights = [m.height_at(x, y) for x, y in middle]
    open_fraction, crossings = m.tightest_ring(grid)

    textures, reduced = texture_profile(m, near, spans, median_height)
    objects = object_profile(m)
    trees = sum(objects["trees"].values())

    # The median is usually the bases' own flat, the biggest piece of level ground there is, so
    # "raised" is read against the middle of the map and the start's percentile instead.
    middle_median = statistics.median(middle_heights)
    for entry in starts:
        entry["height_delta_middle"] = entry["height_bytes"] - middle_median
    start_deltas = [s["height_delta_middle"] for s in starts]
    raised = bool(starts) and (statistics.median(start_deltas) >= RAISED_BYTES or
                               statistics.median([s["height_percentile"] for s in starts]) >= 60)
    middle_lift = -statistics.median(start_deltas) if starts else 0
    cliff_share = cliff_cells / float(len(playable))
    water_share = water_cells / float(len(playable))
    trees_per_kcell = 1000.0 * trees / len(playable)
    # ponytail: the biome by texture names weighted by coverage; a map named against type reads wrong
    words = Counter()
    for texture in textures:
        for word in ("snow", "sand", "grass", "dirt"):
            if word in texture["name"].lower():
                words[word] += texture["share"]
    biome = ("winter" if words["snow"] > 0.3 else
             {"sand": "desert", "grass": "temperate", "dirt": "steppe"}.get(
                 max(("sand", "grass", "dirt"), key=lambda w: words[w]), "temperate"))

    supply_walks = [s["nearest_supply_walk"] for s in starts if s["nearest_supply_walk"]]
    players = max(len(starts), 1)
    ring = [math.hypot(sx - m.playable_x / 2.0, sy - m.playable_y / 2.0) for sx, sy in m.starts]
    ups = [-r["drop_bytes"] for s in starts for r in s["ramps"] if r["way"] == "up"]
    downs = [r["width_cells"] for s in starts for r in s["ramps"] if r["way"] == "down"]
    return {
        "source": source,
        "map": {"size_cells": [m.playable_x, m.playable_y], "border_cells": m.border,
                "players": len(starts), "weather": m.world.get("weather", 0)},
        "height": {"over": "walkable from a start", "reached_share": round(
                       len(reached) / float(len(playable)), 4),
                   "median": median_height, "min": min(heights), "max": max(heights),
                   "p10": percentile(heights, 0.10), "p25": percentile(heights, 0.25),
                   "p75": percentile(heights, 0.75), "p90": percentile(heights, 0.90),
                   "whole_map_median": statistics.median(all_heights),
                   "whole_map_max": max(all_heights)},
        "starts": starts,
        "start_pairs": pairs,
        "symmetry": symmetry(m),
        "money": {"docks": len(m.supplies), "piles": len(m.piles), "derricks": len(m.derricks),
                  "classes": dict(Counter("%s_%s" % (x["kind"], x["class"]) for x in money)),
                  "sites": money},
        "water": {"share": round(water_share, 4), "areas": areas},
        "ground": {
            "passable_share": round(sum(grid[y * m.width + x] for x, y in playable) /
                                    float(len(playable)), 4),
            "cliff_share": round(cliff_share, 4),
            "cliff_source": "painted bits" if m.cliff_bits else "height span",
            "middle_radius_cells": round(radius, 1),
            "middle_step_bytes": round(sum(steps) / float(len(steps)), 2),
            "middle_height_stdev": round(statistics.pstdev(middle_heights), 1),
            "middle_cliff_share": round(sum(1 for x, y in middle if m.cliff_at(x, y)) /
                                        float(len(middle)), 4),
            # mapscore.tightest_ring: the ring round the middle a unit can stand on the least of
            "choke_ring_open_share": round(open_fraction, 3),
            "choke_ring_crossings": crossings,
        },
        "textures": {"classes": textures, "reduced": reduced},
        "objects": objects,
        "lighting": m.lighting,
        "generator": {
            "map_type_guess": map_type_guess(cliff_share, water_share, river_span,
                                             trees_per_kcell, middle_lift, raised),
            "biome_guess": biome,
            "textures": [reduced[r]["texture"] for r in ("grass", "sand", "dirt", "rock")],
            "playable_cells": max(m.playable_x, m.playable_y),
            "relief_bytes": percentile(heights, 0.9) - percentile(heights, 0.1),
            "start_raise_bytes": median_of(start_deltas),     # over the middle
            "starts_raised": raised,
            "base_flat_radius_cells": median_of([s["flat_pad_radius_cells"] for s in starts]),
            "base_plateau_radius_cells": median_of([s["plateau_radius_cells"] for s in starts]),
            "base_cliff_edge_share": median_of([s["plateau_cliff_edge_share"] for s in starts]),
            "ramps_per_base": median_of([s["ramp_exits"] for s in starts]),
            "ramps_down_per_base": median_of([s["ramp_exits_down"] for s in starts]),
            "ramp_width_cells": median_of([r["width_cells"] for s in starts for r in s["ramps"]]),
            "ramp_down_width_cells": median_of(downs),
            # a start's distance from the middle, a share of half the map
            "start_ring_share": round(statistics.median(ring) / (m.playable / 2.0), 3) if ring else None,
            "ridge_bytes": median_of(ups),          # what an "up" exit climbs to: the ground between seats
            # walk_distances steps four ways, so a walk is up to 1.41 times the straight line
            "home_supply_walk_cells": median_of(supply_walks),
            "home_supply_straight_cells": median_of(
                [s["nearest_supply_straight"] for s in starts if s["nearest_supply_straight"]]),
            "docks_per_player": round(len(m.supplies) / float(players), 2),
            "piles_per_player": round(len(m.piles) / float(players), 2),
            "derricks_per_player": round(len(m.derricks) / float(players), 2),
            "middle_lift_bytes": middle_lift,       # over the starts; below zero the middle is a bowl
            "water_share": round(water_share, 4),
            "lakes": len(m.water_areas),
            "cells_per_tree": round(len(playable) / float(trees), 1) if trees else None,
        },
    }


#-----------------------------------------------------------------------------------------------
# The generator's own table entry
#-----------------------------------------------------------------------------------------------

BOWL_FOREST_THRESHOLD = 0.14         # theBowlType's forest column in RandomMapGenerator.cpp
TREE_CELLS_EACH = 110.0              # RMG_TREE_CELLS_EACH
GROVE_TREES = 22                     # RMG_GROVE_TREES
# ponytail: a map's file holds texture names, not their colours; the preview gets a fixed brown set
PREVIEW_COLOURS = ((150, 126, 90), (178, 154, 110), (196, 172, 130), (118, 108, 96))
TIME_OF_DAY_NAMES = {"morning": "TIME_OF_DAY_MORNING", "afternoon": "TIME_OF_DAY_AFTERNOON",
                     "evening": "TIME_OF_DAY_EVENING", "night": "TIME_OF_DAY_NIGHT"}


def cpp_style(result, name):
    """theStyles' entry (RandomMapGenerator.cpp, RMGStyle) for this profile, to paste in. Every
    machine rebuilds a generated map from its name, so a style has to be compiled into the exe."""
    g = result["generator"]
    reduced = result["textures"]["reduced"]
    textures = [reduced[r]["texture"] for r in ("grass", "sand", "dirt", "rock")]
    if textures[1] is None:                         # no beach: the sand class is the second ground
        grass = reduced["grass"]["members"]
        textures[1] = grass[1] if len(grass) > 1 else textures[0]

    def top(group):
        names = [k for k in result["objects"][group] if not k.startswith("Amb_")][:4]
        return (names * 4)[:4]

    def real(v):
        return "%.3ff" % v

    def triple(values):
        return "{ %s }" % ", ".join(real(v) for v in values)

    light = result["lighting"]
    # the generator plants GROVE_TREES beside every base before the woods, so those come off first
    trees = sum(v for k, v in result["objects"]["trees"].items() if not k.startswith("Amb_"))
    woods = max(0, trees - GROVE_TREES * result["map"]["players"])
    area = result["map"]["size_cells"][0] * result["map"]["size_cells"][1]
    share = TREE_CELLS_EACH * woods / (area * (1.3 - 4.0 * BOWL_FOREST_THRESHOLD))
    lines = [
        "\t{\t// %s, from %s" % (name, result["source"]),
        "\t\t\"%s\", %d, %s, %s, %s, %s, %s, %s, %s, %d, %s," % (
            name, g["playable_cells"], real(g["start_ring_share"]), real(g["start_raise_bytes"]),
            real(g["ridge_bytes"]), real(g["base_flat_radius_cells"]),
            real(g["base_plateau_radius_cells"]), real(g["ramp_down_width_cells"]),
            real(g["home_supply_straight_cells"]), int(round(g["piles_per_player"])),
            TIME_OF_DAY_NAMES[light["time_of_day"]]),
        "\t\t{",
        "\t\t\t{ %s }," % ", ".join('"%s"' % t for t in textures),
        "\t\t\t{ %s }," % ", ".join('"%s"' % t for t in top("trees")),
        "\t\t\t{ %s }," % ", ".join('"%s"' % t for t in top("rocks")),
        "\t\t\t\"TwoLaneOld\", %s," % real(min(1.0, share)),
        "\t\t\t%s, %s," % (triple(light["terrain"]["ambient"]), triple(light["terrain"]["diffuse"])),
        "\t\t\t%s, %s," % (triple(light["objects"]["ambient"]), triple(light["objects"]["diffuse"])),
        "\t\t\t%s," % triple(light["terrain"]["direction"]),
        "\t\t\t{ %s }" % ", ".join("{ %d, %d, %d }" % c for c in PREVIEW_COLOURS),
        "\t\t}",
        "\t},",
    ]
    return "\n".join(lines)


#-----------------------------------------------------------------------------------------------
# Self check and CLI
#-----------------------------------------------------------------------------------------------

def selfcheck():
    # hand-built: "ab" literal plus a short copy of 8 at distance 2, an int-form copy of 10 at
    # distance 10, a literal block of four and the end marker carrying one more literal
    stream = bytes([0x10, 0xfb, 0x00, 0x00, 0x19,
                    0x16, 0x01]) + b"ab" + bytes([0x86, 0x00, 0x09,
                                                  0xe0]) + b"WXYZ" + bytes([0xfd]) + b"!"
    expected = b"ababababab" + b"ababababab" + b"WXYZ" + b"!"
    assert refpack_decode(stream) == expected, refpack_decode(stream)

    payload = bytes((i * 7919) & 0xff for i in range(1003))
    assert refpack_decode(refpack_encode_literal(payload)) == payload
    container = b"EAR\0" + struct.pack("<i", len(payload)) + refpack_encode_literal(payload)
    assert unpack_map(container) == payload
    assert unpack_map(b"CkMp....") == b"CkMp...."
    print("mapstyle selfcheck OK")


def main():
    if sys.argv[1:] == ["selfcheck"]:
        selfcheck()
        return 0

    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("path", help="a .map file, or a .big archive")
    parser.add_argument("inner", nargs="?", help="the map's path inside the .big")
    parser.add_argument("-o", "--output", help="write the JSON here instead of stdout")
    parser.add_argument("--cpp", metavar="NAME",
                        help="print the generator's style table entry under this name instead")
    arguments = parser.parse_args()

    data = load_bytes(arguments.path, arguments.inner)
    source = arguments.inner or os.path.basename(arguments.path)
    result = profile(data, source)
    if arguments.cpp:
        print(cpp_style(result, arguments.cpp))
        return 0
    text = json.dumps(result, indent=1)

    if arguments.output:
        with open(arguments.output, "w") as handle:
            handle.write(text + "\n")
    else:
        print(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
