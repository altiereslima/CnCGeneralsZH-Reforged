"""The ground marks a smoke signal leaves: crossed swords for attack, a shield for defend, an eye for
look, each in a ring.  The game lays them on the terrain as decals tinted the sender's colour, so they
are white where the colour goes and black round the edge so they read on any ground.

    python Tools/signal_marks.py            # writes Data/Art/Textures/ReforgedSignal*.tga

Drawn four times too big and brought down, which is the antialiasing."""
import math
import os
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter

DRAWN = 512
SHIPPED = 128
CENTRE = DRAWN // 2
OUTLINE = 17	# a MaxFilter size, odd: the black edge round every white shape, 8 drawn pixels
GAP = 13	# the black kept between two swords where they cross
RING_OUTER = 236
RING_INNER = 204
INSIDE_ALPHA = 80	# the ring's inside darkened a little, so the mark reads on bright ground
OUT_FOLDER = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Data", "Art", "Textures")


def blank():
    return Image.new("L", (DRAWN, DRAWN), 0)


def disc(mask, radius, fill):
    ImageDraw.Draw(mask).ellipse((CENTRE - radius, CENTRE - radius, CENTRE + radius, CENTRE + radius), fill=fill)


def polygon(mask, points, fill, turn=0.0):
    cosine, sine = math.cos(turn), math.sin(turn)
    placed = [(CENTRE + x * cosine - y * sine, CENTRE + x * sine + y * cosine) for x, y in points]
    ImageDraw.Draw(mask).polygon(placed, fill=fill)


def sword(turn):
    mask = blank()
    polygon(mask, [(-19, -125), (0, -172), (19, -125), (19, 62), (-19, 62)], 255, turn)
    polygon(mask, [(-64, 62), (64, 62), (64, 86), (-64, 86)], 255, turn)
    polygon(mask, [(-12, 86), (12, 86), (12, 136), (-12, 136)], 255, turn)
    cosine, sine = math.cos(turn), math.sin(turn)
    x, y = CENTRE - 152 * sine, CENTRE + 152 * cosine
    ImageDraw.Draw(mask).ellipse((x - 21, y - 21, x + 21, y + 21), fill=255)
    return mask


def swords():
    under, over = sword(math.radians(-45)), sword(math.radians(45))
    return ImageChops.lighter(ImageChops.subtract(under, over.filter(ImageFilter.MaxFilter(GAP))), over)


def shield_outline(scale, lift):
    points = [(-120, -140), (120, -140), (120, 10)]
    points += [(120 * math.cos(step / 20 * math.pi / 2), 10 + 160 * math.sin(step / 20 * math.pi / 2)) for step in range(1, 20)]
    points += [(0, 170)]
    points += [(-x, y) for x, y in reversed(points[2:-1])]
    return [(x * scale, y * scale + lift) for x, y in points]


def shield():
    mask = blank()
    polygon(mask, shield_outline(1.0, 0), 255)
    polygon(mask, shield_outline(0.78, -4), 0)
    polygon(mask, shield_outline(0.6, -8), 255)
    return mask


def lens(mask, half_width, half_height, fill):
    # two circles through the lens's corners, the one below giving its top edge and the one above its bottom
    radius = (half_width ** 2 + half_height ** 2) / (2 * half_height)
    top, bottom = blank(), blank()
    ImageDraw.Draw(top).ellipse((CENTRE - radius, CENTRE - half_height, CENTRE + radius, CENTRE - half_height + 2 * radius), fill=255)
    ImageDraw.Draw(bottom).ellipse((CENTRE - radius, CENTRE + half_height - 2 * radius, CENTRE + radius, CENTRE + half_height), fill=255)
    shape = ImageChops.darker(top, bottom)
    return ImageChops.lighter(mask, shape) if fill else ImageChops.subtract(mask, shape)


def eye():
    mask = lens(blank(), 168, 88, True)
    mask = lens(mask, 138, 62, False)
    disc(mask, 58, 255)
    disc(mask, 24, 0)
    return mask


def finish(icon):
    white = blank()
    disc(white, RING_OUTER, 255)
    disc(white, RING_INNER, 0)
    white = ImageChops.lighter(white, icon)
    alpha = white.filter(ImageFilter.MaxFilter(OUTLINE))
    inside = blank()
    disc(inside, RING_INNER, INSIDE_ALPHA)
    alpha = ImageChops.lighter(alpha, inside)
    mark = Image.merge("RGBA", (white, white, white, alpha))
    return mark.resize((SHIPPED, SHIPPED), Image.LANCZOS)


def main():
    out_folder = sys.argv[1] if len(sys.argv) > 1 else OUT_FOLDER
    for name, icon in (("Attack", swords()), ("Defend", shield()), ("Look", eye())):
        path = os.path.join(out_folder, "ReforgedSignal%s.tga" % name)
        finish(icon).save(path)
        print(path)


if __name__ == "__main__":
    main()
