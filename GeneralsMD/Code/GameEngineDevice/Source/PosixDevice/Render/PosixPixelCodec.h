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

// FILE: PosixPixelCodec.h ////////////////////////////////////////////////////////////////////////
// Desc:   Pixels of Direct3D 9's formats to and from RGBA, for the copies, fills and filters that
//         D3D9 and D3DX do on the CPU (decision 7, phase A2).
///////////////////////////////////////////////////////////////////////////////////////////////////

/* One colour is four floats, red, green, blue and alpha, in 0..1.  A format's channels are read as
	 D3D9 defines them: a missing colour channel reads as 1 for X formats' unused bits and as the
	 luminance for L formats, a missing alpha reads as 1.  Writing drops what a format cannot hold,
	 rounding to nearest.

	 Decoding covers every colour format a texture or surface of the game's is made in, and DXT1-5.
	 Encoding covers the uncompressed ones only.  Writing DXT is compression, which nothing has asked for
	 yet: posixCanEncode says so, and the callers fail the call rather than write something else. */

#pragma once

#ifndef POSIXPIXELCODEC_H
#define POSIXPIXELCODEC_H

#if defined(_WIN32)
#include <d3d9.h>		// Windows on ARM64's D3DX texture helpers (d3dx9portable_texture.cpp)
#include <stdint.h>
#include "Platform/RenderTypes.h"
#else
#include "Platform/D3D9Posix.h"
#endif

struct PosixColor
{
	float r, g, b, a;
};

/** Whether posixDecodeBlock can read this format. */
bool posixCanDecode( D3DFORMAT format );
/** Whether posixEncodePixel can write this format. */
bool posixCanEncode( D3DFORMAT format );

/** Reads one block of `format` (one pixel, two for the YUV pairs, sixteen for DXT) into `out`, row by
	* row across the block. */
bool posixDecodeBlock( D3DFORMAT format, const void *block, PosixColor *out );

/** Writes one pixel of an uncompressed format. */
bool posixEncodePixel( D3DFORMAT format, const PosixColor &color, void *pixel );

/** A depth and stencil value in a depth format, as Clear writes it: z in 0..1. */
bool posixEncodeDepth( D3DFORMAT format, float z, RenderUInt32 stencil, void *pixel );

/** D3DCOLOR (A8R8G8B8) to a colour, as ColorFill and Clear take theirs. */
PosixColor posixColorFromD3DColor( D3DCOLOR color );

#endif // POSIXPIXELCODEC_H
