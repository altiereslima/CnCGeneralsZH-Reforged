#!/usr/bin/env python3
#	Copyright 2026 İlyas Akın
#	Additional terms under GNU GPL section 7 apply: see LICENSE.md.
#
#	This program is free software: you can redistribute it and/or modify
#	it under the terms of the GNU General Public License as published by
#	the Free Software Foundation, either version 3 of the License, or
#	(at your option) any later version.
#
#	This program is distributed in the hope that it will be useful,
#	but WITHOUT ANY WARRANTY; without even the implied warranty of
#	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#	GNU General Public License for more details.
#
#	You should have received a copy of the GNU General Public License
#	along with this program.  If not, see <http://www.gnu.org/licenses/>.
"""gdi-font-metrics-header.py <folder>: gdifontmetrics.h from what Tools/gdi-font-metrics.ps1 wrote into
<folder> (gdi-metrics.csv, fonts.txt), on stdout.  The header's prose is here; the numbers and the font
list are the measurement's.  See the header for what they are and how they were measured."""
import csv, sys

HEAD = '/*\n**\tCopyright 2026 İlyas Akın\n**\tAdditional terms under GNU GPL section 7 apply: see LICENSE.md.\n**\n**\tThis program is free software: you can redistribute it and/or modify\n**\tit under the terms of the GNU General Public License as published by\n**\tthe Free Software Foundation, either version 3 of the License, or\n**\t(at your option) any later version.\n**\n**\tThis program is distributed in the hope that it will be useful,\n**\tbut WITHOUT ANY WARRANTY; without even the implied warranty of\n**\tMERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n**\tGNU General Public License for more details.\n**\n**\tYou should have received a copy of the GNU General Public License\n**\talong with this program.  If not, see <http://www.gnu.org/licenses/>.\n*/\n\n/* gdifontmetrics.h: the line metrics GDI gives the game\'s three Windows fonts, for glyphrasteriser.cpp\n\t to use where it draws with a metric-compatible substitute instead (Linux: Liberation Sans, Serif and\n\t Mono for Arial, Times New Roman and Courier New).  The substitutes match the advance widths and even\n\t usWinAscent/usWinDescent, but have no VDMX, and GDI takes tmAscent and tmDescent from VDMX: without\n\t this, 30 to 38 of the 43 sizes below come out 1 or 2 pixels shorter than on Windows (L1b).\n\n\t WHAT THESE ARE: measured integers, TEXTMETRIC\'s tmAscent and tmDescent (tmHeight is their sum at\n\t every size here), per pixel height.  No font data is included: no outlines, no hinting, no table\n\t copied from a font file.\n\n\t HOW THEY WERE MEASURED (2026-09-27): Tools/gdi-font-metrics.ps1 in a Windows VM, GDI itself:\n\t CreateFont( -ppem, 0, 0, 0, FW_NORMAL or FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,\n\t CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, VARIABLE_PITCH, face ) - render2dsentence.cpp\'s own call -\n\t selected into a memory DC, then GetTextMetrics.  GetTextFace confirmed each face; tmOverhang was 0 at\n\t every size; the "Generals" lfWidth (0.40 x the height) changed no ascent or height.\n'
MIDDLE = "\t Cross-checks: Arial at 13 px is 13 + 3 = 16, test_fontchars' number; and macOS's own Arial, Times\n\t New Roman and Courier New (5.01 and alike, regular and bold), through glyphrasteriser.cpp's VDMX\n\t rule, give the same ascent and height at all 43 sizes, so a Mac, which reads those files, needs none\n\t of this. */\n\n#pragma once\n\nstruct GdiFontMetrics\n{\n\tconst char *face;\t\t\t\t\t///< the Windows face these are GDI's metrics for\n\tbool bold;\t\t\t\t\t\t\t\t///< FW_BOLD\n\tunsigned char ascent[43];\t///< tmAscent at ppem GDI_METRICS_FIRST_PPEM + i\n\tunsigned char descent[43];\t///< tmDescent at the same\n};\n\nenum { GDI_METRICS_FIRST_PPEM = 6, GDI_METRICS_LAST_PPEM = 48 };\n\nstatic const GdiFontMetrics theGdiFontMetrics[] =\n{"

def main():
    folder = sys.argv[1]
    rows = [r for r in csv.DictReader(open(folder + '/gdi-metrics.csv', newline='', encoding='utf-8-sig'))
            if r['width'] == '0']
    fonts = [l.strip() for l in open(folder + '/fonts.txt', encoding='utf-8-sig') if l.strip()]
    out = [HEAD.rstrip('\n')]
    out += ['\t   ' + f for f in fonts]
    out.append(MIDDLE)
    for face in ['Arial', 'Times New Roman', 'Courier New']:
        for bold in ['0', '1']:
            sel = sorted([r for r in rows if r['face'] == face and r['bold'] == bold], key=lambda r: int(r['ppem']))
            assert [int(r['ppem']) for r in sel] == list(range(6, 49)), (face, bold)
            assert all(int(r['tmHeight']) == int(r['tmAscent']) + int(r['tmDescent']) for r in sel), (face, bold)
            assert all(r['selected'].strip() == face for r in sel), (face, bold)
            a = ', '.join(r['tmAscent'] for r in sel)
            d = ', '.join(r['tmDescent'] for r in sel)
            out.append('\t{ "%s", %s,\n\t\t{ %s },\n\t\t{ %s } },' % (face, 'true' if bold == '1' else 'false', a, d))
    out.append('};\n')
    sys.stdout.write('\n'.join(out))

if __name__ == '__main__':
    main()
