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
 * The .wak loader's two rules.  A map's .wak file sits beside it, and a network map transfer accepts
 * .wak, so what it says is not trusted: a wave type outside the wave table is skipped, and the track
 * count is capped by what the file holds.  Every shipped .wak (25, checked when this was written)
 * already obeys both, so the loader reads them as before.
 */
#include "test_harness.h"

#include "PreRTS.h"
#include "W3DDevice/GameClient/HeightMap.h"	// what W3DWaterTracks.h leans on, as W3DWaterTracks.cpp has it
#include "W3DDevice/GameClient/W3DWaterTracks.h"

#include <limits.h>

// The names the game's executable supplies, as test_install_textures supplies them.
static char s_noAppPrefix[] = "";
const Char *g_strFile = "data\\Generals.str";
const Char *g_csfFile = "data\\%s\\Generals.csf";
char *gAppPrefix = s_noAppPrefix;
RenderWindow ApplicationHWnd = NULL;
Bool ApplicationIsBorderless = FALSE;

TEST(wak_wave_types_are_the_wave_tables_entries)
{
	for (Int type = 0; type < 6; ++type)
		CHECK(isWakWaveType(type));			// pond, ocean, close ocean, close double, radial, stationary
	CHECK(!isWakWaveType(6));
	CHECK(!isWakWaveType(-1));
	CHECK(!isWakWaveType(INT_MAX));
	CHECK(!isWakWaveType(INT_MIN));
}

TEST(wak_track_count_never_passes_what_the_file_holds)
{
	// twenty bytes a record, then the four-byte count
	CHECK_EQ(wakTrackCount(4 + 3 * 20, 3), 3);				// as shipped files are
	CHECK_EQ(wakTrackCount(4 + 3 * 20, 2), 2);				// fewer than it holds: as claimed
	CHECK_EQ(wakTrackCount(4 + 3 * 20, 4), 3);				// more: what is there
	CHECK_EQ(wakTrackCount(4 + 3 * 20, INT_MAX), 3);
	CHECK_EQ(wakTrackCount(4 + 3 * 20, -5), 0);
	CHECK_EQ(wakTrackCount(4 + 3 * 20 + 7, 5), 3);			// a torn last record is not read
	CHECK_EQ(wakTrackCount(4, 1), 0);
	CHECK_EQ(wakTrackCount(0, 1), 0);
	CHECK_EQ(wakTrackCount(-1, 1), 0);
}
