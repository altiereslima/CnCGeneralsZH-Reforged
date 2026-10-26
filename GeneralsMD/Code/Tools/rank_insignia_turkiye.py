"""Rank marks for Turkiye, one for every rank of the Turkish Land Forces the ladder climbs, from Er to
Mareşal, drawn after the army's own insignia and cut down for twelve to fourteen pixels high. Each
mark is the sign without the board or patch it sits on; a dark red edge stands in for the board and
lets it read over sand and grass the way the other sides' marks do.

What each grade wears, and where it parts from the real one:

  Er           nothing, as in the army.
  Onbaşı       one red chevron, point down; Çavuş two. The army's are red on black, wide and shallow
               with square ends, and so are these.
  Uzman Çavuş  the astsubay pattern in red: crescent and star with two chevrons. Drawn from the
               Gendarmerie's uzman çavuş badge, the one in vector; the army's own (TR-Army-OR5a) is a
               raster that could not be read here, so this grade is the least certain of the set.
  Astsubay     yellow on black: the crescent, horns up, with the star upside down in its cup, two or
               three chevrons whose arms come down from the corners to the crescent, then U-shaped
               bands under the crescent and straight bars under those. The lines count up the grade,
               two for Çavuş to seven for Kıdemli Başçavuş: 2 chevrons; 3; 3 and a band; 3 and 2
               bands; 3, 2 bands and a bar; 3, 2 bands and 2 bars. At 14 pixels the stripes merge: the
               crescent and star on top and the length of the tail under it carry the order, the
               count of lines only reads in the 64-pixel cell.
  Officers     gold five-pointed stars with the ridge of every point shaded, one to three for Teğmen,
               Üsteğmen, Yüzbaşı. The board stacks them along its length; here two sit side by side
               and three in a triangle, so a mark is never three times wider than high.
  Field        Binbaşı, Yarbay, Albay: one to three stars over the laurel sprays that cup a small
               star. The emblem's coloured ground on the board is left out.
  Generals     one to four stars in a row over the laurel sprays round crossed sabers on red. The
               sabers survive only as a light cross on the red disc at 14 pixels.
  Mareşal      no stars; the laurel closes round the red disc and the crossed sabers. The Commons
               drawing's central emblem could not be made out, so the general's emblem stands in.

Sources, read on 2026-10-22: en.wikipedia.org/wiki/Military_ranks_of_Turkey (the ladder and its
image names); the Wikimedia Commons drawings it uses, Army-TUR-OR-04 and -05 (erbaş chevrons,
#d93a36 on black), Army-TUR-OR-06a, -06b, -07, -07a, -07-1, -07-2 (the six astsubay badges,
#F4DC11 on black), Uzman_Çavuş_(Jandarma), Turkey-army-OF-1b, OF-3, OF-6, OF-10 (the officers'
boards: gold #F1D743 faceted stars, laurel sprays, the general's red ground and crossed blades);
kadimhukuk.com.tr's astsubay table ("Ay Yıldız + 2 Çizgi" for Çavuş up to "+ 7 Çizgi" for Kıdemli
Başçavuş); tr.wikipedia.org/wiki/Uzman_çavuş for which drawing is the army's uzman çavuş.

    python Tools/rank_insignia_turkiye.py
        writes Data/Art/Textures/ReforgedRanksTurkiye.tga, one 64-pixel cell per rank, and the
        ReforgedRanksTurkiye.ini that names each cell RankTurkiye_<grade>.

    python Tools/rank_insignia_turkiye.py preview <png>
        also writes a labelled sheet of every mark at game size and at full size.

Drawn eight times too big and brought down, which is the antialiasing."""
import math
import os
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

from rank_insignia_cells import ink_entries

