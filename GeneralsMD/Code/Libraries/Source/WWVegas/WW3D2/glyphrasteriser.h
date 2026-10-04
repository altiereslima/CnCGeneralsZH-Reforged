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
 * GlyphRasteriserClass: what FontCharsClass asks GDI for, off Windows, from FreeType (decision 6, D6).
 *
 * FontCharsClass (render2dsentence.cpp) makes a GDI font with CreateFont, reads its TEXTMETRIC, and
 * for each character draws it with ExtTextOutW into a (2 x point size) square DIB and measures it with
 * GetTextExtentPoint32W.  On Windows it still does, unchanged.  Off Windows its twins ask this class
 * the same four questions, and the answers are meant to be GDI's for the same font file:
 *
 *   - Advances are the TrueType interpreter v35's, which takes the file's hdmx widths where it has
 *     them: the widths GDI uses (191 of 191 Latin-1 glyphs equal at every hdmx size of macOS's Arial,
 *     Arial Bold and Times New Roman; FreeType's default v40 moved whole strings by up to 230 px).  A
 *     width-scaled font ("Generals", CreateFont with an lfWidth) skips hdmx, as GDI does.
 *   - Heights are the file's VDMX table's, which GDI's tmAscent and tmDescent come from and FreeType
 *     does not read; the rounded usWin metrics without one.
 *   - Anti-aliasing follows the file's gasp table, as GDI's ANTIALIASED_QUALITY does, unless the one
 *     switch below says always grey.  Advances and heights never depend on the switch.
 *
 * Fonts: a file registered with Register_Font_File (Language.ini's LocalFontFile, what AddFontResource
 * installs on Windows) is found first, by its family name.  Otherwise the game's face name goes
 * through one substitution table: fixed files on macOS, Liberation through fontconfig on Linux.  No
 * Microsoft font is bundled.
 *
 * POSIX only; no Win32 types.  (D6.)
 */
#ifndef GLYPHRASTERISER_H
#define GLYPHRASTERISER_H

#include <stdint.h>

class GlyphRasteriserClass
{
public:

	/// The one switch (D6, for M4's visual review): where gasp says bilevel, draw bilevel, or not.
	enum AntialiasModeType
	{
		ANTIALIAS_AS_GASP_SAYS,		///< GDI's ANTIALIASED_QUALITY with this platform's font file (default)
		ANTIALIAS_ALWAYS_GRAY			///< grey at every size
	};

	/// What GetTextMetrics gives FontCharsClass
	struct MetricsStruct
	{
		int	Height;			///< tmHeight: Ascent + Descent
		int	Ascent;			///< tmAscent: the baseline's row in a drawn character
		int	Descent;		///< tmDescent
		int	Overhang;		///< tmOverhang: 0, as GDI's is for a TrueType font
	};

	GlyphRasteriserClass( void );
	~GlyphRasteriserClass( void );

	/** CreateFont( -pixel_height, average_width, ..., bold ? FW_BOLD : FW_NORMAL, ..., face ) with a
		* box_size x box_size DIB selected.  average_width is lfWidth: 0 for the face's own proportions.
		* FALSE when no file could be found or opened for the face. */
	bool				Create_Font( const char *face, int pixel_height, int average_width, bool bold, int box_size );
	void				Free_Font( void );

	const MetricsStruct &	Get_Metrics( void ) const		{ return Metrics; }

	/// GetTextExtentPoint32W of one character: its advance in pixels (the height is Metrics.Height)
	int					Get_Advance( uint32_t ch );

	/** ExtTextOutW( ETO_OPAQUE ) of one character at ( x_origin, 0 ), top-aligned: the box is cleared,
		* the pen is at x_origin on the baseline row Metrics.Ascent, and what falls outside is clipped. */
	void				Draw_Char( uint32_t ch, int x_origin );

	/// The box after Draw_Char: box_size * box_size coverage bytes, 0 to 255, top row first
	const uint8_t *	Get_Coverage( void ) const		{ return Box; }
	int					Get_Box_Size( void ) const		{ return BoxSize; }

	/// The file the face resolved to, and whether Draw_Char is drawing bilevel at this size
	const char *		Get_File_Path( void ) const;
	bool				Is_Bilevel( void ) const		{ return Bilevel; }

	static void		Set_Antialias_Mode( AntialiasModeType mode );
	static AntialiasModeType	Get_Antialias_Mode( void );

	/// AddFontResource's counterpart: the file is found before the substitution table, by family
	static bool		Register_Font_File( const char *path );
	static void		Unregister_Font_File( const char *path );

	/// The file a face resolves to without creating a font; empty when there is none
	static bool		Find_Font_File( const char *face, bool bold, char *path, int path_size );

private:

	GlyphRasteriserClass( const GlyphRasteriserClass & );
	GlyphRasteriserClass & operator=( const GlyphRasteriserClass & );

	int					Load_Glyph( uint32_t ch, bool for_drawing );

	struct FontStateStruct;
	FontStateStruct *	State;
	MetricsStruct		Metrics;
	uint8_t *			Box;
	int					BoxSize;
	bool				Bilevel;
};

#endif // GLYPHRASTERISER_H
