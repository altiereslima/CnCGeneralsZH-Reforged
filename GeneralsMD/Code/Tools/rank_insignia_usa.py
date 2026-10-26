"""Every rank of the United States Army as a small game icon, for a rank system with more steps than
the three veterancy levels: enlisted E-1 to E-9 with Specialist and the E-8 and E-9 variants,
warrant officers W-1 to W-5, officers O-1 to O-10, and General of the Army.

Same look as rank_insignia.py: no plate, a black edge round every mark so it reads on sand and
grass, metal lit from the top.  Enlisted chevrons are gold stripes on a black patch cut to their
shape, the Army's dress pattern.  The game draws these twelve to fourteen pixels high beside the
health bar, no wider than half again their height, so each mark is fitted to fill its 64-pixel cell
and cut to what survives that: steep chevrons and fat rockers, the E-8 and E-9 devices big in the
middle of the stack in a colour each (silver, gold, red, blue), a solid eagle, veinless leaves,
short warrant bars with solid black squares, stars in clusters rather than rows, and one ringed
gold star for General of the Army.

    python Tools/rank_insignia_usa.py
        writes Data/Art/Textures/ReforgedRanksUSA.tga, an 8 by 4 atlas of 64-pixel cells, and
        Data/INI/MappedImages/HandCreated/ReforgedRanksUSA.ini that names each cell.
    python Tools/rank_insignia_usa.py preview <png>
        also writes a labelled sheet, every icon at 64, 16 and 12 pixels on sand and on grass.

Drawn big, fitted, outlined at eight times size and brought down, which is the antialiasing."""
import math
import os
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

from rank_insignia_cells import ink_entries

HERE = os.path.dirname(os.path.abspath(__file__))
NAME = "ReforgedRanksUSA"
CELL = 64
COLUMNS, ROWS = 8, 4
SCALE = 8          # outline and fit happen at CELL * SCALE
UNIT = 16          # design pixels per design unit; designs live in -32..32 units
OUTLINE = 3        # shipped pixels of black edge
MARGIN = 1
GOLD = ((255, 232, 120), (196, 134, 24))
SILVER = ((252, 252, 252), (138, 146, 160))
TEXTURE_PATH = os.path.join(HERE, "..", "Data", "Art", "Textures", NAME + ".tga")
INI_PATH = os.path.join(HERE, "..", "Data", "INI", "MappedImages", "HandCreated", NAME + ".ini")
INI_HEADER = """; Every rank of the United States Army, one mapped image per rank in one atlas.  Written by
; Tools/rank_insignia_usa.py with the texture it names; do not edit by hand.  Mapped images are not
; in the multiplayer INI checksum.
"""
INI_ENTRY = """
; %s, %s
MappedImage %s
  Texture = %s.tga
  TextureWidth = %d
  TextureHeight = %d
  Coords = Left:%d Top:%d Right:%d Bottom:%d
  Status = NONE
End
"""


def canvas():
    return Image.new("L", (64 * UNIT, 64 * UNIT), 0)


def px(points):
    """Design units to design pixels."""
    return [((x + 32) * UNIT, (y + 32) * UNIT) for x, y in points]


def box(x0, y0, x1, y1):
    return tuple(v for point in px([(x0, y0), (x1, y1)]) for v in point)


STAR_WAIST = 0.48  # inner radius over outer; the true 0.382 thins to a speck at twelve pixels


def star(draw, cx, cy, r, fill=255):
    points = []
    for i in range(10):
        angle = math.radians(-90 + 36 * i)
        reach = r if i % 2 == 0 else r * STAR_WAIST
        points.append((cx + reach * math.cos(angle), cy + reach * math.sin(angle)))
    draw.polygon(px(points), fill=fill)


