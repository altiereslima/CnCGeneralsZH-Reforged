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

// The capture reader: see ffcapture.h for what it reads and its independence record.

#include "ffreference/ffcapture.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace FFCapture {

namespace {

bool readFile( const std::string &path, std::vector<uint8_t> &out )
{
	FILE *f = fopen( path.c_str(), "rb" );
	if (f == NULL)
		return false;
	uint8_t buffer[ 65536 ];
	size_t got;
	out.clear();
	while ((got = fread( buffer, 1, sizeof( buffer ), f )) > 0)
		out.insert( out.end(), buffer, buffer + got );
	fclose( f );
	return true;
}

uint32_t u32at( const std::vector<uint8_t> &b, size_t at ) { uint32_t v; memcpy( &v, &b[at], 4 ); return v; }
float f32at( const std::vector<uint8_t> &b, size_t at ) { float v; memcpy( &v, &b[at], 4 ); return v; }

struct Cursor
{
	const std::vector<uint8_t> &bytes;
	size_t at;
	bool ok;
	explicit Cursor( const std::vector<uint8_t> &b ) : bytes( b ), at( 0 ), ok( true ) {}
	bool need( size_t n ) { if (!ok || at + n > bytes.size()) ok = false; return ok; }
	uint32_t u32() { if (!need( 4 )) return 0; const uint32_t v = u32at( bytes, at ); at += 4; return v; }
	float f32() { if (!need( 4 )) return 0; const float v = f32at( bytes, at ); at += 4; return v; }
	std::string chars( size_t n )
	{
		if (!need( n ))
			return "";
		std::string s( (const char *)&bytes[at], n );
		at += n;
		const size_t nul = s.find( '\0' );
		return nul == std::string::npos ? s : s.substr( 0, nul );
	}
};

bool readProg( const std::string &path, Prog &out, std::string &error )
{
	std::vector<uint8_t> bytes;
	if (!readFile( path, bytes ))
	{
		error = "cannot open " + path;
		return false;
	}
	Cursor c( bytes );
	if (bytes.size() < 8 || memcmp( &bytes[0], "ZHPG", 4 ) != 0)
	{
		error = path + " is not a ZHPG file";
		return false;
	}
	c.at = 4;
	out.version = c.u32();
	if (out.version != 1 && out.version != 2)
	{
		error = path + ": a .prog version other than 1 or 2";
		return false;
	}
	for (int section = 0; section < 2; ++section)
	{
		const bool present = c.u32() != 0;
		std::string name;
		std::vector<uint32_t> tokens;
		if (present)
		{
			name = c.chars( 64 );
			const uint32_t count = c.u32();
			for (uint32_t i = 0; c.ok && i < count; ++i)
				tokens.push_back( c.u32() );
		}
		if (section == 0)
		{
			out.vertexPresent = present;
			out.vertexName = name;
			out.vertexTokens = tokens;
			const uint32_t count = c.u32();
			for (uint32_t i = 0; c.ok && i < count; ++i)
			{
				if (!c.need( 8 ))
					break;
				FFRef::DeclarationElement e;
				memcpy( &e.stream, &bytes[c.at], 2 );
				memcpy( &e.offset, &bytes[c.at + 2], 2 );
				e.type = bytes[c.at + 4];
				e.method = bytes[c.at + 5];
				e.usage = bytes[c.at + 6];
				e.usageIndex = bytes[c.at + 7];
				out.elements.push_back( e );
				c.at += 8;
			}
		}
		else
		{
			out.pixelPresent = present;
			out.pixelName = name;
			out.pixelTokens = tokens;
		}
	}
	for (int r = 0; r < 96; ++r)
		for (int k = 0; k < 4; ++k)
			out.vertexConstants[r][k] = c.f32();
	for (int r = 0; r < 8; ++r)
		for (int k = 0; k < 4; ++k)
			out.pixelConstants[r][k] = c.f32();
	if (out.version == 2)
	{
		const uint32_t count = c.u32();
		for (uint32_t i = 0; c.ok && i < count; ++i)
			out.d3d8.push_back( c.u32() );
		const uint32_t streams = c.u32();
		for (uint32_t i = 0; c.ok && i < streams; ++i)
		{
			Prog::Stream s;
			s.stream = c.u32();
			s.stride = c.u32();
			s.first = c.u32();
			out.streams.push_back( s );
		}
	}
	if (!c.ok || c.at != bytes.size())
	{
		char what[ 128 ];
		snprintf( what, sizeof( what ), "%s: %s at byte %u of %u", path.c_str(), c.ok ? "bytes left over" : "short",
			(unsigned)c.at, (unsigned)bytes.size() );
		error = what;
		return false;
	}
	return true;
}

// ---- texture formats (D3DFORMAT; the BC pages) -------------------------------------------------------

enum
{
	FMT_A8R8G8B8 = 21, FMT_X8R8G8B8 = 22, FMT_R5G6B5 = 23, FMT_X1R5G5B5 = 24, FMT_A1R5G5B5 = 25,
	FMT_A4R4G4B4 = 26, FMT_A8 = 28, FMT_L8 = 50, FMT_A8L8 = 51
};
const uint32_t FOURCC_DXT1 = 0x31545844, FOURCC_DXT3 = 0x33545844, FOURCC_DXT5 = 0x35545844;

FFRef::Color rgb565( uint16_t v )
{
	FFRef::Color c = { ((v >> 11) & 31) / 31.0, ((v >> 5) & 63) / 63.0, (v & 31) / 31.0, 1.0 };
	return c;
}

/// One 4x4 colour block ("Block Compression (Direct3D 10)", BC1): 4-colour mode when c0 > c1 or when
/// the format always uses it (BC2, BC3), else 3 colours and transparent black.
void colourBlock( const uint8_t *b, bool alwaysFour, FFRef::Color out[16] )
{
	uint16_t c0, c1;
	memcpy( &c0, b, 2 );
	memcpy( &c1, b + 2, 2 );
	FFRef::Color p[4];
	p[0] = rgb565( c0 );
	p[1] = rgb565( c1 );
	if (alwaysFour || c0 > c1)
	{
		for (int k = 0; k < 3; ++k)
		{
			const double a = k == 0 ? p[0].r : k == 1 ? p[0].g : p[0].b, z = k == 0 ? p[1].r : k == 1 ? p[1].g : p[1].b;
			double *t2 = k == 0 ? &p[2].r : k == 1 ? &p[2].g : &p[2].b, *t3 = k == 0 ? &p[3].r : k == 1 ? &p[3].g : &p[3].b;
			*t2 = (2 * a + z) / 3;
			*t3 = (a + 2 * z) / 3;
		}
		p[2].a = p[3].a = 1;
	}
	else
	{
		p[2].r = (p[0].r + p[1].r) / 2;
		p[2].g = (p[0].g + p[1].g) / 2;
		p[2].b = (p[0].b + p[1].b) / 2;
		p[2].a = 1;
		p[3].r = p[3].g = p[3].b = p[3].a = 0;
	}
	uint32_t bits;
	memcpy( &bits, b + 4, 4 );
	for (int i = 0; i < 16; ++i)
		out[i] = p[(bits >> (2 * i)) & 3];
}

bool decodeLevel( uint32_t format, int w, int h, const uint8_t *bytes, size_t size, FFRef::TextureLevel &out,
		std::string &error )
{
	out.width = w;
	out.height = h;
	out.texels.assign( (size_t)w * h, FFRef::Color() );
	const bool block = format == FOURCC_DXT1 || format == FOURCC_DXT3 || format == FOURCC_DXT5;
	if (block)
	{
		const int bw = (w + 3) / 4, bh = (h + 3) / 4;
		const size_t blockBytes = format == FOURCC_DXT1 ? 8 : 16;
		if (size != (size_t)bw * bh * blockBytes)
		{
			error = "a block-compressed level's byte count disagrees with its size";
			return false;
		}
		for (int by = 0; by < bh; ++by)
			for (int bx = 0; bx < bw; ++bx)
			{
				const uint8_t *b = bytes + ((size_t)by * bw + bx) * blockBytes;
				FFRef::Color texel[16];
				double alpha[16];
				bool explicitAlpha = false;
				if (format == FOURCC_DXT1)
					colourBlock( b, false, texel );
				else
				{
					colourBlock( b + 8, true, texel );
					explicitAlpha = true;
					if (format == FOURCC_DXT3)
					{
						// BC2: 4 bits of alpha per texel, row-major, low nibble first
						for (int i = 0; i < 16; ++i)
							alpha[i] = ((b[i / 2] >> (4 * (i & 1))) & 15) / 15.0;
					}
					else
					{
						// BC3: two endpoints and 3-bit indices; 8 values when a0 > a1, else 6 and 0, 1
						const double a0 = b[0] / 255.0, a1 = b[1] / 255.0;
						double table[8];
						table[0] = a0;
						table[1] = a1;
						if (b[0] > b[1])
						{
							for (int i = 1; i <= 6; ++i)
								table[i + 1] = ((7 - i) * a0 + i * a1) / 7;
						}
						else
						{
							for (int i = 1; i <= 4; ++i)
								table[i + 1] = ((5 - i) * a0 + i * a1) / 5;
							table[6] = 0;
							table[7] = 1;
						}
						uint64_t bits = 0;
						for (int i = 0; i < 6; ++i)
							bits |= (uint64_t)b[2 + i] << (8 * i);
						for (int i = 0; i < 16; ++i)
							alpha[i] = table[(bits >> (3 * i)) & 7];
					}
				}
				for (int i = 0; i < 16; ++i)
				{
					const int x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);
					if (x >= w || y >= h)
						continue;
					FFRef::Color c = texel[i];
					if (explicitAlpha)
						c.a = alpha[i];
					out.texels[(size_t)y * w + x] = c;
				}
			}
		return true;
	}
	int bpp = 0;
	switch (format)
	{
		case FMT_A8R8G8B8: case FMT_X8R8G8B8: bpp = 4; break;
		case FMT_R5G6B5: case FMT_X1R5G5B5: case FMT_A1R5G5B5: case FMT_A4R4G4B4: case FMT_A8L8: bpp = 2; break;
		case FMT_A8: case FMT_L8: bpp = 1; break;
		default:
		{
			char what[ 64 ];
			snprintf( what, sizeof( what ), "texture format %u is not decoded here", (unsigned)format );
			error = what;
			return false;
		}
	}
	if (size != (size_t)w * h * bpp)
	{
		error = "a level's byte count disagrees with its size";
		return false;
	}
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
		{
			const uint8_t *p = bytes + ((size_t)y * w + x) * bpp;
			FFRef::Color c = { 0, 0, 0, 1 };
			uint16_t v16 = 0;
			if (bpp == 2)
				memcpy( &v16, p, 2 );
			switch (format)
			{
				case FMT_A8R8G8B8: c.b = p[0] / 255.0; c.g = p[1] / 255.0; c.r = p[2] / 255.0; c.a = p[3] / 255.0; break;
				case FMT_X8R8G8B8: c.b = p[0] / 255.0; c.g = p[1] / 255.0; c.r = p[2] / 255.0; break;
				case FMT_R5G6B5: c = rgb565( v16 ); break;
				case FMT_X1R5G5B5: case FMT_A1R5G5B5:
					c.r = ((v16 >> 10) & 31) / 31.0; c.g = ((v16 >> 5) & 31) / 31.0; c.b = (v16 & 31) / 31.0;
					c.a = format == FMT_A1R5G5B5 ? (double)((v16 >> 15) & 1) : 1.0;
					break;
				case FMT_A4R4G4B4:
					c.a = ((v16 >> 12) & 15) / 15.0; c.r = ((v16 >> 8) & 15) / 15.0; c.g = ((v16 >> 4) & 15) / 15.0;
					c.b = (v16 & 15) / 15.0;
					break;
				case FMT_A8L8: c.r = c.g = c.b = p[0] / 255.0; c.a = p[1] / 255.0; break;
				case FMT_A8: c.r = c.g = c.b = 0; c.a = p[0] / 255.0; break;
				case FMT_L8: c.r = c.g = c.b = p[0] / 255.0; break;
			}
			out.texels[(size_t)y * w + x] = c;
		}
	return true;
}

