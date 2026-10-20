"""The start spot on a staging room's map preview and the load screen's map, cut from EA's own.

EA's PlayerStart is a blue ring round an opaque black middle with a drop shadow, 25 pixels square in
SCSmShellUserInterface512_001.tga.  This cuts it in two: ReforgedLobbySpotRing is the ring and the
shadow with the middle taken out, and ReforgedLobbySpot is the middle in white.  The game draws the
middle multiplied by the spot's colour and the ring over it untinted (LobbyStartSpotFill in
GUIUtil.cpp), so a taken spot is EA's art with its black turned into that colour, the ring's inner
edge blending into it as it blended into the black, and the seat's number goes on top.

The ring's inner edge is the blue fading to black over a few pixels.  Each of those pixels is read as
the pure blue at some coverage (its blue channel over the pure ring's), and the ring layer keeps that
coverage as alpha, so middle-then-ring rebuilds the original exactly when the middle is black.

    python Tools/lobby_spot.py
        reads Run/EnglishZH.big, writes Data/Art/Textures/ReforgedLobbySpot.tga and the
        ReforgedLobbySpot.ini that maps both halves."""
import io
import math
import os
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from bigfile import _read_index

ARCHIVE_PATH = os.path.join(HERE, "..", "..", "Run", "EnglishZH.big")
TEXTURE_MEMBER = "data\\english\\art\\textures\\scsmshelluserinterface512_001.tga"
# PlayerStart's Coords in SCSmShellUserInterface512.INI
SOURCE_BOX = (337, 415, 362, 440)
CELL = 25
# the ring is 22 pixels across from the cell's corner, the shadow beyond it
RING_CENTRE = 10.5
# every pixel nearer the centre than this is middle or the ring's inner edge; the pure ring starts here
MIDDLE_RADIUS = 8.6
# the blue channel of the ring's pure inner pixels; darker is the edge fading into the black
RING_BLUE = 244
TEXTURE_WIDTH, TEXTURE_HEIGHT = 64, 32
MIDDLE_CORNER = (0, 0)
RING_CORNER = (32, 0)
# a black middle rebuilds EA's art within this much per channel (rounding of the coverage)
REBUILD_TOLERANCE = 3
OUT_PATH = os.path.join(HERE, "..", "Data", "Art", "Textures", "ReforgedLobbySpot.tga")
INI_PATH = os.path.join(HERE, "..", "Data", "INI", "MappedImages", "HandCreated", "ReforgedLobbySpot.ini")
INI_TEXT = """; A start spot cut from EA's PlayerStart: the middle in white, tinted with the spot's colour as it
; is drawn, and the ring with the middle taken out, drawn over it untinted.  Written by
; Tools/lobby_spot.py with Art/Textures/ReforgedLobbySpot.tga; do not edit by hand.  Mapped images
; are not in the multiplayer INI checksum.

MappedImage ReforgedLobbySpot
  Texture = ReforgedLobbySpot.tga
  TextureWidth = %d
  TextureHeight = %d
  Coords = Left:%d Top:%d Right:%d Bottom:%d
  Status = NONE
End

MappedImage ReforgedLobbySpotRing
  Texture = ReforgedLobbySpot.tga
  TextureWidth = %d
  TextureHeight = %d
  Coords = Left:%d Top:%d Right:%d Bottom:%d
  Status = NONE
End
""" % (TEXTURE_WIDTH, TEXTURE_HEIGHT, MIDDLE_CORNER[0], MIDDLE_CORNER[1], MIDDLE_CORNER[0] + CELL, MIDDLE_CORNER[1] + CELL,
       TEXTURE_WIDTH, TEXTURE_HEIGHT, RING_CORNER[0], RING_CORNER[1], RING_CORNER[0] + CELL, RING_CORNER[1] + CELL)


def player_start():
    with open(ARCHIVE_PATH, "rb") as archive:
        for path, offset, size in list(_read_index(archive)):
            if path.lower() == TEXTURE_MEMBER:
                archive.seek(offset)
                texture = Image.open(io.BytesIO(archive.read(size))).convert("RGBA")
                return texture.crop(SOURCE_BOX)
    raise ValueError("%s has no %s" % (ARCHIVE_PATH, TEXTURE_MEMBER))


def split(art):
    middle = Image.new("RGBA", (CELL, CELL), (255, 255, 255, 0))
    ring = art.copy()
    for y in range(CELL):
        for x in range(CELL):
            if math.hypot(x - RING_CENTRE, y - RING_CENTRE) > MIDDLE_RADIUS:
                continue
            red, green, blue, alpha = art.getpixel((x, y))
            coverage = min(1.0, blue / RING_BLUE)
            middle.putpixel((x, y), (255, 255, 255, 255))
            if coverage == 0:
                ring.putpixel((x, y), (0, 0, 0, 0))
                continue
            ring.putpixel((x, y), (min(255, round(red / coverage)), min(255, round(green / coverage)),
                                   min(255, round(blue / coverage)), round(alpha * coverage)))
    return middle, ring


def tinted(middle, ring, colour):
    painted = Image.new("RGBA", (CELL, CELL), colour + (255,))
    painted.putalpha(middle.getchannel("A"))
    spot = Image.new("RGBA", (CELL, CELL), (0, 0, 0, 0))
    spot.alpha_composite(painted)
    spot.alpha_composite(ring)
    return spot


def check(art, middle, ring):
    rebuilt = tinted(middle, ring, (0, 0, 0))
    for y in range(CELL):
        for x in range(CELL):
            if math.hypot(x - RING_CENTRE, y - RING_CENTRE) > MIDDLE_RADIUS:
                continue
            worst = max(abs(a - b) for a, b in zip(rebuilt.getpixel((x, y)), art.getpixel((x, y))))
            if worst > REBUILD_TOLERANCE:
                raise ValueError("a black middle rebuilds pixel %d,%d %d off EA's art, expected at most %d"
                                 % (x, y, worst, REBUILD_TOLERANCE))
    # nothing dark may be left in the ring layer beside the middle: it would show as a black rim
    for y in range(CELL):
        for x in range(CELL):
            distance = math.hypot(x - RING_CENTRE, y - RING_CENTRE)
            if MIDDLE_RADIUS < distance <= MIDDLE_RADIUS + 1 and ring.getpixel((x, y))[2] < RING_BLUE * 0.8:
                raise ValueError("pixel %d,%d just outside the middle is %r, expected the pure ring"
                                 % (x, y, ring.getpixel((x, y))))


def main():
    art = player_start()
    middle, ring = split(art)
    check(art, middle, ring)
    texture = Image.new("RGBA", (TEXTURE_WIDTH, TEXTURE_HEIGHT), (0, 0, 0, 0))
    texture.paste(middle, MIDDLE_CORNER)
    texture.paste(ring, RING_CORNER)
    texture.save(OUT_PATH)
    with open(INI_PATH, "w", newline="\r\n") as ini:
        ini.write(INI_TEXT)
    print(OUT_PATH)
    print(INI_PATH)


if __name__ == "__main__":
    main()
