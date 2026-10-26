"""GLA's rank ladder: twenty marks for an army that never had a regulation one, from a recruit's
single scratch to the Supreme Liberator's toothed plate.

Five families, each a step up from the last, and inside a family bars or a backing plate count the
grades:
    fighters       scratched tally strokes, one to four, then the struck five
    unit leaders   crossed machetes, then one to three upright rust bars beside them
    commanders     the skull, then a bar, then blades or rifles behind it, then a rust plate
    warlords       the scorpion, then a bar, then rifles behind it
    generals       the scorpion on a round plate, then on a toothed one, then the skull on the
                   toothed plate with rifles behind

Bone, rust and khaki on a dark edge with no background, so a mark reads over any ground.  The game
draws these twelve to fourteen pixels high, so every stroke is blunt and the dark edge is thick.

    python Tools/rank_insignia_gla.py
        writes Data/Art/Textures/ReforgedRanksGLA.tga, one 64-pixel cell per rank, and the
        ReforgedRanksGLA.ini that names each cell RankGLA_<n>_<name>.
    python Tools/rank_insignia_gla.py preview <png>
        also writes a labelled sheet of every mark at game size and full size.

Drawn eight times too big and brought down, which is the antialiasing.  Same bytes on every run."""
import math
import os
import random
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

from rank_insignia_cells import ink_entries

import rank_insignia

HERE = os.path.dirname(os.path.abspath(__file__))
CELL = 64
COLUMNS = 8
ROWS = 4
SCALE = 8
EDGE = 3.2    # the dark edge round every element, in shipped pixels
DARK = (24, 18, 12)
BONE = (236, 228, 204)
RUST = (178, 76, 36)
KHAKI = (196, 174, 116)
SAND = (150, 128, 84)
NAME = "ReforgedRanksGLA"
TEXTURE_PATH = os.path.join(HERE, "..", "Data", "Art", "Textures", NAME + ".tga")
INI_PATH = os.path.join(HERE, "..", "Data", "INI", "MappedImages", "HandCreated", NAME + ".ini")
INI_HEADER = """; GLA's rank marks, one cell of %s.tga per rank from Recruit to Supreme Liberator.  Written
; by Tools/rank_insignia_gla.py with the texture it names; do not edit by hand.  Mapped images are
; not in the multiplayer INI checksum.
""" % NAME
INI_ENTRY = """
MappedImage %s
  Texture = %s.tga
  TextureWidth = %d
  TextureHeight = %d
  Coords = Left:%d Top:%d Right:%d Bottom:%d
  Status = NONE
End
"""


class Pen:
    """Maps the 64-unit design square into drawn pixels, moved by (dx, dy) and shrunk by s round
    the cell centre, so a symbol can make room for the bars under it."""

    def __init__(self, dx=0.0, dy=0.0, s=1.0):
        self.dx, self.dy, self.s = dx, dy, s

    def point(self, x, y):
        return (round(((x - 32) * self.s + 32 + self.dx) * SCALE),
                round(((y - 32) * self.s + 32 + self.dy) * SCALE))

    def box(self, x0, y0, x1, y1):
        return self.point(x0, y0) + self.point(x1, y1)

    def width(self, units):
        return max(1, round(units * self.s * SCALE))


def blank():
    return Image.new("L", (CELL * SCALE, CELL * SCALE), 0)


def placed(points, angle, cx, cy):
    """Local points turned by angle degrees and moved to (cx, cy)."""
    c, s = math.cos(math.radians(angle)), math.sin(math.radians(angle))
    return [(cx + x * c - y * s, cy + x * s + y * c) for x, y in points]


def polygon(pen, mask, points):
    ImageDraw.Draw(mask).polygon([pen.point(x, y) for x, y in points], fill=255)


def strokes(pen, count, struck):
    """count scratched upright strokes, uneven like a knife cut them; struck adds the slash that
    closes a five."""
    mask = blank()
    rng = random.Random(count * 7 + struck)
    spacing = 11
    first = 32 - spacing * (count - 1) / 2
    for i in range(count):
        x = first + spacing * i + rng.uniform(-0.8, 0.8)
        lean = rng.uniform(-2.5, 2.5)
        top, bottom = 10 + rng.uniform(-1, 1.5), 54 + rng.uniform(-1.5, 1)
        polygon(pen, mask, [(x - 2.4 + lean, top), (x + 3.2 + lean, top + 1.2),
                            (x + 3.6 - lean, bottom), (x - 3.0 - lean, bottom - 1.0)])
    if struck:
        polygon(pen, mask, [(6, 44), (9, 40), (58, 21), (57, 26)])
    return [(mask, BONE)]