FFRef::Matrix matrixFrom( const float m[16] )
{
	FFRef::Matrix out;
	for (int r = 0; r < 4; ++r)
		for (int c = 0; c < 4; ++c)
			out.m[r][c] = m[r * 4 + c];
	return out;
}

FFRef::Color colourFrom( const float *f )
{
	FFRef::Color c = { f[0], f[1], f[2], f[3] };
	return c;
}

}  // namespace

bool readCapture( const std::string &dir, const std::string &name, Capture &out, std::string &error )
{
	std::vector<uint8_t> b;
	const std::string path = dir + "/" + name + ".cap";
	if (!readFile( path, b ))
	{
		error = "cannot open " + path;
		return false;
	}
	if (b.size() < 5132 || memcmp( &b[0], "ZHDC", 4 ) != 0)
	{
		error = path + " is not a ZHDC capture";
		return false;
	}
	out.name = name;
	Header &h = out.header;
	h.version = u32at( b, 4 );
	h.primitiveType = u32at( b, 8 );
	h.primitiveCount = u32at( b, 12 );
	h.fvf = u32at( b, 16 );
	h.stride = u32at( b, 20 );
	h.vertexCount = u32at( b, 24 );
	h.indexCount = u32at( b, 28 );
	h.targetWidth = u32at( b, 32 );
	h.targetHeight = u32at( b, 36 );
	h.targetFormat = u32at( b, 40 );
	h.depthBound = u32at( b, 44 );
	for (int i = 0; i < 4; ++i)
		h.viewport[i] = u32at( b, 48 + 4 * i );
	h.minZ = f32at( b, 64 );
	h.maxZ = f32at( b, 68 );
	for (int i = 0; i < 256; ++i)
		h.renderState[i] = u32at( b, 72 + 4 * i );
	for (int s = 0; s < 8; ++s)
		for (int i = 0; i < 33; ++i)
			h.stageState[s][i] = u32at( b, 1096 + 4 * (s * 33 + i) );
	for (int s = 0; s < 8; ++s)
		for (int i = 0; i < 14; ++i)
			h.samplerState[s][i] = u32at( b, 2152 + 4 * (s * 14 + i) );
	for (int i = 0; i < 16; ++i)
	{
		h.world[i] = f32at( b, 2600 + 4 * i );
		h.view[i] = f32at( b, 2664 + 4 * i );
		h.projection[i] = f32at( b, 2728 + 4 * i );
	}
	for (int s = 0; s < 8; ++s)
		for (int i = 0; i < 16; ++i)
			h.texture[s][i] = f32at( b, 2792 + 4 * (s * 16 + i) );
	for (int i = 0; i < 17; ++i)
		h.material[i] = f32at( b, 3304 + 4 * i );
	for (int l = 0; l < 8; ++l)
		memcpy( h.lights[l], &b[3372 + 104 * l], 104 );
	for (int l = 0; l < 8; ++l)
		h.lightEnabled[l] = u32at( b, 4204 + 4 * l );
	for (int s = 0; s < 8; ++s)
	{
		std::string t( (const char *)&b[4236 + 48 * s], 48 );
		const size_t nul = t.find( '\0' );
		h.textures[s] = nul == std::string::npos ? t : t.substr( 0, nul );
	}
	{
		std::string sig( (const char *)&b[4620], 512 );
		const size_t nul = sig.find( '\0' );
		h.signature = nul == std::string::npos ? sig : sig.substr( 0, nul );
	}
	if (h.version != 1 && h.version != 2)
	{
		error = path + ": a capture version other than 1 or 2";
		return false;
	}
	const size_t vertexBytes = (size_t)h.vertexCount * h.stride, indexBytes = (size_t)h.indexCount * 4;
	if (b.size() != 5132 + vertexBytes + indexBytes)
	{
		char what[ 160 ];
		snprintf( what, sizeof( what ), "%s: %u bytes, the header says %u (5132 + %u vertices x %u + %u indices x 4)",
			path.c_str(), (unsigned)b.size(), (unsigned)(5132 + vertexBytes + indexBytes), (unsigned)h.vertexCount,
			(unsigned)h.stride, (unsigned)h.indexCount );
		error = what;
		return false;
	}
	out.vertices.assign( b.begin() + 5132, b.begin() + 5132 + vertexBytes );
	out.indices.resize( h.indexCount );
	if (h.indexCount)
		memcpy( &out.indices[0], &b[5132 + vertexBytes], indexBytes );
	out.hasProg = h.version == 2;
	if (out.hasProg && !readProg( dir + "/" + name + ".prog", out.prog, error ))
		return false;
	return true;
}