HERE = os.path.dirname(os.path.abspath(__file__))
SCALE = 8
CELL = 64
COLUMNS = 8
ROWS = 3
OUTLINE = 21    # a MaxFilter size, odd: the dark edge round every mark, 10 drawn pixels
EDGE = (48, 4, 6)
# metal: (top colour, bottom colour); the layers paint in this order, later over earlier
METALS = {
    "red": ((236, 78, 68), (160, 30, 26)),           # erbaş chevrons and the uzman çavuş badge, #d93a36
    "yellow": ((255, 240, 96), (214, 176, 10)),      # astsubay badges, #F4DC11
    "field": ((214, 26, 24), (128, 6, 6)),           # the general's red ground
    "gold": ((255, 230, 120), (204, 140, 24)),       # officers' stars and laurel, #F1D743
    "gold_dark": ((214, 170, 40), (150, 92, 10)),    # the shaded ridge of a star's points, #DBB110
    "silver": ((252, 252, 252), (150, 156, 168)),    # crossed sabers
}
NAME = "ReforgedRanksTurkiye"
TEXTURE_PATH = os.path.join(HERE, "..", "Data", "Art", "Textures", NAME + ".tga")
INI_PATH = os.path.join(HERE, "..", "Data", "INI", "MappedImages", "HandCreated", NAME + ".ini")
INI_HEADER = """; Turkiye's rank marks, one per Turkish Land Forces rank, all in one texture.  Written by
; Tools/rank_insignia_turkiye.py with the texture it names; do not edit by hand.  Mapped images are
; not in the multiplayer INI checksum.
"""
INI_ENTRY = """
MappedImage RankTurkiye_%d
  Texture = %s.tga
  TextureWidth = %d
  TextureHeight = %d
  Coords = Left:%d Top:%d Right:%d Bottom:%d
  Status = NONE
End
"""

# the erbaş chevron in Army-TUR-OR-04's proportions: square ends, shallow V, thin arms
CHEVRON_HALF_WIDTH = 20
CHEVRON_DROP = 11.7
CHEVRON = 5.0
CHEVRON_PITCH = 8.0
# the astsubay badge in the units of the Commons drawings (444 wide, crescent on x 222)
BADGE = 0.12    # shipped pixels per unit
ARM_TOP = 38
LINE = 31       # every stripe, chevron arm, band and bar is this thick
LINE_PITCH = 44

# grade, name, mark, the real insignia it follows; grade 0 is unranked on every side and the Er wears
# nothing, so both cells stay empty
RANKS = (
    (0, "Rütbesiz", ("none",), "no rank"),
    (1, "Er", ("none",), "no insignia"),
    (2, "Onbaşı", ("chevrons", 1), "1 red chevron (OR-04)"),
    (3, "Çavuş", ("chevrons", 2), "2 red chevrons (OR-05)"),
    (4, "Uzman Çavuş", ("astsubay", "red", 2, 0, 0), "red crescent, star, 2 chevrons (Jandarma)"),
    (5, "Astsubay Çavuş", ("astsubay", "yellow", 2, 0, 0), "crescent, star + 2 lines (OR-06b)"),
    (6, "Astsubay Kıdemli Çavuş", ("astsubay", "yellow", 3, 0, 0), "crescent, star + 3 lines (OR-06a)"),
    (7, "Astsubay Üstçavuş", ("astsubay", "yellow", 3, 1, 0), "crescent, star + 4 lines (OR-07a)"),
    (8, "Astsubay Kıdemli Üstçavuş", ("astsubay", "yellow", 3, 2, 0), "crescent, star + 5 lines (OR-07)"),
    (9, "Astsubay Başçavuş", ("astsubay", "yellow", 3, 2, 1), "crescent, star + 6 lines (OR-07-2)"),
    (10, "Astsubay Kıdemli Başçavuş", ("astsubay", "yellow", 3, 2, 2), "crescent, star + 7 lines (OR-07-1)"),
    (11, "Teğmen", ("officer", 1), "1 gold star (OF-1b)"),
    (12, "Üsteğmen", ("officer", 2), "2 gold stars (OF-1)"),
    (13, "Yüzbaşı", ("officer", 3), "3 gold stars (OF-2)"),
    (14, "Binbaşı", ("field", 1), "1 star over laurel (OF-3)"),
    (15, "Yarbay", ("field", 2), "2 stars over laurel (OF-4)"),
    (16, "Albay", ("field", 3), "3 stars over laurel (OF-5)"),
    (17, "Tuğgeneral", ("general", 1), "1 star, laurel, sabers on red (OF-6)"),
    (18, "Tümgeneral", ("general", 2), "2 stars, laurel, sabers on red (OF-7)"),
    (19, "Korgeneral", ("general", 3), "3 stars, laurel, sabers on red (OF-8)"),
    (20, "Orgeneral", ("general", 4), "4 stars, laurel, sabers on red (OF-9)"),
    (21, "Mareşal", ("marshal",), "closed laurel, no stars (OF-10)"),
)


def scaled(points):
    """Points in shipped pixels, in drawn pixels."""
    return [(x * SCALE, y * SCALE) for x, y in points]


