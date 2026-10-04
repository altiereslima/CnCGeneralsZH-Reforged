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

// PosixPixelCodec.cpp: see PosixPixelCodec.h.  Every multi-byte pixel is little-endian in memory, as
// on Windows; the formats' bit layouts are D3D9's.

#include "PosixPixelCodec.h"

#include <math.h>
#include <string.h>

namespace {

inline float unorm( uint32_t value, unsigned int bits )
{
	return (float)value / (float)((1u << bits) - 1u);
}

inline uint32_t toUnorm( float value, unsigned int bits )
{
	if (!(value > 0.0f)) return 0;		// also catches NaN
	if (value >= 1.0f) return (1u << bits) - 1u;
	return (uint32_t)lrintf( value * (float)((1u << bits) - 1u) );
}

inline uint32_t field( uint32_t value, unsigned int shift, unsigned int bits )
{
	return (value >> shift) & ((1u << bits) - 1u);
}

inline uint32_t read16( const uint8_t *p ) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
inline uint32_t read24( const uint8_t *p ) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16); }
inline uint32_t read32( const uint8_t *p ) { return read24( p ) | ((uint32_t)p[3] << 24); }
inline void write16( uint8_t *p, uint32_t v ) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
inline void write24( uint8_t *p, uint32_t v ) { write16( p, v ); p[2] = (uint8_t)(v >> 16); }
inline void write32( uint8_t *p, uint32_t v ) { write24( p, v ); p[3] = (uint8_t)(v >> 24); }

/** A packed ARGB layout: each channel's shift and width; a width of 0 means the channel is absent. */
struct PackedLayout
{
	unsigned int bytes;
	unsigned int aShift, aBits, rShift, rBits, gShift, gBits, bShift, bBits;
	bool luminance;		///< r, g and b are one L channel, at rShift/rBits
};

bool packedLayout( D3DFORMAT format, PackedLayout *layout )
{
	static const struct { D3DFORMAT format; PackedLayout layout; } table[] = {
		{ D3DFMT_A8R8G8B8,		{ 4, 24, 8, 16, 8, 8, 8, 0, 8, false } },
		{ D3DFMT_X8R8G8B8,		{ 4, 0, 0, 16, 8, 8, 8, 0, 8, false } },
		{ D3DFMT_A8B8G8R8,		{ 4, 24, 8, 0, 8, 8, 8, 16, 8, false } },
		{ D3DFMT_X8B8G8R8,		{ 4, 0, 0, 0, 8, 8, 8, 16, 8, false } },
		{ D3DFMT_A2R10G10B10,	{ 4, 30, 2, 20, 10, 10, 10, 0, 10, false } },
		{ D3DFMT_A2B10G10R10,	{ 4, 30, 2, 0, 10, 10, 10, 20, 10, false } },
		{ D3DFMT_R8G8B8,			{ 3, 0, 0, 16, 8, 8, 8, 0, 8, false } },
		{ D3DFMT_R5G6B5,			{ 2, 0, 0, 11, 5, 5, 6, 0, 5, false } },
		{ D3DFMT_X1R5G5B5,		{ 2, 0, 0, 10, 5, 5, 5, 0, 5, false } },
		{ D3DFMT_A1R5G5B5,		{ 2, 15, 1, 10, 5, 5, 5, 0, 5, false } },
		{ D3DFMT_A4R4G4B4,		{ 2, 12, 4, 8, 4, 4, 4, 0, 4, false } },
		{ D3DFMT_X4R4G4B4,		{ 2, 0, 0, 8, 4, 4, 4, 0, 4, false } },
		{ D3DFMT_A8R3G3B2,		{ 2, 8, 8, 5, 3, 2, 3, 0, 2, false } },
		{ D3DFMT_R3G3B2,			{ 1, 0, 0, 5, 3, 2, 3, 0, 2, false } },
		{ D3DFMT_A8,					{ 1, 0, 8, 0, 0, 0, 0, 0, 0, false } },
		{ D3DFMT_L8,					{ 1, 0, 0, 0, 8, 0, 0, 0, 0, true } },
		{ D3DFMT_L16,					{ 2, 0, 0, 0, 16, 0, 0, 0, 0, true } },
		{ D3DFMT_A8L8,				{ 2, 8, 8, 0, 8, 0, 0, 0, 0, true } },
		{ D3DFMT_A4L4,				{ 1, 4, 4, 0, 4, 0, 0, 0, 0, true } },
	};
	for (size_t i = 0; i < sizeof( table ) / sizeof( table[0] ); ++i)
	{
		if (table[i].format == format)
		{
			*layout = table[i].layout;
			return true;
		}
	}
	return false;
}