bool readTexture( const std::string &path, FFRef::Texture &out, std::string &error )
{
	std::vector<uint8_t> b;
	if (!readFile( path, b ))
	{
		error = "cannot open " + path;
		return false;
	}
	if (b.size() < 20 || memcmp( &b[0], "ZHTX", 4 ) != 0)
	{
		error = path + " is not a ZHTX texture";
		return false;
	}
	const uint32_t format = u32at( b, 4 );
	int w = (int)u32at( b, 8 ), h = (int)u32at( b, 12 );
	const uint32_t levels = u32at( b, 16 );
	out.type = FFRef::TEXTURE_2D;
	out.levels.clear();
	size_t at = 20;
	for (uint32_t l = 0; l < levels; ++l)
	{
		if (at + 4 > b.size())
		{
			error = path + ": short";
			return false;
		}
		const uint32_t size = u32at( b, at );
		at += 4;
		if (at + size > b.size())
		{
			error = path + ": short";
			return false;
		}
		FFRef::TextureLevel level;
		if (!decodeLevel( format, w, h, &b[at], size, level, error ))
		{
			error = path + ": " + error;
			return false;
		}
		out.levels.push_back( level );
		at += size;
		w = w > 1 ? w / 2 : 1;
		h = h > 1 ? h / 2 : 1;
	}
	if (at != b.size())
	{
		error = path + ": bytes left over";
		return false;
	}
	return true;
}

