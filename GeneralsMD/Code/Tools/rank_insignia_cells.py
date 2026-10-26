"""The rank atlases' mapped images as the game reads them, shared by the three rank_insignia_*.py
generators: each cell cropped to its ink, so a row of stars or a warrant bar draws at its own shape
instead of squeezed into a square; named for its grade, Rank<Side>_<n> in the order the atlas holds
them, which is the order of the side's ladder; and every cell given its DisplayHeight, 14 for the
stacked or busy ones and 12 for the rest. A cell with no ink (America's E-1) gets no image and draws nothing."""
import re

ENTRY = re.compile(r"MappedImage (\S+)\n(.*?)Coords = Left:(\d+) Top:(\d+) Right:(\d+) Bottom:(\d+)\n(.*?)End\n", re.S)
INK = 16  # alpha under this is the antialiasing's fringe, not ink
TALL = "  DisplayHeight = 14\n"
# written on every cell, not only the tall ones: the command bar draws an overlay with no DisplayHeight
# over the whole button the way EA's chevrons go, which stretched a 12-high insignia over the portrait
SHORT = "  DisplayHeight = 12\n"


def ink_entries(atlas, ini_text, side, tall=()):
    """ini_text with every entry renamed, cropped and sized; tall holds the grades drawn 14 high."""
    alpha = atlas.getchannel("A").point(lambda v: 255 if v >= INK else 0)
    grades = iter(range(len(ENTRY.findall(ini_text))))

    def rewrite(match):
        grade = next(grades)
        left, top, right, bottom = (int(v) for v in match.group(3, 4, 5, 6))
        box = alpha.crop((left, top, right, bottom)).getbbox()
        if box is None:
            return ""
        return "MappedImage Rank%s_%d\n%sCoords = Left:%d Top:%d Right:%d Bottom:%d\n%s%sEnd\n" % (
            side, grade, match.group(2), left + box[0], top + box[1], left + box[2], top + box[3],
            match.group(7), TALL if grade in tall else SHORT)

    return ENTRY.sub(rewrite, ini_text)


if __name__ == "__main__":
    from PIL import Image
    test = Image.new("RGBA", (128, 64), (0, 0, 0, 0))
    test.paste((255, 255, 255, 255), (70, 10, 90, 20))
    entry = "MappedImage A\n  Texture = t.tga\n  Coords = Left:%d Top:0 Right:%d Bottom:64\n  Status = NONE\nEnd\n"
    out = ink_entries(test, "; x\n" + entry % (0, 64) + entry % (64, 128), "Test", tall=(1,))
    assert "RankTest_0" not in out, out
    assert "MappedImage RankTest_1" in out and "Left:70 Top:10 Right:90 Bottom:20" in out and TALL in out, out
    short = ink_entries(test, "; x\n" + entry % (0, 64) + entry % (64, 128), "Test")
    assert SHORT in short, short
    print("ok")