uint32_t readPacked( const uint8_t *p, unsigned int bytes )
{
	switch (bytes)
	{
		case 1: return p[0];
		case 2: return read16( p );
		case 3: return read24( p );
		default: return read32( p );
	}
}

void writePacked( uint8_t *p, unsigned int bytes, uint32_t value )
{
	switch (bytes)
	{
		case 1: p[0] = (uint8_t)value; break;
		case 2: write16( p, value ); break;
		case 3: write24( p, value ); break;
		default: write32( p, value ); break;
	}
}

// DXT: a 5:6:5 end point expanded to eight bits a channel, as the hardware decoders do.
void expand565( uint32_t c, float rgb[3] )
{
	const uint32_t r = field( c, 11, 5 ), g = field( c, 5, 6 ), b = field( c, 0, 5 );
	rgb[0] = (float)((r << 3) | (r >> 2)) / 255.0f;
	rgb[1] = (float)((g << 2) | (g >> 4)) / 255.0f;
	rgb[2] = (float)((b << 3) | (b >> 2)) / 255.0f;
}

/** A DXT colour block: four colours from two end points, and a 2-bit index a pixel.  `threeColour`
	* allows DXT1's mode where c0 <= c1 gives a midpoint and transparent black. */
void decodeColourBlock( const uint8_t *block, bool threeColour, PosixColor *out )
{
	const uint32_t c0 = read16( block ), c1 = read16( block + 2 );
	float e0[3], e1[3];
	expand565( c0, e0 );
	expand565( c1, e1 );
	PosixColor palette[4];
	palette[0] = { e0[0], e0[1], e0[2], 1.0f };
	palette[1] = { e1[0], e1[1], e1[2], 1.0f };
	if (!threeColour || c0 > c1)
	{
		palette[2] = { (2 * e0[0] + e1[0]) / 3, (2 * e0[1] + e1[1]) / 3, (2 * e0[2] + e1[2]) / 3, 1.0f };
		palette[3] = { (e0[0] + 2 * e1[0]) / 3, (e0[1] + 2 * e1[1]) / 3, (e0[2] + 2 * e1[2]) / 3, 1.0f };
	}
	else
	{
		palette[2] = { (e0[0] + e1[0]) / 2, (e0[1] + e1[1]) / 2, (e0[2] + e1[2]) / 2, 1.0f };
		palette[3] = { 0.0f, 0.0f, 0.0f, 0.0f };
	}
	const uint32_t indices = read32( block + 4 );
	for (int i = 0; i < 16; ++i)
		out[i] = palette[field( indices, 2 * i, 2 )];
}

} // namespace

bool posixCanDecode( D3DFORMAT format )
{
	PackedLayout layout;
	if (packedLayout( format, &layout ))
		return true;
	switch (format)
	{
		case D3DFMT_DXT1: case D3DFMT_DXT2: case D3DFMT_DXT3: case D3DFMT_DXT4: case D3DFMT_DXT5:
			return true;
		default:
			return false;
	}
}

bool posixCanEncode( D3DFORMAT format )
{
	PackedLayout layout;
	return packedLayout( format, &layout );
}

