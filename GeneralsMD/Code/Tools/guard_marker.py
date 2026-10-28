"""EA's guard marker (SCCGuard_Generic, the GuardAreaRadiusCursor decal) cut into its two parts.

The one picture holds the ring and the shield in the middle, so scaling it to the radius the wheel
sets stretched the shield with the ring. InGameUI draws the parts as two decals instead: the ring at
the radius, the shield at the size it has at the unwheeled one.

    ReforgedGuardRing.tga     the whole 256x256 picture with the shield cleared (it sits inside
                              r < 44 px; the ring is r 100-124, nothing lies between)
    ReforgedGuardCentre.tga   the middle 128x128, the shield alone: half the picture's width, so
                              InGameUI draws it at half the ring decal's size

    python Tools/guard_marker.py <TexturesZH.big>    # writes Data/Art/Textures/ReforgedGuard*.tga
"""
import io
import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bigfile  # noqa: E402

MEMBER = "art/textures/sccguard_generic.dds"
OUT_FOLDER = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Data", "Art", "Textures")


def main(archive):
    with open(archive, "rb") as f:
        for path, offset, size in list(bigfile._read_index(f)):
            if path.lower().replace("\\", "/") == MEMBER:
                f.seek(offset)
                picture = np.array(Image.open(io.BytesIO(f.read(size))).convert("RGBA"))
                break
        else:
            sys.exit("%s has no %s" % (archive, MEMBER))

    assert picture.shape == (256, 256, 4), picture.shape
    y, x = np.mgrid[0:256, 0:256]
    r = np.hypot(x - 127.5, y - 127.5)

    ring = picture.copy()
    ring[r < 70, 3] = 0
    Image.fromarray(ring).save(os.path.join(OUT_FOLDER, "ReforgedGuardRing.tga"), compression="tga_rle")

    centre = picture[64:192, 64:192]
    assert centre[..., 3][r[64:192, 64:192] > 60].max() == 0, "the ring reaches into the shield's square"
    Image.fromarray(centre).save(os.path.join(OUT_FOLDER, "ReforgedGuardCentre.tga"), compression="tga_rle")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
