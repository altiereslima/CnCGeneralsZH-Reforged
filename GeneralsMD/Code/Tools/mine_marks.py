"""The arming light that marks each China mine (ParticleSystemReforged.ini, MineFX and MineFXPurple):
red for the standard and cluster mines, a saturated blue for the EMP mines.  No mine body is drawn.

It is two pictures.  ReforgedMineLed*.tga is a small dot lying flat: a lens in the light's colour with
a near-white middle, inside a dark bezel, alpha blended so it keeps its colour over grass, sand or
snow.  ReforgedMineGlow*.tga is the light above it, drawn additively: a white-hot point in a
saturated glow.

    python Tools/mine_marks.py            # writes Data/Art/Textures/ReforgedMineLed*.tga and ReforgedMineGlow*.tga

Drawn four times too big and brought down, which is the antialiasing."""
import math
import os
import sys

from PIL import Image

DRAWN = 512
SHIPPED = 128
CENTRE = (DRAWN - 1) / 2.0
LED_HOT = 0.10			# radius of the near-white hot spot, as a share of the half width
LED_LENS = 0.50			# radius of the lit lens in the light's colour
LED_BEZEL_OUT = 0.74		# outer edge of the dark bezel round the lens
LED_BEZEL = (10, 8, 8)
LED_BEZEL_ALPHA = 0.9
GLOW_HOT = 0.12			# the light's white-hot point, as a share of the glow sprite's half width
GLOW_FALL = 0.30		# the glow round it falls off as exp(-(r - hot) / this)
LEDS = (("Red", (255, 28, 10), (255, 235, 225)), ("Blue", (40, 110, 255), (225, 240, 255)))
OUT_FOLDER = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Data", "Art", "Textures")


def ramp(edge0, edge1, x):
    t = min(1.0, max(0.0, (x - edge0) / (edge1 - edge0)))
    return t * t * (3.0 - 2.0 * t)


def main():
    out_folder = sys.argv[1] if len(sys.argv) > 1 else OUT_FOLDER
    for name, glow, core in LEDS:
        path = os.path.join(out_folder, "ReforgedMineLed%s.tga" % name)
        led(glow, core).resize((SHIPPED, SHIPPED), Image.LANCZOS).save(path)
        print(path)
        path = os.path.join(out_folder, "ReforgedMineGlow%s.tga" % name)
        shine(glow, core).resize((SHIPPED, SHIPPED), Image.LANCZOS).save(path)
        print(path)


def shine(colour, hot):
    """The light itself, drawn additively over the lens: a white-hot point, a saturated glow round it
    falling away to nothing at the sprite's edge."""
    image = Image.new("RGBA", (DRAWN, DRAWN))
    pixels = image.load()
    for py in range(DRAWN):
        y = (py - CENTRE) / CENTRE
        for px in range(DRAWN):
            x = (px - CENTRE) / CENTRE
            r = math.hypot(x, y)
            white = 1.0 - ramp(GLOW_HOT, GLOW_HOT + 0.06, r)
            strength = math.exp(-max(0.0, r - GLOW_HOT) / GLOW_FALL) * (1.0 - ramp(0.75, 1.0, r))
            value = tuple(int(min(255, (h * white + c * (1.0 - white)) * strength)) for h, c in zip(hot, colour))
            pixels[px, py] = value + (max(value),)
    return image


def led(lens, hot):
    image = Image.new("RGBA", (DRAWN, DRAWN))
    pixels = image.load()
    for py in range(DRAWN):
        y = (py - CENTRE) / CENTRE
        for px in range(DRAWN):
            x = (px - CENTRE) / CENTRE
            r = math.hypot(x, y)
            lit = ramp(LED_HOT, LED_HOT + 0.08, r)
            colour = tuple(h + (l - h) * lit for h, l in zip(hot, lens))
            bezel = ramp(LED_LENS - 0.02, LED_LENS + 0.02, r)
            colour = tuple(int(c + (b - c) * bezel) for c, b in zip(colour, LED_BEZEL))
            alpha = 1.0 - ramp(LED_BEZEL_OUT - 0.04, LED_BEZEL_OUT + 0.04, r)
            alpha *= 1.0 - (1.0 - LED_BEZEL_ALPHA) * bezel
            pixels[px, py] = colour + (int(255 * alpha),)
    return image


if __name__ == "__main__":
    main()