bool posixDecodeBlock( D3DFORMAT format, const void *source, PosixColor *out )
{
	const uint8_t *block = (const uint8_t *)source;
	PackedLayout layout;
	if (packedLayout( format, &layout ))
	{
		const uint32_t value = readPacked( block, layout.bytes );
		PosixColor color;
		if (layout.luminance)
			color.r = color.g = color.b = unorm( field( value, layout.rShift, layout.rBits ), layout.rBits );
		else
		{
			color.r = layout.rBits ? unorm( field( value, layout.rShift, layout.rBits ), layout.rBits ) : 0.0f;
			color.g = layout.gBits ? unorm( field( value, layout.gShift, layout.gBits ), layout.gBits ) : 0.0f;
			color.b = layout.bBits ? unorm( field( value, layout.bShift, layout.bBits ), layout.bBits ) : 0.0f;
		}
		color.a = layout.aBits ? unorm( field( value, layout.aShift, layout.aBits ), layout.aBits ) : 1.0f;
		*out = color;
		return true;
	}

	switch (format)
	{
		case D3DFMT_DXT1:
			decodeColourBlock( block, true, out );
			return true;
		case D3DFMT_DXT2: case D3DFMT_DXT3:
		{
			decodeColourBlock( block + 8, false, out );
			for (int i = 0; i < 16; ++i)
				out[i].a = unorm( (block[i / 2] >> ((i % 2) * 4)) & 0xF, 4 );
			return true;
		}
		case D3DFMT_DXT4: case D3DFMT_DXT5:
		{
			decodeColourBlock( block + 8, false, out );
			const float a0 = block[0] / 255.0f, a1 = block[1] / 255.0f;
			float alphas[8];
			alphas[0] = a0;
			alphas[1] = a1;
			if (block[0] > block[1])
			{
				for (int i = 1; i < 7; ++i)
					alphas[i + 1] = ((7 - i) * a0 + i * a1) / 7.0f;
			}
			else
			{
				for (int i = 1; i < 5; ++i)
					alphas[i + 1] = ((5 - i) * a0 + i * a1) / 5.0f;
				alphas[6] = 0.0f;
				alphas[7] = 1.0f;
			}
			const uint64_t bits = (uint64_t)read24( block + 2 ) | ((uint64_t)read24( block + 5 ) << 24);
			for (int i = 0; i < 16; ++i)
				out[i].a = alphas[(bits >> (3 * i)) & 7];
			return true;
		}
		default:
			return false;
	}
}

bool posixEncodePixel( D3DFORMAT format, const PosixColor &color, void *pixel )
{
	PackedLayout layout;
	if (!packedLayout( format, &layout ))
		return false;
	uint32_t value = 0;
	if (layout.luminance)
	{
		// Rec. 601 luma, as D3DX's conversion into L formats has it.
		const float luma = 0.299f * color.r + 0.587f * color.g + 0.114f * color.b;
		value |= toUnorm( luma, layout.rBits ) << layout.rShift;
	}
	else
	{
		if (layout.rBits) value |= toUnorm( color.r, layout.rBits ) << layout.rShift;
		if (layout.gBits) value |= toUnorm( color.g, layout.gBits ) << layout.gShift;
		if (layout.bBits) value |= toUnorm( color.b, layout.bBits ) << layout.bShift;
	}
	if (layout.aBits)
		value |= toUnorm( color.a, layout.aBits ) << layout.aShift;
	else if (format == D3DFMT_X8R8G8B8 || format == D3DFMT_X8B8G8R8)
		value |= 0xff000000u;		// the unused byte written as D3DX writes it
	writePacked( (uint8_t *)pixel, layout.bytes, value );
	return true;
}

bool posixEncodeDepth( D3DFORMAT format, float z, RenderUInt32 stencil, void *pixel )
{
	uint8_t *p = (uint8_t *)pixel;
	switch (format)
	{
		case D3DFMT_D16: case D3DFMT_D16_LOCKABLE:
			write16( p, toUnorm( z, 16 ) );
			return true;
		case D3DFMT_D15S1:
			write16( p, (toUnorm( z, 15 ) << 1) | (stencil & 1u) );
			return true;
		case D3DFMT_D24S8:
			write32( p, (toUnorm( z, 24 ) << 8) | (stencil & 0xffu) );
			return true;
		case D3DFMT_D24X8:
			write32( p, toUnorm( z, 24 ) << 8 );
			return true;
		case D3DFMT_D24X4S4:
			write32( p, (toUnorm( z, 24 ) << 8) | (stencil & 0xfu) );
			return true;
		case D3DFMT_D32:
		{
			const double scaled = (z <= 0.0f) ? 0.0 : (z >= 1.0f) ? 4294967295.0 : floor( (double)z * 4294967295.0 + 0.5 );
			write32( p, (uint32_t)scaled );
			return true;
		}
		case D3DFMT_D32F_LOCKABLE:
			memcpy( p, &z, 4 );
			return true;
		default:
			return false;
	}
}

PosixColor posixColorFromD3DColor( D3DCOLOR color )
{
	PosixColor out;
	out.a = unorm( field( color, 24, 8 ), 8 );
	out.r = unorm( field( color, 16, 8 ), 8 );
	out.g = unorm( field( color, 8, 8 ), 8 );
	out.b = unorm( field( color, 0, 8 ), 8 );
	return out;
}
