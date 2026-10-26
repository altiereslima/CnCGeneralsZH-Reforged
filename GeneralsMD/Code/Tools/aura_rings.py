"""The ground rings under a Turkiye leader, one texture per aura kind (BalanceReforged.ini,
AuraRing_<kind> in the Veterancy block).

Every ring is drawn at the full aura radius, and each kind owns a fixed band of that one disc, so
any set of them lies as one dial: never on top of each other, the same line weight and spacing
whichever grades are there. The kinds are told apart by pattern, the colour only tints them:

    rate of fire   0.935-0.970   sixty short even ticks; every leader with an aura has it, so the
                                 outermost band is the reach itself
    sight          0.875         a fine dotted line
    guard          0.805         a thin line broken by eight small notches
    range          0.695-0.765   a graduated scale: a line with a long mark every sixth tick
    rally          inside 0.66   a faint wash rising toward its edge, the middle left clear

They are drawn white on black for SHADOW_ADDITIVE_DECAL; the INI's Color and Opacity set how much
light each adds. The texture edge stays black, or clamping smears it across the ground.

    python Tools/aura_rings.py            # writes Data/Art/Textures/ReforgedAura<Kind>.tga

Drawn four times too big and brought down, which is the antialiasing."""
import math
import os
import sys

import numpy as np
from PIL import Image

SHIPPED = 512
DRAWN = SHIPPED * 4
LINE = 0.011            # every line's width, as a share of the radius
OUT_FOLDER = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Data", "Art", "Textures")


def polar():
    c = (DRAWN - 1) / 2.0
    y, x = np.mgrid[0:DRAWN, 0:DRAWN].astype(np.float64)
    x = (x - c) / (DRAWN / 2.0)
    y = (y - c) / (DRAWN / 2.0)
    return np.hypot(x, y), np.arctan2(y, x)


def arc_to_nearest(r, a, count):
    """Distance along the circle from each pixel to the nearest of count evenly spaced spokes."""
    step = 2 * math.pi / count
    return np.abs(a - np.round(a / step) * step) * r


def ring(r, at):
    return np.abs(r - at) < LINE / 2


def ramp(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def rate_of_fire(r, a):
    return (r > 0.935) & (r < 0.970) & (arc_to_nearest(r, a, 60) < LINE / 2)


def sight(r, a):
    at, count = 0.875, 96
    step = 2 * math.pi / count
    nearest = np.round(a / step) * step
    return np.hypot(r * np.cos(a) - at * np.cos(nearest), r * np.sin(a) - at * np.sin(nearest)) < LINE * 0.7


def guard(r, a):
    return ring(r, 0.805) & (arc_to_nearest(r, a + math.pi / 8, 8) > 0.02)


def range_scale(r, a):
    tick = np.round(a / (2 * math.pi / 72)).astype(np.int64) % 72
    inner = np.where(tick % 6 == 0, 0.695, 0.735)
    marks = (r > inner) & (r < 0.765) & (arc_to_nearest(r, a, 72) < LINE / 2)
    return marks | ring(r, 0.765)


def rally(r, a):
    return 0.25 * ramp(0.30, 0.62, r) * (1.0 - ramp(0.62, 0.66, r))


KINDS = (("RateOfFire", rate_of_fire), ("Sight", sight), ("Guard", guard), ("Range", range_scale), ("Rally", rally))


def main():
    out_folder = sys.argv[1] if len(sys.argv) > 1 else OUT_FOLDER
    r, a = polar()
    for name, draw in KINDS:
        big = Image.fromarray(np.round(draw(r, a).astype(np.float64) * 255).astype(np.uint8), "L")
        grey = big.resize((SHIPPED, SHIPPED), Image.BOX)
        path = os.path.join(out_folder, "ReforgedAura%s.tga" % name)
        Image.merge("RGBA", (grey, grey, grey, grey)).save(path, compression="tga_rle")
        print(path)


if __name__ == "__main__":
    main()
