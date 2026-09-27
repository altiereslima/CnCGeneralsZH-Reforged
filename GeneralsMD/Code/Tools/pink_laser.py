"""The pink the Superweapon General's dying Particle Uplink throws its lasers in.  EA's lasers are one
model, ABSDILink_L, textured with a 16 pixel strip that runs dark, blue, white at the middle, blue,
dark across the beam; the SupW_SDILasers reskin draws that model with this strip in its place.  Red
follows the blue, so the glow turns magenta like the SupW cannon's beam and the core stays white.

    python Tools/pink_laser.py            # writes Data/Art/Textures/ReforgedLaserPink.tga"""
import os
import sys

from PIL import Image

SIZE = 16
MIDDLE = SIZE // 2
GLOW_WIDTH = 4.63	# columns from the edge before the magenta is at full strength
CORE_FALLOFF = 1.6	# how fast the white core gives way to the glow
FULL = 255
OUT_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Data", "Art", "Textures", "ReforgedLaserPink.tga")


def column_colour(column):
    distance = abs(column - MIDDLE)
    glow = round(FULL * min(1.0, (MIDDLE - distance) / GLOW_WIDTH))
    core = round(FULL * (1.0 - distance / MIDDLE) ** CORE_FALLOFF)
    return (glow, core, glow)


def strip():
    image = Image.new("RGB", (SIZE, SIZE))
    image.putdata([column_colour(column) for row in range(SIZE) for column in range(SIZE)])
    return image


def selfcheck():
    assert column_colour(MIDDLE) == (FULL, FULL, FULL), column_colour(MIDDLE)
    assert column_colour(0) == (0, 0, 0), column_colour(0)
    red, green, blue = column_colour(MIDDLE - 3)
    assert red == blue == FULL and green < FULL, (red, green, blue)


def main():
    selfcheck()
    out_path = sys.argv[1] if len(sys.argv) > 1 else OUT_PATH
    strip().save(out_path)
    print(out_path)


if __name__ == "__main__":
    main()
