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
 * T1c, defect 17: the water grid steps once per logic frame, however many logic frames a pass runs.
 *
 * WaterGridMotion::updateForLogicFrame is the step moved out of WaterRenderObjClass::update; GameLogic::
 * update calls it (through TerrainVisual::updateWaterGrid) at the top of every logic frame.  This plays
 * Tests/water_grid_scenario.h through it that way, under EA's loop and three shapes of the fork's
 * catch-up, and requires every one to print what the ORIGINAL code printed under EA's loop.
 *
 * The golden is Tests/water_grid_oracle.cpp's first line: the original text, taken from the tree before T1 moved it, by
 * Tools/water_grid_oracle_extract.py and stepped on the client pass as W3DTerrainVisual::update did,
 * one pass per logic frame.  Identical from mingw-w64 under Wine, native arm64 and x86_64 under Rosetta.
 * The armed control is the oracle's other lines: the original under the catch-up schedules below, which
 * all differ from it (and whose grid is still moving at frame 240, where EA's has settled by 183).
 *
 * What it cannot see: that GameLogic::update makes the call, and makes it at the top of the frame - that
 * is the engine's loop, which needs the W3D device to run (read in the source, T1); the
 * client passes' own reads of the grid, which now see it a step earlier in a pass, and draw nothing
 * the simulation reads.
 *
 * Since logic runs at 60 frames a second the step is no longer the original's bits: its gravity is
 * per 60 Hz frame squared, its damping the square root of EA's and its at-rest velocity half of it,
 * so the original's 30 Hz line can no longer be the golden. The catch-up tests hold every schedule
 * to the moved code's own one-pass-per-frame line, computed in the same run, which is the claim T1c
 * makes. EA_LOOP_GOLDEN pins that line against a change to the step, and is that line as a run of
 * this binary prints it ("moved, passes of 1: ..."): regenerate it from a run whenever the step's
 * constants or the frame rate change on purpose.
 */

#include "test_harness.h"

#include "PreRTS.h"
#include "Common/GameCommon.h"
#include "Common/GameMemory.h"
#include "Common/GlobalData.h"
#include "Common/NameKeyGenerator.h"
#include "GameLogic/WaterGridMotion.h"

#include <string>
#include <vector>

#include "water_grid_scenario.h"

namespace {

// The moved code at 60 Hz under one pass per logic frame, as test_water_grid prints it. The 30 Hz
// original's line (water_grid_oracle, "original passes of 1") was "frames 240 all 96559c89 last a7eea444
// moving 183". This is the "moved, passes of 1" line taken from a run of the 60 Hz step.
const char *const EA_LOOP_GOLDEN = "frames 240 all 5c9f1917 last ac3c3fa9 moving 240";

void boot()
{
	static bool booted = false;
	if (booted)
		return;
	booted = true;
	initMemoryManager();
	TheNameKeyGenerator = NEW NameKeyGenerator;
	TheNameKeyGenerator->init();
	TheWritableGlobalData = NEW GlobalData;
	TheWritableGlobalData->m_gravity = ConvertAccelerationInSecsToFrames( -64.0f );	// GameData.ini's
}

// The engine as it is now: nothing on the client pass, the step at the top of each logic frame.
struct MovedEngine
{
	typedef WaterGridMotion::MeshPoint Point;
	std::vector<Point> points;
	Bool moving;
	int callsPerFrame;
	MovedEngine( int calls = 1 ) : points( water_grid::POINTS_X * water_grid::POINTS_Y ), moving( FALSE ), callsPerFrame( calls ) {}
	Point *mesh( void ) { return &points[0]; }
	Bool &inMotion( void ) { return moving; }
	void clientPass( UnsignedInt ) {}
	void logicFrameTop( UnsignedInt frame )
	{
		for (int i = 0; i < callsPerFrame; ++i)
			WaterGridMotion::updateForLogicFrame( frame, TRUE, moving, &points[0], water_grid::CELLS_X, water_grid::CELLS_Y );
	}
};

unsigned nextFirstFrame()
{
	static unsigned run = 0;
	return 1 + 1000 * run++;	// the step's last-frame gate is a static: no run starts where one ended
}

// The moved code under EA's loop, one pass per logic frame: what every other schedule has to print.
std::string eaLoopLine( void )
{
	MovedEngine engine;
	return water_grid::run( engine, std::vector<int>( 1, 1 ), nextFirstFrame() );
}

}  // namespace

TEST(water_grid_scenario_uses_the_engines_gravity)
{
	// the scenario's gravity is the oracle's, per 30 Hz frame squared; the engine's is per 60 Hz frame
	// squared, a quarter of it, and exactly so: halving 1/30 only moves the exponent
	boot();
	const Real engine = ConvertAccelerationInSecsToFrames( -64.0f );
	const float scenario = ConvertRetailPerFrameSquared( water_grid::shippedGravity() );
	CHECK( memcmp( &engine, &scenario, sizeof( engine ) ) == 0 );
}

TEST(water_grid_steps_as_eas_loop_did_under_eas_loop)
{
	boot();
	const std::string line = eaLoopLine();
	printf( "  moved, passes of 1: %s\n", line.c_str() );
	CHECK_STR( line.c_str(), EA_LOOP_GOLDEN );
}

TEST(water_grid_k_logic_frames_in_one_pass_equal_k_single_passes)
{
	boot();
	const std::string eaLoop = eaLoopLine();
	const std::vector<std::vector<int> > schedules = water_grid::schedules();
	for (size_t s = 0; s < schedules.size(); ++s)
	{
		MovedEngine engine;
		const std::string line = water_grid::run( engine, schedules[s], nextFirstFrame() );
		printf( "  moved, passes of %s: %s\n", water_grid::describe( schedules[s] ).c_str(), line.c_str() );
		CHECK_STR( line.c_str(), eaLoop.c_str() );
	}
}

TEST(water_grid_a_second_call_for_the_same_frame_does_not_step_again)
{
	/* A frozen or held logic frame calls GameLogic::update again without advancing the frame; the
		 gate, EA's own, keeps it to one step, as one client pass per unchanged frame did. */
	boot();
	const std::string eaLoop = eaLoopLine();
	MovedEngine engine( 3 );
	const std::string line = water_grid::run( engine, std::vector<int>( 1, 2 ), nextFirstFrame() );
	CHECK_STR( line.c_str(), eaLoop.c_str() );
}

TEST(water_grid_unstepped_is_not_the_golden)
{
	// So the reference means something: with no step at all the result is different
	boot();
	const std::string eaLoop = eaLoopLine();
	struct Unstepped : MovedEngine { void logicFrameTop( UnsignedInt ) {} } still;
	const std::string line = water_grid::run( still, std::vector<int>( 1, 1 ), nextFirstFrame() );
	CHECK( line != eaLoop );
}
