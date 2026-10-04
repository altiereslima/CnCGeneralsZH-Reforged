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
 * W3DRoadBuffer's column count, the one rule of its road mesh that can be checked without a map: one
 * column a cell, at least two, and never more than the map is long corner to corner.  A road point far
 * outside the map (map data) used to ask for 2^31 columns on ARM64, whose conversion saturates, and the
 * loop that walks them had no other way out than a vertex budget four times the stack array it guarded.
 */
#include "test_harness.h"

#include "PreRTS.h"
#include "W3DDevice/GameClient/W3DRoadBuffer.h"

#include <limits>

// The names the game's executable supplies, as test_install_textures supplies them.
static char s_noAppPrefix[] = "";
const Char *g_strFile = "data\\Generals.str";
const Char *g_csfFile = "data\\%s\\Generals.csf";
char *gAppPrefix = s_noAppPrefix;
RenderWindow ApplicationHWnd = NULL;
Bool ApplicationIsBorderless = FALSE;

TEST(road_columns_are_one_a_cell_at_least_two_and_never_longer_than_the_map)
{
	const Int x = 100, y = 100;								// a 100 x 100 cell map, 1,000 units a side
	CHECK_EQ(W3DRoadBuffer::roadColumnCountFor(0.0f, x, y), 2);
	CHECK_EQ(W3DRoadBuffer::roadColumnCountFor(5.0f, x, y), 2);
	CHECK_EQ(W3DRoadBuffer::roadColumnCountFor(100.0f, x, y), 11);
	CHECK_EQ(W3DRoadBuffer::roadColumnCountFor(896.0f, x, y), 90);	// Tournament Desert's longest, measured
	CHECK_EQ(W3DRoadBuffer::roadColumnCountFor(1414.0f, x, y), 142);	// corner to corner: unchanged

	// past the map: capped at its extents, where the count was the length's
	CHECK_EQ(W3DRoadBuffer::roadColumnCountFor(1.0e9f, x, y), x + y + 2);
	// past the int range, and not a number: Windows' conversion, INT_MIN, then the floor of two
	CHECK_EQ(W3DRoadBuffer::roadColumnCountFor(3.0e10f, x, y), 2);
	CHECK_EQ(W3DRoadBuffer::roadColumnCountFor(std::numeric_limits<float>::infinity(), x, y), 2);
	CHECK_EQ(W3DRoadBuffer::roadColumnCountFor(std::numeric_limits<float>::quiet_NaN(), x, y), 2);
	// no map at all: the floor
	CHECK_EQ(W3DRoadBuffer::roadColumnCountFor(500.0f, 0, 0), 2);
}