def blade(pen, angle, cx=32, cy=32):
    """A machete pointing along angle: bone blade, rust grip and guard."""
    metal, grip = blank(), blank()
    polygon(pen, metal, placed([(-4, -6), (19, -6.5), (28, -1), (20, 6), (-4, 6)], angle, cx, cy))
    polygon(pen, grip, placed([(-22, -4.2), (-5, -4.2), (-5, 4.2), (-22, 4.2)], angle, cx, cy))
    polygon(pen, grip, placed([(-8, -9), (-3.5, -9), (-3.5, 9), (-8, 9)], angle, cx, cy))
    return [(metal, BONE), (grip, RUST)]


def rifle(pen, angle, cx=32, cy=32):
    """A rifle silhouette pointing along angle, magazine curling under, all khaki."""
    mask = blank()
    for part in ([(-27, -3), (-14, -4), (-14, 4), (-27, 7)],    # stock
                 [(-15, -4), (10, -4), (10, 3), (-15, 3)],    # receiver
                 [(9, -3), (28, -3), (28, 1), (9, 1)],    # barrel
                 [(-5, 2), (1, 2), (5, 11), (-1, 12)]):    # magazine
        polygon(pen, mask, placed(part, angle, cx, cy))
    return [(mask, KHAKI)]


def crossed(item, pen, spread, cx=32, cy=32):
    """Two of item crossed at +-spread degrees, the second drawn over the first."""
    return item(pen, 180 + spread, cx, cy) + item(pen, -spread, cx, cy)


def skull(pen, size, cx=32, cy=32):
    """rank_insignia's skull, size units across, centred on (cx, cy)."""
    shape, _ = rank_insignia.skull()
    drawn = pen.width(size)
    shape = shape.resize((drawn, drawn), Image.LANCZOS).point(lambda v: 255 if v > 127 else 0)
    mask = blank()
    x, y = pen.point(cx - size / 2, cy - size / 2)
    mask.paste(shape, (x, y))
    return [(mask, BONE)]


def scorpion(pen, colour, cx=32, cy=30, k=1.0):
    """A scorpion seen from above, head up, pincers forward and the tail curling round its right.
    No legs and no jaw gaps: at twelve pixels they were the scribble round a blob, and the body, the
    two claws and the hooked tail are what reads."""
    mask = blank()
    draw = ImageDraw.Draw(mask)

    def at(x, y):
        return cx + x * k, cy + y * k

    def ellipse(x0, y0, x1, y1):
        draw.ellipse(pen.box(*at(x0, y0), *at(x1, y1)), fill=255)

    def line(points, width):
        draw.line([pen.point(*at(x, y)) for x, y in points], fill=255, width=pen.width(width * k),
                  joint="curve")

    for side in (-1, 1):
        line([(side * 4, -6), (side * 14, -10), (side * 16, -17)], 5)    # arm
        ellipse(side * 16 - 6.5, -27, side * 16 + 6.5, -14)    # claw
    ellipse(-8, -5, 8, 14)    # body
    ellipse(-6.5, -13, 6.5, -1)    # head
    tail = [(0, 16), (3, 22), (8, 25), (14, 24), (18, 19)]
    for (x, y), r in zip(tail, (5.2, 5.0, 4.8, 4.6, 4.4)):
        ellipse(x - r, y - r, x + r, y + r)
    draw.polygon([pen.point(*at(x, y)) for x, y in ((15, 16), (22, 9), (23, 19))], fill=255)    # sting
    return [(mask, colour)]


ROOM_LEFT, ROOM_RIGHT = 3.5, 60.5    # inside the dark edge's reach of the cell's sides
BAR_PITCH, BAR_WIDTH = 8, 4.4


