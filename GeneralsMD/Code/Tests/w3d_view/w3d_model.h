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
 * One W3D model, flattened for drawing: every rigid mesh an HLOD's top level of detail places on a
 * bone, moved into model space by that bone's transform, split into draws by texture and shader.
 *
 * Read with WWLib's ChunkLoadClass and w3d_file.h's structs, the same reader and layouts
 * WW3D2/meshmdlio.cpp, htree.cpp and hlod.cpp use.  Pivot transforms are built with WWMath as
 * HTreeClass::read_pivots does.  What it leaves out, because one static model does not need it:
 * skins (vertex influences), animation, aggregates, proxies, damage stages, lower LODs, and every
 * material pass and texture stage after the first.
 */
#pragma once

#include <string>
#include <vector>

struct ModelVertex
{
	float position[3];	// model space, Z up
	float normal[3];
	float texCoord[2];	// top-left origin, as WW3D2 hands them to Direct3D
};

// What the first pass's W3dShaderStruct asks of the pipeline, reduced to what differs between the
// Crusader's draws.
struct DrawState
{
	bool alphaTest;
	bool blend;			// SRC_ALPHA / ONE_MINUS_SRC_ALPHA; the only blend the spike maps
	bool additive;		// ONE / ONE
	bool depthWrite;
	bool twoSided;
	bool operator<(const DrawState &o) const;
	bool operator==(const DrawState &o) const;
};

struct ModelDraw
{
	std::string meshName;
	std::string textureName;	// as the W3D names it, e.g. "avcrusader.tga"; empty for none
	float diffuse[4];			// vertex material diffuse and opacity
	bool houseColor;			// a HOUSECOLOR mesh: tinted with the player's colour
	DrawState state;
	unsigned firstIndex;
	unsigned indexCount;
};

struct Model
{
	std::string name;
	std::vector<ModelVertex> vertices;
	std::vector<unsigned> indices;
	std::vector<ModelDraw> draws;
	float boundsMin[3];
	float boundsMax[3];
	std::vector<std::string> notes;	// what was skipped and why, printed by the viewer
};

bool loadW3dModel(const std::vector<unsigned char> &file, Model *model, std::string *why);