def box(cx, cy, rx, ry):
    return scaled([(cx - rx, cy - ry), (cx + rx, cy + ry)])


def star_points(cx, cy, radius, turn=-90):
    points = []
    for i in range(10):
        r = radius if i % 2 == 0 else radius * 0.5
        angle = math.radians(turn + 36 * i)
        points.append((cx + r * math.cos(angle), cy + r * math.sin(angle)))
    return points


class Layers:
    """One white-on-black mask per metal, CELL by CELL in drawn pixels."""

    def __init__(self):
        self.masks = {}

    def draw(self, metal):
        if metal not in self.masks:
            self.masks[metal] = Image.new("L", (CELL * SCALE, CELL * SCALE), 0)
        return ImageDraw.Draw(self.masks[metal])

    def paste(self, metal, shape):
        self.draw(metal)
        self.masks[metal].paste(255, (0, 0), shape)


def blank():
    return Image.new("L", (CELL * SCALE, CELL * SCALE), 0)


def star(layers, cx, cy, radius):
    """A gold five-pointed star, point up, the right half of every point in shadow like the struck
    metal ones."""
    points = star_points(cx, cy, radius)
    layers.draw("gold").polygon(scaled(points), fill=255)
    shade = layers.draw("gold_dark")
    for i in range(0, 10, 2):
        shade.polygon(scaled([(cx, cy), points[i], points[i + 1]]), fill=255)


def stars(layers, count, cy, radius, pitch):
    """count stars in a tight cluster centred across the cell on cy: two side by side, three in a
    triangle. Returns the cluster's bottom."""
    layout = {1: [(0, 0)],
              2: [(-0.5, 0), (0.5, 0)],
              3: [(0, -0.45), (-0.5, 0.45), (0.5, 0.45)]}[count]
    for x, y in layout:
        star(layers, 32 + x * pitch, cy + y * pitch, radius)
    return cy + max(y for _, y in layout) * pitch + radius * 0.81


def chevron_down(draw, top):
    """The erbaş chevron, point down, square ends, its arms' tops on top."""
    w, d, t = CHEVRON_HALF_WIDTH, CHEVRON_DROP, CHEVRON
    draw.polygon(scaled([(32 - w, top), (32, top + d), (32 + w, top), (32 + w, top + t),
                         (32, top + d + t), (32 - w, top + t)]), fill=255)


def astsubay(layers, metal, chevrons, bands, bars):
    """The astsubay badge after the Commons drawings, in their units: the inverted star in the cup of
    the crescent, the chevrons' arms coming down from the corners, the bands under the crescent and the
    bars under the bands. The crescent alone has no line in the count."""
    bottom = 301
    if bands:
        bottom = 339 + LINE_PITCH * (bands - 1)
    if bars:
        bottom = 393 + LINE_PITCH * (bars - 1) + LINE
    oy = 32 - (ARM_TOP + bottom) / 2 * BADGE

    def at(x, y):
        return 32 + (x - 222) * BADGE, oy + y * BADGE

    def ellipse(shape, cx, cy, rx, ry, fill):
        x0, y0 = at(cx - rx, cy - ry)
        x1, y1 = at(cx + rx, cy + ry)
        ImageDraw.Draw(shape).ellipse(scaled([(x0, y0), (x1, y1)]), fill=fill)

    draw = layers.draw(metal)
    sx, sy = at(222, 131)
    draw.polygon(scaled(star_points(sx, sy, 36 * BADGE, turn=90)), fill=255)
    moon = blank()
    ellipse(moon, 222, 229, 72, 72, 255)
    ellipse(moon, 222, 211, 58, 58, 0)
    layers.paste(metal, moon)
    for i in range(chevrons):
        top = ARM_TOP + i * LINE_PITCH
        for outer, inner in ((8, 148), (436, 296)):
            draw.polygon(scaled([at(outer, top), at(inner, top + 63), at(inner, top + 63 + LINE),
                                 at(outer, top + LINE)]), fill=255)
    for j in range(bands):
        cy = 170 + j * LINE_PITCH
        band = blank()
        ellipse(band, 222, cy, 207, 169, 255)
        ellipse(band, 222, cy, 207 - LINE, 169 - LINE, 0)
        x0, y0 = at(0, 0)
        x1, y1 = at(444, cy)
        ImageDraw.Draw(band).rectangle(scaled([(x0, y0), (x1, y1)]), fill=0)
        layers.paste(metal, band)
    for k in range(bars):
        y = 393 + k * LINE_PITCH
        draw.rectangle(scaled([at(15, y), at(429, y + LINE)]), fill=255)


