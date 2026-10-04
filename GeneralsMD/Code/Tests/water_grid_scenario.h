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
 * T1c's water grid scenario, shared by Tests/water_grid_oracle.cpp (the ORIGINAL code, stepped from the
 * render object's client-pass update) and Tests/test_water_grid.cpp (the moved code, stepped from the
 * top of each logic frame), so the two cannot drift.
 *
 * A 12 x 9 cell grid at rest at height 10.  Every third logic frame of the first half (the second is
 * left to settle, so the step's at-rest branch runs too) something pushes velocity into a
 * 3 x 3 box of points, as WaveGuideUpdate does through addWaterVelocity during a logic frame: the new
 * preferred height, the velocity added, the point marked in motion and the mesh marked moving - the
 * four things WaterRenderObjClass::addVelocity writes.  The box and the push change with the frame, so
 * the grid holds points at every stage from just pushed to settled.
 *
 * run() plays FRAMES logic frames through an Engine, grouped into passes as the schedule says: one
 * client pass, then that many logic frames, as GameEngine::update does (a schedule of all 1s is EA's
 * loop; anything larger is this fork's catch-up).  Per logic frame: the Engine's logicFrameTop, the
 * push, the frame counter moves on.  It returns one line: the hash of every point's bits after every
 * frame, the last frame's own hash, and how many of the frames ended with the mesh in motion.
 *
 * The gravity is the shipped one: GameData.ini's Gravity = -64.0, converted per logic frame as
 * INI::parseAccelerationReal does (test_water_grid checks that against the engine's own conversion).
 */
#ifndef WATER_GRID_SCENARIO_H
#define WATER_GRID_SCENARIO_H

#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

namespace water_grid {

enum { CELLS_X = 12, CELLS_Y = 9, FRAMES = 240, PUSH_EVERY = 3 };
enum { POINTS_X = CELLS_X + 1 + 2, POINTS_Y = CELLS_Y + 1 + 2 };	// the mesh's size, border included

inline float shippedGravity()
{
	const float secondsPerFrame = 1.0f / 30.0f;
	return -64.0f * (secondsPerFrame * secondsPerFrame);
}

inline unsigned fnv(unsigned hash, const void *data, size_t size)
{
	const unsigned char *bytes = (const unsigned char *)data;
	for (size_t i = 0; i < size; ++i)
		hash = (hash ^ bytes[i]) * 16777619u;
	return hash;
}

/* Engine: has `Point` (the mesh point type: height, velocity, status, preferredHeight), `Point *mesh()`,
	 `bool &inMotion()`, and clientPass(frame) and logicFrameTop(frame), which step the grid where the
	 code under test steps it.  The frame numbers start at `firstFrame`: both versions keep the last
	 frame they stepped in a function-level static, so every run in one process starts at a frame no
	 earlier run ended on.  A first step onto a mesh at rest changes nothing, so where a run starts
	 does not show in its result. */
template<class Engine>
std::string run(Engine &engine, const std::vector<int> &schedule, unsigned firstFrame)
{
	typedef typename Engine::Point Point;
	Point *mesh = engine.mesh();
	for (int i = 0; i < POINTS_X * POINTS_Y; ++i)
	{
		mesh[i].height = 10.0f;
		mesh[i].velocity = 0.0f;
		mesh[i].status = 0;
		mesh[i].preferredHeight = 10;
	}
	engine.inMotion() = false;

	unsigned frame = firstFrame, all = 2166136261u, last = 0;
	int moving = 0, played = 0;
	for (size_t pass = 0; played < FRAMES; ++pass)
	{
		const int frames = schedule[pass % schedule.size()];
		engine.clientPass(frame);
		for (int k = 0; k < frames && played < FRAMES; ++k, ++played)
		{
			engine.logicFrameTop(frame);

			// the frame's push, where WaveGuideUpdate's would be; none in the second half, so it settles
			if (played < FRAMES / 2 && played % PUSH_EVERY == 0)
			{
				const int cx = 1 + (played * 7) % (CELLS_X - 1), cy = 1 + (played * 5) % (CELLS_Y - 1);
				const unsigned char preferred = (unsigned char)(8 + played % 5);
				const float push = 0.75f * (float)(1 + played % 4);
				for (int y = cy - 1; y <= cy + 1; ++y)
					for (int x = cx - 1; x <= cx + 1; ++x)
					{
						Point &p = mesh[(y + 1) * POINTS_X + x + 1];
						p.preferredHeight = preferred;
						p.velocity = p.velocity + push;
						p.status |= 0x01;	// IN_MOTION
					}
				engine.inMotion() = true;
			}

			++frame;
			last = 2166136261u;
			for (int i = 0; i < POINTS_X * POINTS_Y; ++i)
			{
				last = fnv(last, &mesh[i].height, sizeof(mesh[i].height));
				last = fnv(last, &mesh[i].velocity, sizeof(mesh[i].velocity));
				last = fnv(last, &mesh[i].status, 1);
				last = fnv(last, &mesh[i].preferredHeight, 1);
			}
			all = fnv(all, &last, sizeof(last));
			moving += engine.inMotion() ? 1 : 0;
		}
	}
	char line[128];
	snprintf(line, sizeof(line), "frames %d all %08x last %08x moving %d", FRAMES, all, last, moving);
	return line;
}

// The schedules both files run: EA's loop, and three shapes of catch-up.
inline std::vector<std::vector<int> > schedules()
{
	std::vector<std::vector<int> > s;
	s.push_back(std::vector<int>(1, 1));
	s.push_back(std::vector<int>(1, 3));
	s.push_back(std::vector<int>(1, 5));
	const int mixed[] = { 1, 4, 2, 1, 6, 3, 1, 1, 5, 2 };
	s.push_back(std::vector<int>(mixed, mixed + sizeof(mixed) / sizeof(mixed[0])));
	return s;
}

inline std::string describe(const std::vector<int> &schedule)
{
	std::string text;
	for (size_t i = 0; i < schedule.size(); ++i)
	{
		char n[16];
		snprintf(n, sizeof(n), "%s%d", i ? "," : "", schedule[i]);
		text += n;
	}
	return text;
}

}  // namespace water_grid

#endif