def eagle(draw, cx, cy, s, fill=255):
    """A displayed eagle, wings raised, head turned to its right (the viewer's left)."""
    def at(points):
        return px([(cx + x * s, cy + y * s) for x, y in points])
    # solid wings and no beak: at twelve pixels the feathers and the turned head were a smudge
    for side in (-1, 1):
        wing = [(3, -3), (22, -14), (22, -2), (14, 2), (5, 6)]
        draw.polygon(at([(x * side, y) for x, y in wing]), fill=fill)
    draw.ellipse(box(cx - 5.5 * s, cy - 5 * s, cx + 5.5 * s, cy + 10 * s), fill=fill)
    draw.ellipse(box(cx - 4 * s, cy - 13 * s, cx + 4 * s, cy - 4 * s), fill=fill)
    draw.polygon(at([(-5, 8), (5, 8), (8, 15), (-8, 15)]), fill=fill)    # tail


# Enlisted stripes. Chevrons alone are steep and narrow, so a lone chevron is no wider than half again
# its height; over rockers they flatten and widen, since the stack is tall already and the rockers
# need the width. Rockers are fat with a wide gap, so one, two and three count at fourteen pixels.
STEEP, FLAT = (14, 14), (18, 7)    # half width and drop of a chevron arm
T, PITCH = 4.5, 6
ROCKER_GAP, ROCKER_T, ROCKER_PITCH, SAG = 3, 7, 11, 4
# E-8 and E-9: the device is what tells them apart, so it goes big in the middle of the stack, in a
# colour of its own, on one deep rocker that cradles it, in place of three rockers it had no room
# among
DEVICE_R = 11      # the device's half size
CRADLE_SAG = 15
RED = ((255, 120, 100), (170, 34, 24))
BLUE = ((150, 200, 255), (40, 86, 190))


def chevron(apex, w, d):
    return [(-w, apex + d), (0, apex), (w, apex + d), (w, apex + d + T), (0, apex + T), (-w, apex + d + T)]


def curve(end_y, w, sag=SAG, steps=24):
    return [(-w + 2 * w * i / steps, end_y + sag * (1 - ((-w + 2 * w * i / steps) / w) ** 2))
            for i in range(steps + 1)]


def stripes(chevrons, rockers, device=None):
    """Gold chevrons and rockers on their black patch, or with a device: three chevrons, the device
    under them and one deep rocker round it."""
    w, d = FLAT if rockers or device else STEEP
    gold, back = canvas(), canvas()
    g, b = ImageDraw.Draw(gold), ImageDraw.Draw(back)
    top = -30
    for k in range(chevrons):
        g.polygon(px(chevron(top + PITCH * k, w, d)), fill=255)
    low = top + PITCH * (chevrons - 1)
    if device:
        cy = low + T + d + ROCKER_GAP + DEVICE_R
        tips = cy - DEVICE_R * 0.3
        g.polygon(px(curve(tips, w, CRADLE_SAG) + curve(tips + ROCKER_T, w, CRADLE_SAG)[::-1]), fill=255)
        b.polygon(px([(-w, top + d), (0, top), (w, top + d)] + curve(tips + ROCKER_T, w, CRADLE_SAG)[::-1]),
                  fill=255)
        layer = canvas()
        draw_device, colours = device
        draw_device(ImageDraw.Draw(layer), cy)
        return gold, back, GOLD, layer, colours
    first_rocker = low + d + T + ROCKER_GAP
    for j in range(rockers):
        end_y = first_rocker + ROCKER_PITCH * j
        g.polygon(px(curve(end_y, w) + curve(end_y + ROCKER_T, w)[::-1]), fill=255)
    if rockers:
        bottom = curve(first_rocker + ROCKER_PITCH * (rockers - 1) + ROCKER_T, w)[::-1]
    else:
        bottom = [(w, low + d + T), (0, low + T), (-w, low + d + T)]
    b.polygon(px([(-w, top + d), (0, top), (w, top + d)] + bottom), fill=255)
    return gold, back, GOLD


def lozenge(draw, cy):
    r = DEVICE_R * 1.1
    draw.polygon(px([(0, cy - r), (r * 0.75, cy), (0, cy + r), (-r * 0.75, cy)]), fill=255)


def sgm_star(draw, cy):
    star(draw, 0, cy + 1, DEVICE_R * 1.1)


