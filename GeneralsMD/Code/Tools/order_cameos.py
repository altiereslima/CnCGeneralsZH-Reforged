"""The command bar's attack, hold position, move and stance cameos,
painted in the manner of EA's command cameos (SSStop, SSGuard): a sign with a bevelled steel rim and
four bolts standing over a painted backdrop.  No command set has a button for any of these orders,
so the game has no picture for them.

Attack is a red plate with a white crosshair over a burning sky, hold position a blue plate with a
raised palm over a blue one, move a green diamond with a white arrow over an overcast field.  The
aggressive stance is a red pennant of two chevrons pointing ahead and the defensive one a steel blue
shield.  Each is painted at four times its size and brought down,
which is
what softens the edges into the look of the shipped art.  The noise is seeded, so the file comes
out the same every time.

    python Tools/order_cameos.py             # writes Data/Art/Textures/ReforgedOrders.tga
    python Tools/order_cameos.py selfcheck

The mapped images are Data/INI/MappedImages/HandCreated/ReforgedOrders.ini, whose coordinates are
CAMEO_PLACES below."""
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

CAMEO_W, CAMEO_H = 60, 48           # every command cameo in the shipped mapped images
SUPER = 4                           # painted at this many times the size
TEXTURE_W, TEXTURE_H = 256, 128
CAMEO_PLACES = {"attack": (0, 0), "hold": (64, 0), "move": (0, 64),
                "aggressive": (192, 0), "defensive": (128, 64)}
SEED = 20260928
OUT_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Data", "Art", "Textures", "ReforgedOrders.tga")

W, H = CAMEO_W * SUPER, CAMEO_H * SUPER


