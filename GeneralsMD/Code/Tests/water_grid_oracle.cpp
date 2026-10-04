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
 * T1c's oracle: the ORIGINAL water grid step, run the way the engine ran it.
 *
 * Tools/water_grid_oracle_extract.py takes WaterRenderObjClass::update's logic-frame block and the mesh
 * point type out of a commit before the fix, unchanged, into water_grid_original.inc.  This file wraps
 * the block in a stand-in WaterRenderObjClass holding only the members it touches, with stand-ins for
 * TheGameLogic (the frame) and TheGlobalData (the gravity), and plays Tests/water_grid_scenario.h
 * through it: the step on each client pass, as W3DTerrainVisual::update called it.
 *
 * Its output under the one-pass-per-frame schedule is EA's loop, and is the golden test_water_grid
 * holds the moved code to under every schedule.  Its output under the others is the armed control:
 * the original code with the fork's catch-up, which must NOT match, or the test proves nothing.
 *
 * Not a ctest test: it needs the extraction, which needs git and the commit.  Built by hand:
 *   python3 Tools/water_grid_oracle_extract.py <commit> <dir>/water_grid_original.inc
 *   c++ -std=c++17 -O2 -ffp-contract=off -fsigned-char -I<dir> -I Tests -I Libraries/Include \
 *       -I GameEngine/Include Tests/water_grid_oracle.cpp -o water_grid_oracle
 * and the same with x86_64-w64-mingw32-g++ for Wine (T1).
 */

#include "Lib/BaseType.h"

#include <math.h>
#include <stdio.h>
#include <vector>

#include "water_grid_scenario.h"

struct OracleGameLogic
{
	UnsignedInt m_frame;
	UnsignedInt getFrame( void ) { return m_frame; }
};
struct OracleGlobalData
{
	Real m_gravity;
};
static OracleGameLogic theOracleGameLogic;
static OracleGlobalData theOracleGlobalData;
static OracleGameLogic *TheGameLogic = &theOracleGameLogic;
static const OracleGlobalData *TheGlobalData = &theOracleGlobalData;

class WaterRenderObjClass
{
public:
#define WATER_ORACLE_MEMBERS
#include "water_grid_original.inc"
#undef WATER_ORACLE_MEMBERS

	WaterMeshData *m_meshData;
	Bool m_meshInMotion;
	Bool m_doWaterGrid;
	Int m_gridCellsX;
	Int m_gridCellsY;

	// the rest of the original update() is the wall-clock scenery, which touches none of this
	void update( void )
	{
#define WATER_ORACLE_UPDATE
#include "water_grid_original.inc"
#undef WATER_ORACLE_UPDATE
	}
};

// The engine as it was: the step on the client pass, nothing at the top of a logic frame.
struct OriginalEngine
{
	typedef WaterRenderObjClass::WaterMeshData Point;
	WaterRenderObjClass water;
	std::vector<Point> points;
	OriginalEngine() : points( water_grid::POINTS_X * water_grid::POINTS_Y )
	{
		water.m_meshData = &points[0];
		water.m_meshInMotion = FALSE;
		water.m_doWaterGrid = TRUE;
		water.m_gridCellsX = water_grid::CELLS_X;
		water.m_gridCellsY = water_grid::CELLS_Y;
	}
	Point *mesh( void ) { return &points[0]; }
	Bool &inMotion( void ) { return water.m_meshInMotion; }
	void clientPass( UnsignedInt frame ) { TheGameLogic->m_frame = frame; water.update(); }
	void logicFrameTop( UnsignedInt ) {}
};

int main( void )
{
	theOracleGlobalData.m_gravity = water_grid::shippedGravity();
	const std::vector<std::vector<int> > schedules = water_grid::schedules();
	for (size_t s = 0; s < schedules.size(); ++s)
	{
		OriginalEngine engine;
		const std::string line = water_grid::run( engine, schedules[s], 1 + 1000 * (unsigned)s );
		printf( "original passes of %s: %s\n", water_grid::describe( schedules[s] ).c_str(), line.c_str() );
	}
	return 0;
}