def csm_wreath(draw, cy):
    r = DEVICE_R
    draw.ellipse(box(-r, cy - r, r, cy + r), outline=255, width=round(3 * UNIT))
    star(draw, 0, cy + 0.5, r * 0.62)


def sma_stars(draw, cy):
    for x in (-7, 7):
        star(draw, x, cy + 1, DEVICE_R * 0.75)


def specialist():
    """The Specialist's eagle, big, on a tall patch with a pointed top and a round bottom, gold
    edged: the eagle fills the patch so the badge reads as a bird and not a disc."""
    gold, back = canvas(), canvas()
    bottom = [(x, 8 + 12 * (1 - (x / 18) ** 2)) for x, _ in curve(0, 18)][::-1]
    outer = [(0, -26), (18, -14), (18, 8)] + bottom + [(-18, 8), (-18, -14)]
    inner = [(x * 0.88, y * 0.9) for x, y in outer]
    ImageDraw.Draw(back).polygon(px(outer), fill=255)
    g = ImageDraw.Draw(gold)
    g.polygon(px(outer), fill=255)
    g.polygon(px(inner), fill=0)
    eagle(g, 0, 0, 0.68)
    return gold, back, GOLD


def bar(metal, holes=0, line=False):
    """A lieutenant's or warrant officer's bar; warrant bars carry black squares or a black line,
    solid black on the patch rather than holes the edge half filled in."""
    gold, back = canvas(), canvas()
    g = ImageDraw.Draw(gold)
    outline = box(-16.5, -10, 16.5, 10)
    g.rounded_rectangle(outline, radius=3 * UNIT, fill=255)
    ImageDraw.Draw(back).rounded_rectangle(outline, radius=3 * UNIT, fill=255)
    pitch, size = 7, 5.5
    for i in range(holes):
        x = (i - (holes - 1) / 2) * pitch
        g.rectangle(box(x - size / 2, -size / 2, x + size / 2, size / 2), fill=0)
    if line:
        g.rectangle(box(-13, -2.5, 13, 2.5), fill=0)
    return gold, back, metal


def captain():
    """Two silver bars joined at the ends, laid flat like the lieutenants' bars."""
    gold = canvas()
    g = ImageDraw.Draw(gold)
    for y in (-7, 7):
        g.rounded_rectangle(box(-13, y - 4.5, 13, y + 4.5), radius=2 * UNIT, fill=255)
    for x in (-8.5, 8.5):
        g.rectangle(box(x - 1.8, -3, x + 1.8, 3), fill=255)
    return gold, gold, SILVER


def oak_leaf(metal):
    gold = canvas()
    g = ImageDraw.Draw(gold)
    # squat and lobed, no midrib: the slim leaf with its vein came down to a dot
    g.ellipse(box(-12, -20, 12, 16), fill=255)
    for x, y, r in ((9, -14, 6), (13, -5, 7.5), (14, 5, 7.5), (11, 13, 6)):
        for side in (-1, 1):
            g.ellipse(box(side * x - r, y - r, side * x + r, y + r), fill=255)
    g.ellipse(box(-6, -25, 6, -14), fill=255)
    g.polygon(px([(-3, 16), (3, 16), (4, 25), (-4, 25)]), fill=255)    # stem
    return gold, gold, metal


def colonel():
    gold = canvas()
    eagle(ImageDraw.Draw(gold), 0, 0, 1.4)
    return gold, gold, SILVER


def stars(count):
    gold = canvas()
    g = ImageDraw.Draw(gold)
    # a tight cluster rather than a row: two on a slant, three in a triangle, four two over two
    layout = {1: [(0, 0)],
              2: [(-0.4, -0.3), (0.4, 0.3)],
              3: [(0, -0.45), (-0.5, 0.45), (0.5, 0.45)],
              4: [(-0.5, -0.5), (0.5, -0.5), (-0.5, 0.5), (0.5, 0.5)]}[count]
    pitch = 15
    for x, y in layout:
        star(g, x * pitch, y * pitch, 8.2)
    return gold, gold, SILVER