def value_noise(rng, octaves):
    """Clouds: random grids of growing size, each blown up smooth and added at half the last's weight."""
    field = np.zeros((H, W))
    weight, total = 1.0, 0.0
    for octave in range(octaves):
        cells = 3 * 2 ** octave
        grid = rng.random((cells, cells + cells // 2))
        layer = np.asarray(Image.fromarray((grid * 255).astype(np.uint8)).resize((W, H), Image.BICUBIC), dtype=float) / 255
        field += layer * weight
        total += weight
        weight /= 2
    return field / total


def backdrop(rng, top, bottom, cloud, ground, glow=None):
    """A sky from `top` to `bottom` with clouds of `cloud`, a ragged dark ground along the foot and,
    if given, a fire's glow rising off it."""
    rows = np.linspace(0, 1, H)[:, None, None]
    sky = np.asarray(top, float) * (1 - rows) + np.asarray(bottom, float) * rows
    sky = np.broadcast_to(sky, (H, W, 3)).copy()
    clouds = np.clip((value_noise(rng, 5) - 0.42) * 2.6, 0, 1)[:, :, None]
    sky = sky * (1 - clouds * 0.7) + np.asarray(cloud, float) * clouds * 0.7
    if glow is not None:
        heat = np.clip(rows[:, :, 0] * 1.6 - 0.55, 0, 1) * np.clip(value_noise(rng, 4) * 1.6 - 0.3, 0, 1)
        sky = sky * (1 - heat[:, :, None]) + np.asarray(glow, float) * heat[:, :, None]
    image = Image.fromarray(np.clip(sky, 0, 255).astype(np.uint8), "RGB")
    draw = ImageDraw.Draw(image)
    ridge = value_noise(rng, 3)[0]
    points = [(x, int(H * 0.86 - ridge[x] * H * 0.1)) for x in range(0, W, 4)] + [(W, H), (0, H)]
    draw.polygon(points, fill=ground)
    return image


def rim_colours(angle):
    """Steel lit from the top left: a bevel's colour at `angle` round it, 0 at the top left."""
    light = 0.5 + 0.5 * math.cos(angle)
    return tuple(int(90 + 150 * light) for _ in range(3))


def bolt(draw, x, y, radius):
    draw.ellipse((x - radius, y - radius, x + radius, y + radius), fill=(60, 64, 70))
    draw.ellipse((x - radius + 2, y - radius + 2, x + radius - 3, y + radius - 3), fill=(215, 220, 226))


def shadow_under(image, mask, offset):
    """A soft dark copy of `mask` under the sign, down and to the right."""
    blurred = mask.filter(ImageFilter.GaussianBlur(SUPER * 1.5))
    shifted = Image.new("L", mask.size)
    shifted.paste(blurred, (offset, offset))
    darkened = Image.new("RGB", image.size, (0, 0, 0))
    image.paste(darkened, (0, 0), shifted.point(lambda v: v * 0.6))


def plate(image, outline, face, bolts):
    """A sign: `outline` the points of its edge, a steel rim round a `face` of colour, bolts at
    `bolts`.  The rim is a ring of short strokes each shaded by the way it faces."""
    mask = Image.new("L", image.size)
    ImageDraw.Draw(mask).polygon(outline, fill=255)
    shadow_under(image, mask, SUPER * 2)
    draw = ImageDraw.Draw(image)
    draw.polygon(outline, fill=(150, 155, 162))
    count = len(outline)
    for each in range(count):
        a, b = outline[each], outline[(each + 1) % count]
        facing = math.atan2(-(b[0] - a[0]), b[1] - a[1]) - math.radians(135)
        draw.line((a, b), fill=rim_colours(facing), width=SUPER * 3)
    centre = (sum(p[0] for p in outline) / count, sum(p[1] for p in outline) / count)
    inner = [(centre[0] + (x - centre[0]) * 0.84, centre[1] + (y - centre[1]) * 0.84) for x, y in outline]
    draw.polygon(inner, fill=(30, 30, 34))
    face_points = [(centre[0] + (x - centre[0]) * 0.8, centre[1] + (y - centre[1]) * 0.8) for x, y in outline]
    shade = Image.new("L", image.size)
    ImageDraw.Draw(shade).polygon(face_points, fill=255)
    rows = np.linspace(1.18, 0.78, H)[:, None, None]
    lit = np.clip(np.broadcast_to(np.asarray(face, float), (H, W, 3)) * rows, 0, 255).astype(np.uint8)
    image.paste(Image.fromarray(lit, "RGB"), (0, 0), shade)
    for x, y in bolts:
        bolt(draw, x, y, SUPER * 2)
    return centre


def circle_points(cx, cy, radius, count=48):
    return [(cx + radius * math.cos(2 * math.pi * i / count), cy + radius * math.sin(2 * math.pi * i / count))
            for i in range(count)]


def attack(rng):
    image = backdrop(rng, (70, 40, 34), (190, 90, 40), (120, 110, 104), (34, 26, 20), glow=(255, 150, 40))
    cx, cy, radius = W / 2, H / 2 - SUPER, H * 0.44
    diagonal = radius * 0.62
    plate(image, circle_points(cx, cy, radius), (205, 32, 28),
          [(cx - diagonal, cy - diagonal), (cx + diagonal, cy - diagonal),
           (cx - diagonal, cy + diagonal), (cx + diagonal, cy + diagonal)])
    draw = ImageDraw.Draw(image)
    white = (248, 246, 240)
    ring = radius * 0.46
    thick = SUPER * 3
    draw.ellipse((cx - ring, cy - ring, cx + ring, cy + ring), outline=white, width=thick)
    reach, gap = radius * 0.72, radius * 0.18
    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        draw.line((cx + dx * gap, cy + dy * gap, cx + dx * reach, cy + dy * reach), fill=white, width=thick)
    dot = SUPER * 2
    draw.ellipse((cx - dot, cy - dot, cx + dot, cy + dot), fill=white)
    return image


def hold(rng):
    image = backdrop(rng, (70, 110, 170), (170, 190, 205), (235, 238, 240), (52, 48, 38))
    left, top, right, bottom = W * 0.2, SUPER * 2, W * 0.8, H - SUPER * 5
    cut = SUPER * 6
    outline = [(left + cut, top), (right - cut, top), (right, top + cut), (right, bottom - cut),
               (right - cut, bottom), (left + cut, bottom), (left, bottom - cut), (left, top + cut)]
    inset = SUPER * 5
    cx, cy = plate(image, outline, (36, 84, 190),
                   [(left + inset, top + inset), (right - inset, top + inset),
                    (left + inset, bottom - inset), (right - inset, bottom - inset)])
    # a raised palm: the palm, four fingers and the thumb, each a rounded bar
    draw = ImageDraw.Draw(image)
    white = (248, 246, 240)
    scale = (bottom - top) / 125.0
    cy -= 4 * scale
    palm = (cx - 20 * scale, cy - 2 * scale, cx + 20 * scale, cy + 34 * scale)
    draw.rounded_rectangle(palm, radius=10 * scale, fill=white)
    fingers = [(-15.5, 42), (-5.2, 50), (5.2, 48), (15.5, 40)]
    for x, length in fingers:
        width = 8 * scale
        draw.rounded_rectangle((cx + x * scale - width / 2, cy + 6 * scale - length * scale,
                                cx + x * scale + width / 2, cy + 8 * scale), radius=width / 2, fill=white)
    thumb = Image.new("L", image.size)
    ImageDraw.Draw(thumb).rounded_rectangle((cx + 12 * scale, cy - 4 * scale, cx + 22 * scale, cy + 28 * scale),
                                            radius=5 * scale, fill=255)
    thumb = thumb.rotate(-35, center=(cx + 17 * scale, cy + 26 * scale), resample=Image.BICUBIC)
    image.paste(Image.new("RGB", image.size, white), (0, 0), thumb)
    return image


def move(rng):
    image = backdrop(rng, (96, 112, 104), (176, 184, 160), (214, 218, 206), (46, 52, 34))
    cx, cy = W / 2, H / 2 - SUPER
    reach = H * 0.47
    inset = reach * 0.66
    cx, cy = plate(image, [(cx, cy - reach), (cx + reach, cy), (cx, cy + reach), (cx - reach, cy)], (40, 150, 60),
                   [(cx, cy - inset), (cx + inset, cy), (cx, cy + inset), (cx - inset, cy)])
    # a road sign's arrow, pointing ahead: a shaft and a head, rounded off by the resize
    draw = ImageDraw.Draw(image)
    white = (248, 246, 240)
    scale = reach / 60.0
    draw.rectangle((cx - 6 * scale, cy - 2 * scale, cx + 6 * scale, cy + 22 * scale), fill=white)
    draw.polygon([(cx, cy - 25 * scale), (cx + 19 * scale, cy), (cx - 19 * scale, cy)], fill=white)
    return image


def aggressive(rng):
    image = backdrop(rng, (80, 34, 30), (196, 84, 44), (130, 104, 96), (34, 24, 20), glow=(255, 140, 40))
    cx, top, bottom, half = W / 2, SUPER * 2, H - SUPER * 5, W * 0.3
    inset = SUPER * 6
    plate(image, [(cx, top), (cx + half, bottom), (cx - half, bottom)], (200, 28, 26),
          [(cx, top + inset * 1.8), (cx + half - inset * 1.6, bottom - inset * 0.7), (cx - half + inset * 1.6, bottom - inset * 0.7)])
    # two chevrons pointing ahead
    draw = ImageDraw.Draw(image)
    white = (248, 246, 240)
    scale = (bottom - top) / 125.0
    mid = (top + bottom) / 2 + 14 * scale
    for lift in (0, 26):
        apex = mid - lift * scale - 14 * scale
        draw.line([(cx - 22 * scale, apex + 20 * scale), (cx, apex), (cx + 22 * scale, apex + 20 * scale)],
                  fill=white, width=SUPER * 3, joint="curve")
    return image


def defensive(rng):
    image = backdrop(rng, (58, 84, 120), (150, 172, 190), (220, 228, 236), (44, 46, 42))
    cx, top, bottom, half = W / 2, SUPER * 2, H - SUPER * 3, W * 0.27
    shoulder = top + (bottom - top) * 0.5
    inset = SUPER * 5
    plate(image, [(cx - half, top), (cx + half, top), (cx + half, shoulder), (cx, bottom), (cx - half, shoulder)],
          (54, 104, 168),
          [(cx - half + inset, top + inset), (cx + half - inset, top + inset),
           (cx - half + inset, shoulder - SUPER), (cx + half - inset, shoulder - SUPER)])
    # a smaller shield inside, outlined, with a bar down it
    draw = ImageDraw.Draw(image)
    white = (248, 246, 240)
    small_half, small_top, small_bottom = half * 0.45, top + (bottom - top) * 0.22, bottom - (bottom - top) * 0.2
    small_shoulder = small_top + (small_bottom - small_top) * 0.5
    inner = [(cx - small_half, small_top), (cx + small_half, small_top), (cx + small_half, small_shoulder),
             (cx, small_bottom), (cx - small_half, small_shoulder)]
    draw.line(inner + [inner[0]], fill=white, width=SUPER * 3, joint="curve")
    draw.line((cx, small_top, cx, small_bottom - SUPER * 2), fill=white, width=SUPER * 3)
    return image


def cameo(painted):
    small = painted.resize((CAMEO_W, CAMEO_H), Image.LANCZOS)
    return small.filter(ImageFilter.UnsharpMask(radius=1, percent=60, threshold=2))


def texture():
    rng = np.random.default_rng(SEED)
    sheet = Image.new("RGBA", (TEXTURE_W, TEXTURE_H), (0, 0, 0, 0))
    # in this order: the stream is shared, so a new cameo goes last and leaves the others as they were
    for name, paint in (("attack", attack), ("hold", hold), ("move", move),
                        ("aggressive", aggressive), ("defensive", defensive)):
        sheet.paste(cameo(paint(rng)).convert("RGBA"), CAMEO_PLACES[name])
    return sheet


def selfcheck():
    sheet = texture()
    assert sheet.size == (TEXTURE_W, TEXTURE_H)
    for name, (x, y) in CAMEO_PLACES.items():
        picture = np.asarray(sheet.crop((x, y, x + CAMEO_W, y + CAMEO_H)), dtype=int)
        assert picture[:, :, 3].min() == 255, name       # a whole cameo, no hole in it
        assert picture[:, :, :3].std() > 30, name        # a picture, not a flat fill
    # a red plate, a blue one and a green one: the three read apart at a glance
    attack_mid = np.asarray(sheet.crop((20, 10, 40, 30)), dtype=int)[:, :, :3].mean(axis=(0, 1))
    hold_mid = np.asarray(sheet.crop((64 + 12, 6, 64 + 20, 14)), dtype=int)[:, :, :3].mean(axis=(0, 1))
    move_side = np.asarray(sheet.crop((17, 64 + 25, 23, 64 + 29)), dtype=int)[:, :, :3].mean(axis=(0, 1))
    assert attack_mid[0] > attack_mid[2], attack_mid
    assert hold_mid[2] > hold_mid[0], hold_mid
    assert move_side[1] > move_side[0] and move_side[1] > move_side[2], move_side
    # and the two stances: red and steel blue plates
    def face(x0, y0, x1, y1):
        return np.asarray(sheet.crop((x0, y0, x1, y1)), dtype=int)[:, :, :3].mean(axis=(0, 1))
    aggressive_foot = face(192 + 27, 34, 192 + 33, 38)
    defensive_top = face(128 + 16, 64 + 8, 128 + 20, 64 + 14)
    assert aggressive_foot[0] > 140 and aggressive_foot[1] < 60, aggressive_foot
    assert defensive_top[2] > defensive_top[0] + 50, defensive_top
    assert np.array_equal(np.asarray(texture()), np.asarray(sheet))   # seeded: the same file every time
    print("order_cameos selfcheck ok")


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "selfcheck":
        selfcheck()
        return
    texture().save(OUT_PATH)
    print(OUT_PATH)


if __name__ == "__main__":
    main()
