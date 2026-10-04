/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
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
// Modified 2026 by İlyas Akın for the macOS/Linux port: parts come from GeneralsMD/Code/GameEngineDevice/Include/W3DDevice/GameClient/W3DWater.h, the rest is new code, copyright 2026 İlyas Akın; see NOTICE.md and the git history.

// FILE: WaterGridMotion.h ////////////////////////////////////////////////////////////////////////
// Desc:   The water grid's mesh motion, one step per logic frame (T1c, defect 17).
///////////////////////////////////////////////////////////////////////////////////////////////////

/* The water grid is simulation state kept in a render object.  On a map with a WaveGuide1 waypoint,
	 WaveGuideUpdate pushes velocity into WaterRenderObjClass's mesh and TerrainLogic::isUnderwater reads
	 the mesh's heights back, for pathfinding, locomotors, the partition manager and more.  Between the
	 two, the mesh moves: every point in motion is damped, pulled towards its preferred height and moved.

	 EA stepped that motion from the render object's update, on the client pass, at most once a pass,
	 gated on the logic frame having changed.  EA's loop ran one client pass per logic frame, so that was
	 one step a frame.  This fork's catch-up runs several logic frames behind one client pass, and a
	 machine that caught up k frames stepped the mesh once: grid heights came to depend on the machine's
	 frame-rate history (defect 17).

	 GameLogic::update now steps it, at its top, before anything in the frame reads or writes the grid -
	 where EA's client pass stood, just ahead of the logic frame - through TerrainVisual::updateWaterGrid.
	 The step is W3DWater.cpp's text unchanged, frame gate and all, but for these substitutions:
	 - currLogicFrame is a parameter where the original read TheGameLogic->getFrame() (0 without one);
	 - the render object's m_doWaterGrid, m_meshInMotion, m_meshData, m_gridCellsX and m_gridCellsY are
		 parameters of the same names without the m_;
	 - WaterRenderObjClass::IN_MOTION and WaterMeshData are this class's IN_MOTION and MeshPoint, which
		 WaterRenderObjClass now uses under its old names (the same members in the same order).
	 Tests/test_water_grid.cpp holds it to the original's own output, and shows the count per frame. */

#pragma once

#ifndef WATERGRIDMOTION_H
#define WATERGRIDMOTION_H

#include "Lib/BaseType.h"

class WaterGridMotion
{
public:

	enum
	{
		IN_MOTION = 0x01		///< WaterRenderObjClass::WaterMeshStatus's
	};

	/// One point of the grid's mesh: WaterRenderObjClass::WaterMeshData, which is this type
	struct MeshPoint
	{
		Real height;										///< height of the 3D mesh at this point
		Real velocity;									///< velocity in Z that this point is moving up and down
		UnsignedByte status;						///< status for this grid point
		UnsignedByte preferredHeight;		///< the hight we prefer to be
	};

	/** Moves every point in motion one step, the first time it is called for a given logic frame; a
		* second call for the same frame does nothing.  meshData holds (gridCellsX+1+2)*(gridCellsY+1+2)
		* points, a border of one around the grid. */
	static void updateForLogicFrame( UnsignedInt currLogicFrame, Bool doWaterGrid, Bool &meshInMotion,
		MeshPoint *meshData, Int gridCellsX, Int gridCellsY );

};

#endif // WATERGRIDMOTION_H