def general_of_the_army():
    """One big gold star in a thick ring: the real five-star circle came down to a grey disc."""
    gold = canvas()
    g = ImageDraw.Draw(gold)
    g.ellipse(box(-20, -20, 20, 20), outline=255, width=round(5 * UNIT))
    star(g, 0, 1.5, 13)
    return gold, gold, GOLD


def empty():
    """A Private (E-1) wears no insignia."""
    return canvas(), canvas(), GOLD


# image name, grade, title, design; the atlas fills row by row in this order
RANKS = [
    ("RankUSA_E1", "E-1", "Private (PV1)", empty),
    ("RankUSA_E2", "E-2", "Private (PV2)", lambda: stripes(1, 0)),
    ("RankUSA_E3", "E-3", "Private First Class", lambda: stripes(1, 1)),
    ("RankUSA_E4", "E-4", "Specialist", specialist),
    ("RankUSA_E4_CPL", "E-4", "Corporal", lambda: stripes(2, 0)),
    ("RankUSA_E5", "E-5", "Sergeant", lambda: stripes(3, 0)),
    ("RankUSA_E6", "E-6", "Staff Sergeant", lambda: stripes(3, 1)),
    ("RankUSA_E7", "E-7", "Sergeant First Class", lambda: stripes(3, 2)),
    ("RankUSA_E8", "E-8", "Master Sergeant", lambda: stripes(3, 3)),
    ("RankUSA_E8_1SG", "E-8", "First Sergeant", lambda: stripes(3, 1, (lozenge, SILVER))),
    ("RankUSA_E9", "E-9", "Sergeant Major", lambda: stripes(3, 1, (sgm_star, GOLD))),
    ("RankUSA_E9_CSM", "E-9", "Command Sergeant Major", lambda: stripes(3, 1, (csm_wreath, RED))),
    ("RankUSA_E9_SMA", "E-9", "Sergeant Major of the Army", lambda: stripes(3, 1, (sma_stars, BLUE))),
    ("RankUSA_W1", "W-1", "Warrant Officer 1", lambda: bar(SILVER, 1)),
    ("RankUSA_W2", "W-2", "Chief Warrant Officer 2", lambda: bar(SILVER, 2)),
    ("RankUSA_W3", "W-3", "Chief Warrant Officer 3", lambda: bar(SILVER, 3)),
    ("RankUSA_W4", "W-4", "Chief Warrant Officer 4", lambda: bar(SILVER, 4)),
    ("RankUSA_W5", "W-5", "Chief Warrant Officer 5", lambda: bar(SILVER, line=True)),
    ("RankUSA_O1", "O-1", "Second Lieutenant", lambda: bar(GOLD)),
    ("RankUSA_O2", "O-2", "First Lieutenant", lambda: bar(SILVER)),
    ("RankUSA_O3", "O-3", "Captain", captain),
    ("RankUSA_O4", "O-4", "Major", lambda: oak_leaf(GOLD)),
    ("RankUSA_O5", "O-5", "Lieutenant Colonel", lambda: oak_leaf(SILVER)),
    ("RankUSA_O6", "O-6", "Colonel", colonel),
    ("RankUSA_O7", "O-7", "Brigadier General", lambda: stars(1)),
    ("RankUSA_O8", "O-8", "Major General", lambda: stars(2)),
    ("RankUSA_O9", "O-9", "Lieutenant General", lambda: stars(3)),
    ("RankUSA_O10", "O-10", "General", lambda: stars(4)),
    ("RankUSA_O11_GA", "O-11", "General of the Army", general_of_the_army),
]


