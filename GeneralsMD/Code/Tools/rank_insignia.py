"""The rank marks America and GLA wear beside a unit's health bar, one for each rank: a gold bar for
America, stacked one under the other, and a skull for GLA, laid out like China's gold stars.

Both have a black edge and no background, so they read over any ground the way the score screen's
star does.  The game draws them twelve to fourteen pixels across, so the shapes are few and blunt.

    python Tools/rank_insignia.py
        writes Data/Art/Textures/ReforgedRankBar.tga, ReforgedRankSkull.tga and the
        ReforgedRankInsignia.ini that maps both.

Drawn sixteen times too big and brought down, which is the antialiasing."""
import os

from PIL import Image, ImageChops, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
SCALE = 16
OUTLINE = 41    # a MaxFilter size, odd: the black edge round every mark, 20 drawn pixels
BONE = (236, 228, 204)
GOLD_TOP = (255, 232, 120)
GOLD_BOTTOM = (196, 134, 24)
TEXTURES = os.path.join(HERE, "..", "Data", "Art", "Textures")
INI_PATH = os.path.join(HERE, "..", "Data", "INI", "MappedImages", "HandCreated", "ReforgedRankInsignia.ini")
INI_HEADER = """; The rank marks America and GLA wear beside a unit's health bar, one for each rank.  Written by
; Tools/rank_insignia.py with the textures it names; do not edit by hand.  Mapped images are not in
; the multiplayer INI checksum.
"""
INI_ENTRY = """
MappedImage %s
  Texture = %s.tga
  TextureWidth = %d
  TextureHeight = %d
  Coords = Left:0 Top:0 Right:%d Bottom:%d
  Status = NONE
End
"""


def scaled(box):
    """A box or point in shipped pixels, in drawn pixels."""
    return tuple(round(value * SCALE) for value in box)


def skull():
    """The bone of a 32-pixel skull with its holes cut, and its colour."""
    size = scaled((32, 32))
    bone = Image.new("L", size, 0)
    draw = ImageDraw.Draw(bone)
    draw.ellipse(scaled((4, 2, 28, 24)), fill=255)    # cranium
    draw.rounded_rectangle(scaled((9, 17, 23, 29)), radius=3 * SCALE, fill=255)    # jaw
    holes = Image.new("L", size, 0)
    draw = ImageDraw.Draw(holes)
    # big sockets: at twelve pixels the eyes are what makes it a skull and not a white dot
    draw.ellipse(scaled((6.5, 9, 14.5, 17.5)), fill=255)
    draw.ellipse(scaled((17.5, 9, 25.5, 17.5)), fill=255)
    draw.polygon([scaled((16, 17.5)), scaled((13.5, 21.5)), scaled((18.5, 21.5))], fill=255)    # nose
    for x in (13, 16, 19):    # gaps between the teeth
        draw.rectangle(scaled((x - 0.75, 24, x + 0.75, 29)), fill=255)
    shape = ImageChops.subtract(bone, holes)
    colour = Image.new("RGB", size, BONE)
    return shape, colour


def bar():
    """A 32 by 16 gold bar, light along the top and dark along the bottom like struck metal."""
    size = scaled((32, 16))
    shape = Image.new("L", size, 0)
    ImageDraw.Draw(shape).rounded_rectangle(scaled((3, 3, 29, 13)), radius=2 * SCALE, fill=255)
    colour = Image.new("RGB", size)
    draw = ImageDraw.Draw(colour)
    for y in range(size[1]):
        share = min(1.0, max(0.0, (y - 3 * SCALE) / (10 * SCALE)))
        draw.line((0, y, size[0], y), fill=tuple(round(top + (bottom - top) * share)
                                                for top, bottom in zip(GOLD_TOP, GOLD_BOTTOM)))
    return shape, colour


def finish(shape, colour):
    """The mark on black where the edge goes; holes stay inside the edge, so they come out black."""
    alpha = shape.filter(ImageFilter.MaxFilter(OUTLINE))
    black = Image.new("RGB", shape.size, (0, 0, 0))
    art = Image.composite(colour, black, shape)
    art.putalpha(alpha)
    return art.resize((shape.width // SCALE, shape.height // SCALE), Image.LANCZOS)


def main():
    ini_text = INI_HEADER
    for name, (shape, colour) in (("ReforgedRankBar", bar()), ("ReforgedRankSkull", skull())):
        art = finish(shape, colour)
        path = os.path.join(TEXTURES, name + ".tga")
        art.save(path)
        print(path)
        ini_text += INI_ENTRY % (name, name, art.width, art.height, art.width, art.height)
    with open(INI_PATH, "w", newline="\r\n") as ini:
        ini.write(ini_text)
    print(INI_PATH)


if __name__ == "__main__":
    main()
