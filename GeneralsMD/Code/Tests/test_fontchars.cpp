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
/*
 * D6b: FontCharsClass (render2dsentence.cpp) off Windows, drawing with FreeType where Windows draws
 * with GDI.  The class is the game's own; only its three GDI members have POSIX bodies, and those do
 * GDI's arithmetic around GlyphRasteriserClass - which test_glyph_rasteriser holds to the font files'
 * own hdmx, VDMX and gasp.  So this checks the arithmetic, through the class's public face:
 *   - the line height is the font's tmHeight, which for macOS's Arial at 10 points (13 px) is VDMX's 16;
 *   - a character's width is its advance plus the overlap (a pixel per 8 of height, at most 4), and 'W'
 *     one more, drawn one pixel in, as Store_GDI_Char has it on Windows;
 *   - the pixels come through the same square root into 4-bit alpha, and at a size gasp keeps bilevel
 *     every one is 0 or 15;
 *   - "Generals" is Arial squeezed: narrower, the same height.
 * What it cannot see: GDI's own output (no Windows machine); text in the game (the renderer, M4);
 * Language.ini's LocalFontFile registration in a real language (English ships none).
 */

#include "test_harness.h"

#include "glyphrasteriser.h"
#include "render2dsentence.h"

#include <stdio.h>
#include <string.h>
#include <vector>

/* W3DMPO_GLUE routes FontCharsClass's pooled buffers to the game's memory pools, which this test does
	 not link; plain storage stands in, as in test_ww3d2.cpp. */
void *createW3DMemPool( const char *, int ) { return (void *)1; }
void *allocateFromW3DMemPool( void *, int size ) { return ::operator new( (size_t)size ); }
void freeFromW3DMemPool( void *, void *p ) { ::operator delete( p ); }

namespace {

int pixelsFor( int points ) { return (points * 96 + 36) / 72; }	// -MulDiv( points, 96, 72 )

int overlapFor( int points )
{
	int overlap = pixelsFor( points ) / 8;
	return overlap < 0 ? 0 : (overlap > 4 ? 4 : overlap);
}

int advanceOf( const char *face, int points, int width, bool bold, WideChar ch )
{
	GlyphRasteriserClass r;
	if (!r.Create_Font( face, pixelsFor( points ), width, bold, points * 2 ))
		return -1;
	return r.Get_Advance( ch );
}

}  // namespace

TEST(fontchars_line_height_is_the_fonts_tmheight)
{
	FontCharsClass font;
	font.Initialize_GDI_Font( "Arial", 10, false );
	GlyphRasteriserClass r;
	CHECK( r.Create_Font( "Arial", 13, 0, false, 20 ) );
	CHECK_EQ( font.Get_Char_Height(), r.Get_Metrics().Height );
	CHECK_EQ( font.Get_Char_Height(), 16 );		// VDMX's 13 + 3 at 13 px, macOS's Arial 5.01 (D6's measurements)
	CHECK_EQ( font.Get_Extra_Overlap(), overlapFor( 10 ) );
}

TEST(fontchars_widths_are_the_advance_plus_gdis_overlap_and_w_one_more)
{
	const int sizes[] = { 8, 10, 12, 14 };
	for (size_t i = 0; i < sizeof( sizes ) / sizeof( sizes[0] ); ++i)
	{
		FontCharsClass font;
		font.Initialize_GDI_Font( "Arial", sizes[i], false );
		const char *text = "General, 1,234 $ supplies: Qwerty jig! W";
		int wrong = 0;
		for (const char *p = text; *p; ++p)
		{
			const WideChar ch = (WideChar)(unsigned char)*p;
			const int expected = advanceOf( "Arial", sizes[i], 0, false, ch ) + overlapFor( sizes[i] ) + (ch == 'W' ? 1 : 0);
			if (font.Get_Char_Width( ch ) != expected && wrong++ < 3)
				printf( "  Arial %dpt '%c': %d, want %d\n", sizes[i], *p, font.Get_Char_Width( ch ), expected );
		}
		CHECK_EQ( wrong, 0 );
	}
}

TEST(fontchars_pixels_are_4_bit_alpha_bilevel_where_gasp_says)
{
	GlyphRasteriserClass::Set_Antialias_Mode( GlyphRasteriserClass::ANTIALIAS_AS_GASP_SAYS );
	FontCharsClass font;
	font.Initialize_GDI_Font( "Arial", 10, false );	// 13 px: bilevel by macOS Arial's gasp
	const int width = font.Get_Char_Width( 'H' ), height = font.Get_Char_Height();
	std::vector<uint16> pixels( (size_t)width * height, 0 );
	font.Blit_Char( 'H', &pixels[0], width * 2, 0, 0 );
	int ink = 0, other = 0;
	for (size_t i = 0; i < pixels.size(); ++i)
	{
		const unsigned alpha = pixels[i] >> 12, colour = pixels[i] & 0x0FFF;
		if (alpha == 15 && colour == 0x0FFF) ++ink;
		else if (!(alpha == 0 && colour == 0)) ++other;
	}
	printf( "  'H' at 10pt: %d x %d, %d inked pixels, %d partial\n", width, height, ink, other );
	CHECK( ink > 10 );
	CHECK_EQ( other, 0 );

	// and grey where the switch says always grey: partial alpha appears
	GlyphRasteriserClass::Set_Antialias_Mode( GlyphRasteriserClass::ANTIALIAS_ALWAYS_GRAY );
	FontCharsClass grey;
	grey.Initialize_GDI_Font( "Arial", 10, false );
	std::vector<uint16> greyPixels( (size_t)grey.Get_Char_Width( 'O' ) * grey.Get_Char_Height(), 0 );
	grey.Blit_Char( 'O', &greyPixels[0], grey.Get_Char_Width( 'O' ) * 2, 0, 0 );
	int partial = 0;
	for (size_t i = 0; i < greyPixels.size(); ++i)
		partial += ((greyPixels[i] >> 12) != 0 && (greyPixels[i] >> 12) != 15) ? 1 : 0;
	CHECK( partial > 0 );
	GlyphRasteriserClass::Set_Antialias_Mode( GlyphRasteriserClass::ANTIALIAS_AS_GASP_SAYS );
}

TEST(fontchars_generals_is_arial_squeezed)
{
	FontCharsClass generals, arial;
	generals.Initialize_GDI_Font( "Generals", 15, false );
	arial.Initialize_GDI_Font( "Arial", 15, false );
	CHECK_EQ( generals.Get_Char_Height(), arial.Get_Char_Height() );
	int squeezed = 0, plain = 0;
	for (const char *p = "GENERALS"; *p; ++p)
	{
		squeezed += generals.Get_Char_Width( (WideChar)*p );
		plain += arial.Get_Char_Width( (WideChar)*p );
	}
	printf( "  \"GENERALS\" at 15pt: Generals %d px, Arial %d px\n", squeezed, plain );
	CHECK( squeezed < plain );
	// the squeeze is lfWidth = 0.40 x 20 px, the rasteriser's own answer for it
	const int lfWidth = (int)(pixelsFor( 15 ) * 0.40f);
	CHECK_EQ( generals.Get_Char_Width( 'G' ), advanceOf( "Arial", 15, lfWidth, false, 'G' ) + overlapFor( 15 ) );
}
