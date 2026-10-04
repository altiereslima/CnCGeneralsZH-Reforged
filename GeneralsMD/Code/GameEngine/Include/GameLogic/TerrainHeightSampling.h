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

// FILE: TerrainHeightSampling.h //////////////////////////////////////////////////////////////////
// Desc:   The simulation's terrain queries, as arithmetic over a map's heights and cliff bits.
///////////////////////////////////////////////////////////////////////////////////////////////////

/* BaseHeightMapRenderObjClass's height maths, moved out of GameEngineDevice (T1).  Every unit's height,
	 every cliff test and every line of sight the simulation asks for was answered by the W3D terrain
	 render object, from the terrain visual's logical height map.  The arithmetic is here now, its text
	 unchanged, so that an engine without a W3D terrain answers the same questions the same way; the
	 render object's methods call these, so Windows runs the one copy.

	 What changed in the text, and only this:
	 - m_map, the render object's own map, is a parameter: renderMap where the original only tests it
		 for NULL, clipMap where getClipHeight reads it.  On Windows they are the same map
		 (DO_SEISMIC_SIMULATIONS is off), but which one each line read is kept.
	 - The line that chose the logical height map - TheTerrainVisual's, or m_map without one - stays in
		 the render object's methods, which pass what they chose as logicHeightMap.
	 - getMaxHeight(), the render object's highest sample, is the parameter maxHeight;
		 findMinMaxHeights is the pass in initHeightData that sets it.
	 Tests/terrain_golden.txt is what these must print for nine shipped maps, from the original code
	 run three ways (T1). */

#pragma once

#ifndef TERRAINHEIGHTSAMPLING_H
#define TERRAINHEIGHTSAMPLING_H

#include "Lib/BaseType.h"

class WorldHeightMapData;

class TerrainHeightSampling
{
public:
	/// The height sample at (x, y), clamped to the map: BaseHeightMapRenderObjClass::getClipHeight.
	static UnsignedByte getClipHeight(WorldHeightMapData *clipMap, Int x, Int y);
	/// The ground height at a world point, and its smoothed normal if asked: getHeightMapHeight.
	static Real getHeightMapHeight(WorldHeightMapData *clipMap, WorldHeightMapData *logicHeightMap, Real x, Real y, Coord3D* normal);
	/// Whether the terrain lets pos see posOther: isClearLineOfSight (Bresenham over the cells).
	static Bool isClearLineOfSight(WorldHeightMapData *renderMap, WorldHeightMapData *logicHeightMap, Real maxHeight, const Coord3D& pos, const Coord3D& posOther);
	/// The highest corner of the cell holding (x, y): getMaxCellHeight.
	static Real getMaxCellHeight(WorldHeightMapData *renderMap, WorldHeightMapData *logicHeightMap, Real x, Real y);
	/// Whether the cell holding (x, y) is a cliff cell: isCliffCell.
	static Bool isCliffCell(WorldHeightMapData *renderMap, WorldHeightMapData *logicHeightMap, Real x, Real y);
	/// The lowest and highest height samples on the map: initHeightData's min/max pass.
	static void findMinMaxHeights(WorldHeightMapData *pMap, Real &minHeight, Real &maxHeight);
};

#endif // TERRAINHEIGHTSAMPLING_H
