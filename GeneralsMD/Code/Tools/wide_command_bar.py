"""The command bar's middle plate two columns wider, for a mod whose CommandSets fill all eighteen slots.

Our ControlBar Pro painting holds seven columns of two command cells, 92 pixels apart at 1920x1080.
Contra's edit of ControlBar Pro (Hojjat, after EA, FAS and xezon, MIT) holds nine: the same plate one
column longer at each end, cells from x 576 to 1400.  This makes that plate out of our own art, so each
side keeps its own steel: everything left of x 708 moves 92 to the left, everything from 1208 on moves
92 to the right, and the 92 pixels before each cut are painted twice.  Both cuts sit where the art
repeats every 92 pixels (the cells, the rail, the shoulders beside the money box), so the joins do not
show.  Every offset is a multiple of four, so the DXT5 blocks are copied whole and nothing is
recompressed.  Only x 440 to 1476 changes, the middle piece drawPainting cuts out; the radar and the
selection pieces are the narrow painting's own pixels.

ControlBar::initWindows widens the grid to match (ControlBar_needsWideCommandGrid), and
ControlBarScheme draws <image>Wide, mapped in ReforgedHud<Side>CommandBar.ini, while it does.

    python Tools/wide_command_bar.py
        writes Data/Art/Textures/ReforgedHud<Side>CommandBarWide.dds from ReforgedHud<Side>CommandBar.dds
"""
import os
import struct

TEXTURES = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Data", "Art", "Textures")
SIDES = ("America", "China", "Gla")
STEP = 92                   # one command column at 1920x1080
LEFT_CUT, RIGHT_CUT = 708, 1208
BAND = (440, 1476)          # the middle piece: drawPainting's seams at design 182 and 616, to a block
HEADER = 128
BLOCK = 16                  # bytes in a DXT5 block of 4x4 texels


def source_x(x):
    if x < LEFT_CUT:
        return x + STEP
    if x >= RIGHT_CUT:
        return x - STEP
    return x


def widen(data):
    if data[:4] != b"DDS " or data[84:88] != b"DXT5":
        raise SystemExit("not a DXT5 dds")
    height, width = struct.unpack("<II", data[12:20])
    blocks_x = width // 4
    out = bytearray(data)
    for by in range(height // 4):
        row = HEADER + by * blocks_x * BLOCK
        for bx in range(BAND[0] // 4, BAND[1] // 4):
            src = row + (source_x(bx * 4) // 4) * BLOCK
            dst = row + bx * BLOCK
            out[dst:dst + BLOCK] = data[src:src + BLOCK]
    return bytes(out)


def main():
    for side in SIDES:
        with open(os.path.join(TEXTURES, "ReforgedHud%sCommandBar.dds" % side), "rb") as f:
            data = f.read()
        path = os.path.join(TEXTURES, "ReforgedHud%sCommandBarWide.dds" % side)
        with open(path, "wb") as f:
            f.write(widen(data))
        print("wrote", os.path.normpath(path))


if __name__ == "__main__":
    main()
