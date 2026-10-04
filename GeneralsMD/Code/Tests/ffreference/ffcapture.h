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
 * ffcapture: the device's draw captures, read for FFReference's own replay (L2's oracle side).
 *
 * INDEPENDENCE RECORD.  Written from the capture side's written descriptions of the capture format (v1, v2's .prog,
 * v3's D3D8 declaration) and from Microsoft's pages for D3DFVF, D3DFORMAT and the BC formats.  Its author did
 * not read the device's capture writer (PosixDevice9Capture.cpp), the harness's reader
 * (test_ffref_capture.cpp) or any other A3 code.  A disagreement between this reader and the writer is
 * a finding about the description.
 *
 * WHAT IT READS
 *   draw_NNNNN.cap  "ZHDC", version 1 (fixed function) or 2 (a .prog beside it); a 5132-byte header of
 *                   the draw's state, then VertexCount x Stride vertex bytes from the first vertex the
 *                   draw reads, then IndexCount u32 indices rebased to that vertex.
 *   NAME.tex        "ZHTX", D3DFORMAT, width, height, levels; per level a u32 byte count and the bytes.
 *   draw_NNNNN.prog "ZHPG", version 1 or 2: the vertex and pixel programs' tokens, the D3D9 elements,
 *                   the constants, and (2) the engine's D3D8 declaration and the streams.
 * Decoded here: the vertex format (D3DFVF), and the texture formats the captures hold (A8R8G8B8,
 * X8R8G8B8, R5G6B5, A4R4G4B4, A1R5G5B5, X1R5G5B5, A8, L8, A8L8, DXT1, DXT3, DXT5); others are refused
 * by name, never guessed.
 */

#ifndef FFREFERENCE_FFCAPTURE_H
#define FFREFERENCE_FFCAPTURE_H

#include "ffreference/ffreference.h"
#include "ffreference/ffprogram.h"

#include <map>
#include <stdint.h>
#include <string>
#include <vector>

namespace FFCapture {

struct Header
{
	uint32_t version, primitiveType, primitiveCount, fvf, stride, vertexCount, indexCount;
	uint32_t targetWidth, targetHeight, targetFormat, depthBound;
	uint32_t viewport[4];		///< X, Y, Width, Height
	float minZ, maxZ;
	uint32_t renderState[256];
	uint32_t stageState[8][33];
	uint32_t samplerState[8][14];
	float world[16], view[16], projection[16], texture[8][16];
	float material[17];			///< diffuse, ambient, specular, emissive (r, g, b, a each), power
	uint8_t lights[8][104];		///< D3DLIGHT9, as written
	uint32_t lightEnabled[8];
	std::string textures[8];	///< a .tex name per stage, "" for none
	std::string signature;
};

struct Prog
{
	uint32_t version;
	bool vertexPresent, pixelPresent;
	std::string vertexName, pixelName;
	std::vector<uint32_t> vertexTokens, pixelTokens;
	std::vector<FFRef::DeclarationElement> elements;	///< the D3D9 declaration, when it was the current format
	float vertexConstants[96][4], pixelConstants[8][4];
	std::vector<uint32_t> d3d8;		///< version 2: the engine's D3D8 declaration, through D3DVSD_END
	struct Stream { uint32_t stream, stride, first; };
	std::vector<Stream> streams;
};

struct Capture
{
	std::string name;			///< draw_NNNNN
	Header header;
	std::vector<uint8_t> vertices;
	std::vector<uint32_t> indices;
	bool hasProg;
	Prog prog;
};

/// One .cap (and its .prog when version 2).  false: unreadable, with why.
bool readCapture( const std::string &dir, const std::string &name, Capture &out, std::string &error );

/// A .tex, decoded into FFReference's texture (level 0 first; texels from the top row).  false:
/// unreadable or a format this does not decode, with why.
bool readTexture( const std::string &path, FFRef::Texture &out, std::string &error );

/// D3DFVF: the vertex layout, and each vertex decoded.  false: an FVF bit this does not read, or a
/// stride that disagrees with the FVF.
struct VertexLayout
{
	bool pretransformed, hasNormal, hasPSize, hasDiffuse, hasSpecular;
	int blendWeights;			///< XYZB1..B5
	int texCoordSets, texCoordSize[8];
	unsigned size;				///< bytes the FVF describes
};
bool layoutFromFVF( uint32_t fvf, VertexLayout &out, std::string &error );
void decodeVertex( const VertexLayout &layout, const uint8_t *bytes, FFRef::Vertex &out );

/// The capture's state as FFReference's DrawState: every state the header carries, the vertex format,
/// the textures (from `textures`, by stage), and the programs (from `programs`, owned by the caller).
/// false: a state this cannot give FFReference faithfully (it says which).
bool drawState( const Capture &capture, const VertexLayout &layout, const FFRef::Texture *const textures[8],
		const FFRef::Program *vertexProgram, const FFRef::Program *pixelProgram, FFRef::DrawState &out,
		std::string &error );

}  // namespace FFCapture

#endif