bool layoutFromFVF( uint32_t fvf, VertexLayout &out, std::string &error )
{
	// D3DFVF: the position mask 0x400E, then NORMAL 0x10, PSIZE 0x20, DIFFUSE 0x40, SPECULAR 0x80,
	// TEXCOUNT in bits 8-11, and each set's TEXCOORDSIZE in bits 16 + 2i (0: 2, 1: 3, 2: 4, 3: 1).
	memset( &out, 0, sizeof( out ) );
	unsigned size = 0;
	switch (fvf & 0x400E)
	{
		case 0x002: size = 12; break;
		case 0x004: size = 16; out.pretransformed = true; break;
		case 0x006: case 0x008: case 0x00A: case 0x00C: case 0x00E:
			out.blendWeights = (int)((fvf & 0xE) - 4) / 2;
			size = 12 + 4 * out.blendWeights;
			break;
		default:
		{
			char what[ 80 ];
			snprintf( what, sizeof( what ), "FVF 0x%X: a position this does not read", (unsigned)fvf );
			error = what;
			return false;
		}
	}
	if (fvf & 0x1000 || fvf & 0x8000)
	{
		error = "FVF LASTBETA_UBYTE4 or LASTBETA_D3DCOLOR is not read here";
		return false;
	}
	out.hasNormal = (fvf & 0x010) != 0;
	out.hasPSize = (fvf & 0x020) != 0;
	out.hasDiffuse = (fvf & 0x040) != 0;
	out.hasSpecular = (fvf & 0x080) != 0;
	size += (out.hasNormal ? 12 : 0) + (out.hasPSize ? 4 : 0) + (out.hasDiffuse ? 4 : 0) + (out.hasSpecular ? 4 : 0);
	out.texCoordSets = (int)((fvf >> 8) & 0xF);
	if (out.texCoordSets > 8)
	{
		error = "FVF with more than 8 texture-coordinate sets";
		return false;
	}
	for (int i = 0; i < out.texCoordSets; ++i)
	{
		static const int sizes[4] = { 2, 3, 4, 1 };
		out.texCoordSize[i] = sizes[(fvf >> (16 + 2 * i)) & 3];
		size += 4 * out.texCoordSize[i];
	}
	out.size = size;
	return true;
}