def bars(count):
    """count upright rust bars at the cell's right, rough at the ends like cut cloth, each its own
    layer so the dark edge parts it from the next."""
    layers = []
    pen = Pen()
    for i in range(count):
        mask = blank()
        right = ROOM_RIGHT - BAR_PITCH * (count - 1 - i)
        left = right - BAR_WIDTH
        polygon(pen, mask, [(left + 0.5, 15), (right, 16), (right - 0.4, 48), (left, 47.5)])
        layers.append((mask, RUST))
    return layers


def plate(teeth):
    """The rust disc the top ranks stand on; teeth gives it a saw rim of scrap metal, few and deep
    so the rim still reads as teeth at twelve pixels."""
    mask = blank()
    pen = Pen()
    if teeth:
        points = []
        for i in range(teeth * 2):
            r = 29 if i % 2 == 0 else 21.5
            a = math.pi * i / teeth - math.pi / 2
            points.append((32 + r * math.cos(a), 32 + r * math.sin(a)))
        polygon(pen, mask, points)
    else:
        ImageDraw.Draw(mask).ellipse(pen.box(6, 6, 58, 58), fill=255)
    return [(mask, RUST)]


def with_bars(symbol, count):
    """symbol moved left and shrunk only as far as the count bars beside it need: under it they
    squeezed the symbol to two thirds and the bars to slivers."""
    bar_room = BAR_PITCH * count
    centre = (ROOM_LEFT + ROOM_RIGHT - bar_room) / 2
    scale = (ROOM_RIGHT - ROOM_LEFT - bar_room) / (ROOM_RIGHT - ROOM_LEFT - 2)
    return symbol(Pen(centre - 32, 0, scale)) + bars(count)


RANKS = (
    ("Unranked", "grade 0 on every side: no mark, so the cell stays empty", lambda: [(blank(), BONE)]),
    ("Recruit", "one scratched stroke", lambda: strokes(Pen(), 1, False)),
    ("Irregular", "two scratched strokes", lambda: strokes(Pen(), 2, False)),
    ("Fighter", "three scratched strokes", lambda: strokes(Pen(), 3, False)),
    ("Raider", "four scratched strokes", lambda: strokes(Pen(), 4, False)),
    ("HardenedRaider", "four strokes struck through, the tally of five",
     lambda: strokes(Pen(), 4, True)),
    ("Squadhand", "crossed machetes", lambda: crossed(blade, Pen(), 45)),
    ("SquadLeader", "crossed machetes over one rust bar",
     lambda: with_bars(lambda pen: crossed(blade, pen, 45), 1)),
    ("CellLeader", "crossed machetes over two rust bars",
     lambda: with_bars(lambda pen: crossed(blade, pen, 45), 2)),
    ("SeniorCellLeader", "crossed machetes over three rust bars",
     lambda: with_bars(lambda pen: crossed(blade, pen, 45), 3)),
    ("RaidCaptain", "the skull", lambda: skull(Pen(), 46)),
    ("ColumnCaptain", "the skull over one rust bar",
     lambda: with_bars(lambda pen: skull(pen, 46), 1)),
    ("StrikeCommander", "the skull on crossed machetes",
     lambda: crossed(blade, Pen(), 40) + skull(Pen(), 38)),
    ("FieldCommander", "the skull on crossed rifles",
     lambda: crossed(rifle, Pen(), 40) + skull(Pen(), 38)),
    ("SectorCommander", "the skull on crossed rifles and a rust plate",
     lambda: plate(0) + crossed(rifle, Pen(), 40) + skull(Pen(), 42)),
    ("Warlord", "the khaki scorpion", lambda: scorpion(Pen(), KHAKI)),
    ("RegionalWarlord", "the khaki scorpion over one rust bar",
     lambda: with_bars(lambda pen: scorpion(pen, KHAKI), 1)),
    ("HighWarlord", "the bone scorpion on crossed rifles",
     lambda: crossed(rifle, Pen(), 40) + scorpion(Pen(), BONE, 32, 30, 0.85)),
    ("General", "the bone scorpion on a round rust plate",
     lambda: plate(0) + scorpion(Pen(), BONE, 32, 30, 0.8)),
    ("GrandGeneral", "the bone scorpion on a toothed rust plate",
     lambda: plate(8) + scorpion(Pen(), BONE, 32, 30, 0.72)),
    ("SupremeLiberator", "the skull on a toothed rust plate with crossed rifles behind it",
     lambda: crossed(rifle, Pen(), 40) + plate(8) + skull(Pen(), 40)),
)


