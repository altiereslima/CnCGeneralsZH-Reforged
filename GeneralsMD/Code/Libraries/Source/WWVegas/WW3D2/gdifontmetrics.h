/*
**	Copyright 2026 İlyas Akın
**	Additional terms under GNU GPL section 7 apply: see LICENSE.md.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/* gdifontmetrics.h: the line metrics GDI gives the game's three Windows fonts, for glyphrasteriser.cpp
	 to use where it draws with a metric-compatible substitute instead (Linux: Liberation Sans, Serif and
	 Mono for Arial, Times New Roman and Courier New).  The substitutes match the advance widths and even
	 usWinAscent/usWinDescent, but have no VDMX, and GDI takes tmAscent and tmDescent from VDMX: without
	 this, 30 to 38 of the 43 sizes below come out 1 or 2 pixels shorter than on Windows (L1b).

	 WHAT THESE ARE: measured integers, TEXTMETRIC's tmAscent and tmDescent (tmHeight is their sum at
	 every size here), per pixel height.  No font data is included: no outlines, no hinting, no table
	 copied from a font file.

	 HOW THEY WERE MEASURED (2026-09-27): Tools/gdi-font-metrics.ps1 in a Windows VM, GDI itself:
	 CreateFont( -ppem, 0, 0, 0, FW_NORMAL or FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
	 CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, VARIABLE_PITCH, face ) - render2dsentence.cpp's own call -
	 selected into a memory DC, then GetTextMetrics.  GetTextFace confirmed each face; tmOverhang was 0 at
	 every size; the "Generals" lfWidth (0.40 x the height) changed no ascent or height.
	   Windows: Microsoft Windows 11 Pro build 26200.6584, LOGPIXELSY of the memory DC 96
	   arial.ttf: Version 7.03, 1047208 bytes, sha256 74D696E666F696E93DB685A85C94BFBFCC0796DFCF8A2E2E2E2C908A54E82949
	   arialbd.ttf: Version 7.03, 991572 bytes, sha256 9E77165C7BFB4A5436F1AB683F1592BB2A9826E15AECAD8DC9ED278CE03E8882
	   times.ttf: Version 7.05, 1201620 bytes, sha256 F2CB777422A10368904D3F691E650E39DA862FE723389056324927DF540A35EC
	   timesbd.ttf: Version 7.05, 1180596 bytes, sha256 85B680FC9D8FEE549191DC823F31D777B37181D298B470BD615CF066920A4A51
	   cour.ttf: Version 6.94, 828760 bytes, sha256 6AAB1F79264CCDA1E7129DBA62DBE3750B10EAA27408A327DADB808724E62C84
	   courbd.ttf: Version 6.94, 827444 bytes, sha256 E376F24C9F27EB7370B25AA366D78DA731834A1AEA651858801C9A14E8906943
	 Cross-checks: Arial at 13 px is 13 + 3 = 16, test_fontchars' number; and macOS's own Arial, Times
	 New Roman and Courier New (5.01 and alike, regular and bold), through glyphrasteriser.cpp's VDMX
	 rule, give the same ascent and height at all 43 sizes, so a Mac, which reads those files, needs none
	 of this. */

#pragma once

struct GdiFontMetrics
{
	const char *face;					///< the Windows face these are GDI's metrics for
	bool bold;								///< FW_BOLD
	unsigned char ascent[43];	///< tmAscent at ppem GDI_METRICS_FIRST_PPEM + i
	unsigned char descent[43];	///< tmDescent at the same
};

enum { GDI_METRICS_FIRST_PPEM = 6, GDI_METRICS_LAST_PPEM = 48 };

static const GdiFontMetrics theGdiFontMetrics[] =
{
	{ "Arial", false,
		{ 5, 6, 8, 9, 10, 11, 12, 13, 13, 14, 15, 15, 17, 18, 19, 19, 20, 21, 21, 23, 25, 26, 26, 27, 28, 28, 29, 31, 32, 32, 33, 34, 34, 35, 36, 38, 38, 39, 40, 40, 42, 43, 45 },
		{ 1, 1, 2, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 6, 5, 6, 6, 6, 6, 7, 7, 7, 7, 7, 8, 8, 8, 9, 9, 9, 9, 9, 10, 10, 10, 10, 10, 10 } },
	{ "Arial", true,
		{ 5, 6, 8, 9, 10, 11, 12, 13, 13, 14, 15, 15, 17, 18, 19, 19, 21, 22, 23, 24, 24, 25, 26, 27, 28, 29, 30, 31, 32, 32, 34, 35, 36, 36, 37, 38, 39, 40, 40, 42, 43, 44, 45 },
		{ 1, 1, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4, 5, 5, 5, 5, 6, 6, 6, 7, 7, 7, 7, 7, 7, 7, 8, 9, 9, 9, 9, 9, 9, 9, 10, 11, 11, 11, 11, 11, 11 } },
	{ "Times New Roman", false,
		{ 5, 6, 8, 10, 10, 11, 12, 12, 13, 13, 15, 15, 16, 16, 17, 18, 20, 20, 21, 23, 23, 24, 26, 26, 27, 28, 28, 29, 31, 32, 32, 33, 34, 34, 36, 37, 38, 39, 40, 41, 42, 42, 43 },
		{ 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6, 7, 7, 7, 7, 7, 8, 8, 8, 8, 9, 9, 9, 9, 10, 10, 10, 10, 11, 11, 11, 11, 12 } },
	{ "Times New Roman", true,
		{ 5, 6, 9, 10, 10, 11, 12, 12, 14, 14, 15, 15, 16, 17, 18, 19, 20, 20, 21, 24, 24, 25, 26, 26, 28, 29, 29, 30, 31, 32, 33, 34, 35, 35, 36, 38, 39, 40, 41, 42, 43, 43, 44 },
		{ 1, 2, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6, 7, 7, 7, 7, 7, 8, 8, 8, 8, 9, 9, 9, 9, 9, 10, 10, 10, 10, 11 } },
	{ "Courier New", false,
		{ 5, 6, 6, 9, 9, 11, 12, 12, 13, 13, 14, 15, 15, 16, 17, 17, 18, 19, 20, 22, 22, 23, 23, 24, 25, 25, 27, 27, 28, 29, 29, 29, 30, 31, 31, 33, 33, 34, 35, 35, 36, 37, 37 },
		{ 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 6, 6, 6, 7, 7, 7, 7, 8, 8, 8, 8, 9, 9, 9, 10, 10, 10, 10, 11, 11, 11, 12, 12, 12, 12, 13, 13, 13 } },
	{ "Courier New", true,
		{ 5, 6, 8, 9, 10, 11, 12, 12, 12, 12, 13, 15, 16, 16, 17, 17, 18, 20, 20, 21, 22, 23, 23, 24, 25, 25, 26, 27, 27, 29, 30, 30, 32, 34, 34, 35, 36, 36, 37, 38, 38, 39, 40 },
		{ 2, 2, 3, 3, 3, 3, 4, 4, 4, 5, 5, 5, 5, 6, 6, 6, 7, 7, 7, 8, 8, 8, 8, 9, 9, 9, 10, 10, 10, 11, 11, 11, 11, 12, 12, 12, 13, 13, 13, 14, 14, 14, 14 } },
};