def laurel(layers, cx, cy, radius, sweep):
    """Two laurel sprays rising from the bottom of a circle round (cx, cy), each through sweep degrees,
    their leaves along the stem and pointing up it."""
    draw = layers.draw("gold")
    stem = box(cx, cy, radius, radius)
    draw.arc(stem, start=90, end=90 + sweep, fill=255, width=round(1.2 * SCALE))
    draw.arc(stem, start=90 - sweep, end=90, fill=255, width=round(1.2 * SCALE))
    for side in (1, -1):
        for step in range(1, int(sweep // 22) + 1):
            angle = math.radians(90 + side * step * 22)
            x, y = cx + radius * math.cos(angle), cy + radius * math.sin(angle)
            tangent = angle + side * math.pi / 2    # up the stem, away from the bottom
            for lean in (-0.55, 0.55):
                a = tangent + lean
                points = [(x + 2.2 * math.cos(a) + 2.6 * math.cos(t) * math.cos(a) - 1.0 * math.sin(t) * math.sin(a),
                           y + 2.2 * math.sin(a) + 2.6 * math.cos(t) * math.sin(a) + 1.0 * math.sin(t) * math.cos(a))
                          for t in (i * math.pi / 8 for i in range(16))]
                draw.polygon(scaled(points), fill=255)


def saber(layers, cx, cy, half, flip):
    """A curved saber across the disc, hilt low, tip high, crossing its twin."""
    start = (cx - flip * half, cy + half)
    end = (cx + flip * half, cy - half)
    nx, ny = flip * 0.7, 0.7       # the bow, away from the twin's
    left, right = [], []
    for i in range(13):
        t = 0.22 + 0.78 * i / 12
        bow = 2.0 * math.sin(math.pi * t)
        px = start[0] + (end[0] - start[0]) * t + nx * bow
        py = start[1] + (end[1] - start[1]) * t + ny * bow
        w = 1.5 * (1 - t) + 0.3     # across the blade, which runs (flip, -1)
        left.append((px - 0.7 * w, py - flip * 0.7 * w))
        right.append((px + 0.7 * w, py + flip * 0.7 * w))
    draw = layers.draw("silver")
    draw.polygon(scaled(left + right[::-1]), fill=255)
    gx = start[0] + (end[0] - start[0]) * 0.22
    gy = start[1] + (end[1] - start[1]) * 0.22
    draw.line(scaled([(gx - 2.2 * 0.7, gy - 2.2 * 0.7 * flip), (gx + 2.2 * 0.7, gy + 2.2 * 0.7 * flip)]),
              fill=255, width=round(1.2 * SCALE))
    draw.line(scaled([start, (gx, gy)]), fill=255, width=round(1.4 * SCALE))


def sabers_on_red(layers, cx, cy, radius):
    layers.draw("field").ellipse(box(cx, cy, radius, radius), fill=255)
    saber(layers, cx, cy, radius * 0.6, 1)
    saber(layers, cx, cy, radius * 0.6, -1)


def mark(kind):
    """The masks of one rank's mark."""
    layers = Layers()
    if kind[0] == "chevrons":
        count = kind[1]
        height = (count - 1) * CHEVRON_PITCH + CHEVRON + CHEVRON_DROP
        top = (CELL - height) / 2
        for i in range(count):
            chevron_down(layers.draw("red"), top + i * CHEVRON_PITCH)
    elif kind[0] == "astsubay":
        astsubay(layers, *kind[1:])
    elif kind[0] == "officer":
        stars(layers, kind[1], 32, 9, 17)
    elif kind[0] == "field":
        bottom = stars(layers, kind[1], 18, 8, 15)
        cy = bottom + 6
        star(layers, 32, cy + 5, 3.2)
        laurel(layers, 32, cy, 11, 105)
    elif kind[0] == "general":
        count = kind[1]
        pitch = 11
        for i in range(count):
            star(layers, 32 + (i - (count - 1) / 2) * pitch, 14, 5.5)
        sabers_on_red(layers, 32, 34, 9)
        laurel(layers, 32, 30, 15, 120)
    elif kind[0] == "marshal":
        sabers_on_red(layers, 32, 32, 12)
        laurel(layers, 32, 32, 20, 165)
    return layers


def gradient(colours):
    size = CELL * SCALE
    image = Image.new("RGB", (size, size))
    draw = ImageDraw.Draw(image)
    top_colour, bottom_colour = colours
    for y in range(size):
        share = y / (size - 1)
        draw.line((0, y, size, y), fill=tuple(round(top + (bottom - top) * share)
                                              for top, bottom in zip(top_colour, bottom_colour)))
    return image


def finish(layers):
    """The mark in its metals, light at the top and dark at the bottom, on its dark edge."""
    size = (CELL * SCALE, CELL * SCALE)
    art = Image.new("RGB", size, EDGE)
    shape = blank()
    for metal, colours in METALS.items():
        if metal in layers.masks:
            art = Image.composite(gradient(colours), art, layers.masks[metal])
            shape = ImageChops.lighter(shape, layers.masks[metal])
    art.putalpha(shape.filter(ImageFilter.MaxFilter(OUTLINE)))
    return art.resize((CELL, CELL), Image.LANCZOS)


def cell_origin(index):
    return (index % COLUMNS) * CELL, (index // COLUMNS) * CELL


def build():
    atlas = Image.new("RGBA", (COLUMNS * CELL, ROWS * CELL), (0, 0, 0, 0))
    cells = []
    ini_text = INI_HEADER
    for index, (grade, _, kind, _) in enumerate(RANKS):
        assert grade == index, "the atlas holds the ladder in order"
        art = finish(mark(kind))
        x, y = cell_origin(index)
        atlas.paste(art, (x, y))
        cells.append(art)
        ini_text += INI_ENTRY % (grade, NAME, atlas.width, atlas.height, x, y, x + CELL, y + CELL)
    return atlas, cells, ini_text


def preview(cells, path):
    """Every mark at 14 and 64 pixels on sand and on grass, with its grade, name and the real insignia
    it follows, two columns."""
    font = ImageFont.truetype("arial.ttf", 15)
    small_font = ImageFont.truetype("arial.ttf", 12)
    row_h, column_w = 76, 640
    rows = (len(RANKS) + 1) // 2
    sheet = Image.new("RGB", (2 * column_w + 10, 40 + row_h * rows), (30, 32, 34))
    draw = ImageDraw.Draw(sheet)
    draw.text((10, 10), "Turkiye ranks  RankTurkiye_<grade>  (14 px game size and 64 px cell, on sand and grass;"
              " grey line: the real insignia it follows)", font=font, fill=(230, 230, 230))
    for i, ((grade, name, _, real), art) in enumerate(zip(RANKS, cells)):
        left0 = 10 + (i // rows) * column_w
        top = 40 + (i % rows) * row_h
        for j, ground in enumerate(((196, 170, 112), (78, 106, 48))):
            left = left0 + j * 120
            draw.rectangle((left, top, left + 112, top + row_h - 6), fill=ground)
            # the game crops a cell to its ink and draws it 12 or 14 high (rank_insignia_cells.py)
            ink_box = art.getchannel("A").point(lambda v: 255 if v >= 16 else 0).getbbox()
            if ink_box:
                ink = art.crop(ink_box)
                small = ink.resize((max(1, round(ink.width * 14 / ink.height)), 14), Image.LANCZOS)
                sheet.paste(small, (left + 6, top + 28), small)
            sheet.paste(art, (left + 40, top + 3), art)
        draw.text((left0 + 250, top + 10), "%d  %s" % (grade, name), font=font, fill=(240, 240, 240))
        draw.text((left0 + 250, top + 34), "RankTurkiye_%d" % grade, font=small_font, fill=(170, 170, 170))
        draw.text((left0 + 250, top + 50), real, font=small_font, fill=(150, 150, 150))
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    sheet.save(path)
    print(path)


def main():
    atlas, cells, ini_text = build()
    # the astsubay and uzman çavuş badges, the field officers, generals and the Mareşal draw 14 high
    ini_text = ink_entries(atlas, ini_text, "Turkiye", tall=list(range(4, 11)) + list(range(14, 22)))
    assert "RankTurkiye_0\n" not in ini_text and "RankTurkiye_1\n" not in ini_text, "grade 0 and the Er wear nothing"
    atlas.save(TEXTURE_PATH)
    print(TEXTURE_PATH)
    with open(INI_PATH, "w", newline="\r\n") as ini:
        ini.write(ini_text)
    print(INI_PATH)
    if len(sys.argv) == 3 and sys.argv[1] == "preview":
        preview(cells, sys.argv[2])


if __name__ == "__main__":
    main()
