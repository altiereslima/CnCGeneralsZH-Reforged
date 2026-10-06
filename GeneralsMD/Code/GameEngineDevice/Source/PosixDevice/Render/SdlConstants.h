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
// Portions adapted from GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/dx11backend.h by Olcay Seygan (upstream CnCGeneralsZH-Reforged), GPL-3.0-or-later.

/*
** The constants a generated program reads (decision 7, phase A3c), pushed with SDL_PushGPUVertexUniformData
** and SDL_PushGPUFragmentUniformData at slot 0.  Laid out as the generators declare their blocks
** (ffvertex.cpp's `cbuffer VertexPipeline`, ffshader.cpp's `cbuffer CombinerConstants`), which is
** dx11backend's VertexConstantBlock and the first three fields of its PixelConstantBlock: a program
** declares a prefix of its block, and a longer push is legal.  Every field is a float4 or a row-major
** float4x4, so HLSL's packing and std140, which SDL3 asks for, agree.
**
** The pixel block stops after the alpha reference: the fields after it serve the Direct3D 11 backend's
** own additions (normal maps, the shadow map, the sky), which A3 does not draw - A3 draws the Direct 3D 9
** picture.
*/

#pragma once

#ifndef SDLCONSTANTS_H
#define SDLCONSTANTS_H

#include "ffvertex.h"		// MAXIMUM_VERTEX_STAGES, MAXIMUM_VERTEX_LIGHTS

struct SdlVertexConstants
{
	float WorldViewProjection[16];
	float WorldView[16];
	float NormalTransform[16];		///< the inverse transpose of WorldView
	float TextureMatrix[MAXIMUM_VERTEX_STAGES][16];
	float MaterialAmbient[4];
	float MaterialDiffuse[4];
	float MaterialSpecular[4];
	float MaterialEmissive[4];
	float MaterialPower[4];
	float GlobalAmbient[4];
	float FogParameters[4];			///< start, end, density
	float ViewportInverse[4];		///< one over the viewport's width and height
	float LightFields[MAXIMUM_VERTEX_LIGHTS][VERTEX_REGISTERS_PER_LIGHT][4];	///< position, direction, diffuse, specular, attenuation, spot, ambient
};

struct SdlPixelConstants
{
	float TextureFactor[4];
	float FogColour[4];
	float AlphaReference[4];		///< .x is D3DRS_ALPHAREF as a whole level, 0 to 255
};

/// What an engine pixel program's b0 declares (A3e, engineshader.cpp's write_pixel_preamble): the Direct3D
/// 11 backend's PixelConstantBlock, field for field.  The first three are SdlPixelConstants; the rest -
/// the normal mapped lights, the terrain's sun, the shadow and the sky - are unread padding from the
/// normal maps taken out on 2026-10-06 and the fork's Direct3D 11 additions, which A3e does not
/// draw, and stay zero: zero shadow parameters
/// read as a pixel the sun reaches.
struct SdlEnginePixelConstants
{
	SdlPixelConstants Combiner;
	float NormalLightDirection[4][4];
	float NormalLightDiffuse[4][4];
	float NormalMapParameters[4];
	float TerrainSunDirection[4];
	float ShadowFromClip[16];
	float ShadowParameters[4];
	float ShadowViewport[4];
	float ShadowSoftness[4];
	float Sky[4];
	float SkyUp[4];
};
static_assert(sizeof(SdlEnginePixelConstants) == 22 * 16, "PixelConstantBlock's 22 float4s");

static_assert(sizeof(SdlVertexConstants) % 16 == 0 && sizeof(SdlPixelConstants) % 16 == 0,
	"whole float4s, as std140 lays them out");
static_assert(sizeof(SdlVertexConstants) == (3 + MAXIMUM_VERTEX_STAGES) * 64 + 8 * 16 + MAXIMUM_VERTEX_LIGHTS * VERTEX_REGISTERS_PER_LIGHT * 16,
	"the generated VertexPipeline block, field for field");

#endif // SDLCONSTANTS_H