void decodeVertex( const VertexLayout &layout, const uint8_t *bytes, FFRef::Vertex &out )
{
	memset( &out, 0, sizeof( out ) );
	float f[4];
	size_t at = 0;
	memcpy( f, bytes, 12 );
	out.position[0] = f[0];
	out.position[1] = f[1];
	out.position[2] = f[2];
	out.position[3] = 1;
	at = 12;
	if (layout.pretransformed)
	{
		memcpy( f, bytes + 12, 4 );
		out.position[3] = f[0];
		at = 16;
	}
	at += 4 * layout.blendWeights;
	if (layout.hasNormal)
	{
		memcpy( f, bytes + at, 12 );
		out.normal[0] = f[0];
		out.normal[1] = f[1];
		out.normal[2] = f[2];
		at += 12;
	}
	if (layout.hasPSize)
		at += 4;
	uint32_t c;
	if (layout.hasDiffuse)
	{
		memcpy( &c, bytes + at, 4 );
		out.diffuse = FFRef::colorFromD3D( c );
		at += 4;
	}
	if (layout.hasSpecular)
	{
		memcpy( &c, bytes + at, 4 );
		out.specular = FFRef::colorFromD3D( c );
		at += 4;
	}
	for (int s = 0; s < layout.texCoordSets; ++s)
	{
		for (int k = 0; k < 4; ++k)
			out.tex[s][k] = 0;
		memcpy( f, bytes + at, 4 * layout.texCoordSize[s] );
		for (int k = 0; k < layout.texCoordSize[s]; ++k)
			out.tex[s][k] = f[k];
		at += 4 * layout.texCoordSize[s];
	}
}

