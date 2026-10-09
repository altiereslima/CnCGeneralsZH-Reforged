"""The Chinese command bar's beacon button with the alpha EA left off it.  SNBeacon, the button at
rest, sits apart from the rest of its set in SNControlBar512_001.tga (100,281 where the others are
past x 389), and its alpha is solid over the whole 34x24 rectangle bar 68 texels of the bottom row,
so the Classic bar drew it as a red button on a black card.  SNBeaconH, SNBeaconP and SNBeaconI, the
same button lit, pressed and greyed, have the rounded corners open.  This takes SNBeacon's colour
and SNBeaconH's alpha into a texture of its own; ReforgedBarChinaBeacon.ini maps it and the Chinese
schemes in ControlBarSchemeReforged.ini wear it.

    python Tools/china_beacon.py <base game Textures.big>
        writes Data/Art/Textures/ReforgedBarChinaBeacon.tga.  The archive is the base game's,
        Run/ZH_Generals/Textures.big on this machine."""
import io
import os
import sys

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bigfile import _read_index, _matches

SOURCE = "art/textures/sncontrolbar512_001.tga"
BEACON = (100, 281, 134, 305)		# SNBeacon in SNControlBar512.INI
BEACON_LIT = (461, 269, 495, 293)	# SNBeaconH, the same shape with its corners open
TEXTURE_W, TEXTURE_H = 64, 32
OUT_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Data", "Art", "Textures", "ReforgedBarChinaBeacon.tga")


def read_member(archive, pattern):
    with open(archive, "rb") as f:
        for path, offset, size in list(_read_index(f)):
            if _matches(path, pattern):
                f.seek(offset)
                return f.read(size)
    raise ValueError("%s has no %s" % (archive, pattern))


def texture(archive):
    sheet = Image.open(io.BytesIO(read_member(archive, SOURCE))).convert("RGBA")
    button = sheet.crop(BEACON)
    button.putalpha(sheet.crop(BEACON_LIT).getchannel("A"))
    out = Image.new("RGBA", (TEXTURE_W, TEXTURE_H), (0, 0, 0, 0))
    out.paste(button, (0, 0))
    assert out.getpixel((0, 0))[3] == 0 and out.getpixel((16, 12))[3] == 255
    return out


def main():
    texture(sys.argv[1]).save(OUT_PATH)
    print(OUT_PATH)


if __name__ == "__main__":
    main()