def dilated(mask):
    """mask grown by EDGE shipped pixels with round corners: a blur thresholded just above zero."""
    radius = EDGE * SCALE / 2
    return mask.filter(ImageFilter.GaussianBlur(radius)).point(lambda v: 255 if v > 6 else 0)


def finish(layers):
    """Layers painted bottom up, each over a dark edge of its own, so crossing parts stay apart."""
    size = layers[0][0].size
    art = Image.new("RGB", size, DARK)
    alpha = Image.new("L", size, 0)
    dark = Image.new("RGB", size, DARK)
    for mask, colour in layers:
        edge = dilated(mask)
        art.paste(dark, (0, 0), edge)
        art.paste(Image.new("RGB", size, colour), (0, 0), mask)
        alpha = ImageChops.lighter(alpha, edge)
    art.putalpha(alpha)
    return art.resize((CELL, CELL), Image.LANCZOS)


def image_name(index, name):
    return "RankGLA_%d_%s" % (index + 1, name)


def build():
    """The atlas and the INI text."""
    atlas = Image.new("RGBA", (CELL * COLUMNS, CELL * ROWS), DARK + (0,))
    ini_text = INI_HEADER
    for index, (name, _, draw) in enumerate(RANKS):
        left, top = index % COLUMNS * CELL, index // COLUMNS * CELL
        atlas.paste(finish(draw()), (left, top))
        ini_text += INI_ENTRY % (image_name(index, name), NAME, atlas.width, atlas.height,
                                 left, top, left + CELL, top + CELL)
    return atlas, ini_text


def preview(atlas, path):
    """Every mark at 12 and 16 pixels, as the game minifies it, and at its full 64, over dark
    ground and sand, with its name."""
    font = ImageFont.truetype("arialbd.ttf", 15)
    small = ImageFont.truetype("arial.ttf", 12)
    card_w, card_h, columns = 380, 92, 4
    sheet = Image.new("RGB", (card_w * columns, card_h * math.ceil(len(RANKS) / columns)), (40, 38, 34))
    draw = ImageDraw.Draw(sheet)
    for index, (name, about, _) in enumerate(RANKS):
        x0, y0 = index % columns * card_w, index // columns * card_h
        cell = atlas.crop((index % COLUMNS * CELL, index // COLUMNS * CELL,
                           index % COLUMNS * CELL + CELL, index // COLUMNS * CELL + CELL))
        draw.rectangle((x0 + 4, y0 + 4, x0 + 76, y0 + 76), fill=(70, 74, 58))
        sheet.paste(cell, (x0 + 8, y0 + 8), cell)
        for column, ground in enumerate(((70, 74, 58), (200, 180, 140))):
            gx = x0 + 84 + column * 44
            draw.rectangle((gx, y0 + 4, gx + 40, y0 + 76), fill=ground)
            for size, gy in ((16, y0 + 12), (12, y0 + 46)):
                icon = cell.resize((size, size), Image.LANCZOS)
                sheet.paste(icon, (gx + 20 - size // 2, gy), icon)
        draw.text((x0 + 178, y0 + 10), "%d. %s" % (index + 1, name), fill=(240, 232, 210), font=font)
        draw.text((x0 + 178, y0 + 32), image_name(index, name), fill=(180, 170, 150), font=small)
        words, line = about.split(), ""
        lines = []
        for word in words:
            if draw.textlength(line + " " + word, font=small) > card_w - 186:
                lines.append(line)
                line = word
            else:
                line = (line + " " + word).strip()
        lines.append(line)
        for i, text in enumerate(lines[:2]):
            draw.text((x0 + 178, y0 + 50 + i * 14), text, fill=(200, 190, 160), font=small)
    sheet.save(path)
    print(path)


def main():
    atlas, ini_text = build()
    # the plates and the scorpions, busiest at twelve, draw 14 high
    ini_text = ink_entries(atlas, ini_text, "GLA", tall=range(14, 21))
    atlas.save(TEXTURE_PATH)
    print(TEXTURE_PATH)
    with open(INI_PATH, "w", newline="\r\n") as ini:
        ini.write(ini_text)
    print(INI_PATH)
    if len(sys.argv) == 3 and sys.argv[1] == "preview":
        preview(atlas, sys.argv[2])


if __name__ == "__main__":
    main()
