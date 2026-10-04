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
** A captured draw (decision 7, A3's DONE: "every pipeline key checked").  With ZH_GPU_CAPTURE=<dir> the
** device writes the first draw of every distinct signature - the vertex program's key, the pixel
** program's and the pipeline's - with everything a replay needs to draw it again: the state as set, the
** vertices and indices it reads, and its textures.  test_ffref_capture replays each through the device on
** the GPU and through FFReference, and compares.
**
** What a capture does not hold: the target's pixels, depth and stencil before the draw.  A replay draws
** into a cleared target, so a draw that depends on what was under it (EQUAL depth, a stencil test,
** blending onto the frame) is checked against the clear, not against the frame.
**
** Version 1 skips a draw that samples a render target: its texels are the GPU's, and reading them back
** at the draw would flush the batch.  The writer counts what it skipped and why.
**
** Captures come from the user's install: they are written to a scratch directory and never committed.
** The files are raw structs of this build, not an interchange format - the same build writes and reads them.
**
**   <dir>/draw_<n>.cap      a DrawCaptureHeader, then vertex_count * stride vertex bytes, then index_count
**                            uint32_t indices (0 for a non-indexed draw), rebased to the first vertex stored
**   <dir>/<name>.tex        a DrawCaptureTexture, then each level's rows, packed, as the texture holds them;
**                            one file per texture content, shared by every capture that samples it
**   <dir>/draw_<n>.prog     version 2 only: "ZHPG", u32 1; the vertex section - u32 present, and if
**                            present char[64] registered name, u32 token count and the tokens as the device
**                            received them; then u32 element count and the bound declaration's elements,
**                            8 bytes each (u16 stream, u16 offset, u8 type, method, usage, usage index); the
**                            pixel section - u32 present, and if present its name, count and tokens; then
**                            f32 c0-c95 of the vertex shader constants and c0-c7 of the pixel ones, 4 each.
**                            .prog version 2 (capture v3) then appends: u32 count and the engine's own D3D8
**                            declaration tokens through D3DVSD_END (0 when the current declaration did not
**                            come from one); u32 stream count and per stream u32 stream, stride, and the
**                            byte offset of the draw's first vertex in the .cap's vertex bytes.
**                            Written from a prose description, so the reader shares no code with this writer.
*/

#pragma once

#ifndef DRAWCAPTURE_H
#define DRAWCAPTURE_H

#include "Platform/D3D9Posix.h"

#include <stdint.h>

/// DRAW_CAPTURE_VERSION: a fixed-function draw.  DRAW_CAPTURE_VERSION_PROGRAMMABLE: a draw with an engine
/// shader bound (A3e), whose draw_<n>.prog holds the programs - for the tests' vs_1_1/ps_1_1 interpreter (Tests/ffreference/ffprogram.h).
enum { DRAW_CAPTURE_VERSION = 1, DRAW_CAPTURE_VERSION_PROGRAMMABLE = 2, DRAW_CAPTURE_STAGES = 8, DRAW_CAPTURE_NAME = 48,
	DRAW_CAPTURE_SIGNATURE = 512, DRAW_CAPTURE_PROGRAM_NAME = 64, DRAW_CAPTURE_VS_CONSTANTS = 96,
	DRAW_CAPTURE_PS_CONSTANTS = 8 };

struct DrawCaptureHeader
{
	char Magic[4];										///< "ZHDC"
	uint32_t Version;
	uint32_t Primitive;									///< D3DPRIMITIVETYPE
	uint32_t PrimitiveCount;
	uint32_t FVF;
	uint32_t Stride;
	uint32_t VertexCount;
	uint32_t IndexCount;								///< 0: not indexed
	uint32_t TargetWidth;
	uint32_t TargetHeight;
	uint32_t TargetFormat;								///< D3DFORMAT of render target 0
	uint32_t DepthBound;								///< a depth-stencil surface was bound: 0 draws as D3D9 does, without depth
	D3DVIEWPORT9 Viewport;
	uint32_t RenderStates[256];
	uint32_t StageStates[DRAW_CAPTURE_STAGES][33];
	uint32_t SamplerStates[DRAW_CAPTURE_STAGES][14];
	D3DMATRIX World, View, Projection;
	D3DMATRIX TextureMatrices[DRAW_CAPTURE_STAGES];
	D3DMATERIAL9 Material;
	D3DLIGHT9 Lights[8];
	uint32_t LightsEnabled[8];
	char Textures[DRAW_CAPTURE_STAGES][DRAW_CAPTURE_NAME];	///< a .tex name per stage, "" for none
	char Signature[DRAW_CAPTURE_SIGNATURE];				///< the program keys and the pipeline key's hash
};

struct DrawCaptureTexture
{
	char Magic[4];										///< "ZHTX"
	uint32_t Format;									///< D3DFORMAT
	uint32_t Width;
	uint32_t Height;
	uint32_t Levels;
	// then, per level: uint32_t byte count, and that many bytes
};

#endif // DRAWCAPTURE_H