bool drawState( const Capture &capture, const VertexLayout &layout, const FFRef::Texture *const textures[8],
		const FFRef::Program *vertexProgram, const FFRef::Program *pixelProgram, FFRef::DrawState &out,
		std::string &error )
{
	const Header &h = capture.header;
	out.setDefaults( (int)h.targetWidth, (int)h.targetHeight );
	memcpy( out.renderState, h.renderState, sizeof( out.renderState ) );
	memcpy( out.stageState, h.stageState, sizeof( out.stageState ) );
	memcpy( out.samplerState, h.samplerState, sizeof( out.samplerState ) );
	if (!h.depthBound)
	{
		// The description: "no depth-stencil bound; D3D9 then draws with Z and stencil off"
		out.renderState[FFRef::RS_ZENABLE] = FFRef::ZB_FALSE;
		out.renderState[FFRef::RS_ZWRITEENABLE] = 0;
		out.renderState[FFRef::RS_STENCILENABLE] = 0;
	}
	// SCISSORTESTENABLE: the capture holds no rectangle, and the replay sets none, so the device's default
	// applies - the whole target (the capture side's description of the replay) - which setDefaults gave the scissor.
	out.world = matrixFrom( h.world );
	out.view = matrixFrom( h.view );
	out.projection = matrixFrom( h.projection );
	for (int s = 0; s < 8; ++s)
		out.textureTransform[s] = matrixFrom( h.texture[s] );
	for (int l = 0; l < 8; ++l)
	{
		const uint8_t *p = h.lights[l];
		FFRef::Light &light = out.lights[l];
		uint32_t type;
		float f[25];
		memcpy( &type, p, 4 );
		memcpy( f, p + 4, 100 );
		light.enabled = h.lightEnabled[l] != 0;
		light.type = (int)type;
		light.diffuse = colourFrom( f );
		light.specular = colourFrom( f + 4 );
		light.ambient = colourFrom( f + 8 );
		for (int k = 0; k < 3; ++k)
		{
			light.position[k] = f[12 + k];
			light.direction[k] = f[15 + k];
		}
		light.range = f[18];
		light.falloff = f[19];
		light.attenuation0 = f[20];
		light.attenuation1 = f[21];
		light.attenuation2 = f[22];
		light.theta = f[23];
		light.phi = f[24];
	}
	out.material.diffuse = colourFrom( h.material );
	out.material.ambient = colourFrom( h.material + 4 );
	out.material.specular = colourFrom( h.material + 8 );
	out.material.emissive = colourFrom( h.material + 12 );
	out.material.power = h.material[16];
	out.viewport.x = h.viewport[0];
	out.viewport.y = h.viewport[1];
	out.viewport.width = h.viewport[2];
	out.viewport.height = h.viewport[3];
	out.viewport.minZ = h.minZ;
	out.viewport.maxZ = h.maxZ;
	for (int s = 0; s < 8; ++s)
		out.textures[s] = textures[s];
	out.vertexShaderBound = capture.hasProg && capture.prog.vertexPresent;
	out.pixelShaderBound = capture.hasProg && capture.prog.pixelPresent;
	out.vertexProgram = vertexProgram;
	out.pixelProgram = pixelProgram;
	if (capture.hasProg)
	{
		for (int r = 0; r < 96; ++r)
			for (int k = 0; k < 4; ++k)
				out.vertexConstants[r][k] = capture.prog.vertexConstants[r][k];
		for (int r = 0; r < 8; ++r)
			for (int k = 0; k < 4; ++k)
				out.pixelConstants[r][k] = capture.prog.pixelConstants[r][k];
	}
	out.pretransformed = layout.pretransformed;
	out.hasNormal = layout.hasNormal;
	out.hasDiffuse = layout.hasDiffuse;
	out.hasSpecular = layout.hasSpecular;
	out.texCoordSets = layout.texCoordSets;
	for (int s = 0; s < 8; ++s)
		out.texCoordSize[s] = s < layout.texCoordSets ? layout.texCoordSize[s] : 2;
	return true;
}

}  // namespace FFCapture