def icon(design):
    """One 64-pixel cell: the design fitted to the cell, lit, edged in black."""
    gold, back, colours, *device = design()
    silver, device_colours = device if device else (canvas(), SILVER)
    size = CELL * SCALE
    union = ImageChops.lighter(ImageChops.lighter(gold, back), silver)
    bbox = union.getbbox()
    if bbox is None:
        return Image.new("RGBA", (CELL, CELL), (0, 0, 0, 0))
    room = (CELL - 2 * (OUTLINE + MARGIN)) * SCALE
    fit = room / max(bbox[2] - bbox[0], bbox[3] - bbox[1])
    w, h = round((bbox[2] - bbox[0]) * fit), round((bbox[3] - bbox[1]) * fit)
    left, upper = (size - w) // 2, (size - h) // 2
    shapes = []
    for layer in (gold, silver, union):
        placed = Image.new("L", (size, size), 0)
        placed.paste(layer.crop(bbox).resize((w, h), Image.LANCZOS), (left, upper))
        shapes.append(placed)
    gold, silver, union = shapes

    def lit(top, bottom):
        metal = Image.new("RGB", (size, size))
        draw = ImageDraw.Draw(metal)
        for y in range(size):
            share = min(1.0, max(0.0, (y - upper) / h))
            draw.line((0, y, size, y), fill=tuple(round(a + (b - a) * share) for a, b in zip(top, bottom)))
        return metal

    # the black edge: a blur thresholded about two sigmas out, so OUTLINE pixels wide
    alpha = union.filter(ImageFilter.GaussianBlur(OUTLINE * SCALE / 2)).point(lambda v: 255 if v >= 6 else 0)
    art = Image.composite(lit(*colours), Image.new("RGB", (size, size), (0, 0, 0)), gold)
    art = Image.composite(lit(*device_colours), art, silver)
    art.putalpha(ImageChops.lighter(alpha, union))
    return art.resize((CELL, CELL), Image.LANCZOS)


def preview(atlas, path):
    """Each icon at 64, 16 and 12 pixels, on sand and on grass, with its grade and title."""
    tile_w, tile_h, columns = 330, 84, 3
    rows = (len(RANKS) + columns - 1) // columns
    sheet = Image.new("RGB", (tile_w * columns, tile_h * rows), (40, 40, 44))
    font = ImageFont.load_default(13)
    bold = ImageFont.load_default(15)
    draw = ImageDraw.Draw(sheet)
    for index, (name, grade, title, _) in enumerate(RANKS):
        x, y = (index % columns) * tile_w, (index // columns) * tile_h
        cell = atlas.crop(((index % COLUMNS) * CELL, (index // COLUMNS) * CELL,
                           (index % COLUMNS + 1) * CELL, (index // COLUMNS + 1) * CELL))
        for ground, gx in (((196, 172, 120), x + 4), ((78, 104, 52), x + 74)):
            draw.rectangle((gx, y + 4, gx + 66, y + tile_h - 5), fill=ground)
        sheet.paste(cell, (x + 5, y + 10), cell)
        for size, sy in ((16, y + 14), (12, y + 50)):
            small = cell.resize((size, size), Image.LANCZOS)
            for gx in (x + 76, x + 112):
                sheet.paste(small, (gx + (16 - size) // 2, sy), small)
        draw.text((x + 148, y + 12), grade + "  " + name, fill=(255, 255, 255), font=bold)
        draw.text((x + 148, y + 36), title, fill=(210, 210, 210), font=font)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    sheet.save(path)
    print(path)


def main():
    atlas = Image.new("RGBA", (CELL * COLUMNS, CELL * ROWS), (0, 0, 0, 0))
    ini_text = INI_HEADER
    for index, (name, grade, title, design) in enumerate(RANKS):
        x, y = (index % COLUMNS) * CELL, (index // COLUMNS) * CELL
        atlas.paste(icon(design), (x, y))
        ini_text += INI_ENTRY % (grade, title, name, NAME, atlas.width, atlas.height, x, y, x + CELL, y + CELL)
    # E-6 to E-9 (chevrons over rockers), the leaves, the eagle and the ringed star draw 14 high
    ini_text = ink_entries(atlas, ini_text, "America", tall=list(range(6, 13)) + [21, 22, 23, 28])
    atlas.save(TEXTURE_PATH)
    print(TEXTURE_PATH)
    with open(INI_PATH, "w", newline="\r\n") as ini:
        ini.write(ini_text)
    print(INI_PATH)
    if len(sys.argv) == 3 and sys.argv[1] == "preview":
        preview(atlas, sys.argv[2])


if __name__ == "__main__":
    main()
