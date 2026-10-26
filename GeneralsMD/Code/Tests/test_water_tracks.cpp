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

#include "Platform/WaterShoreOverlay.h"
#include <limits>

TEST(shore_overlay_accepts_whole_same_level_convex_coverage_both_windings)
{
    using namespace WaterShoreOverlay;
    Quad patch={{0,0,100,0,100,100,0,100},7},track={{10,20,60,20,60,70,10,70},7};
    CHECK(Contains(patch,track));
    for(unsigned i=0;i<4;++i) {std::swap(patch.xy[0],patch.xy[4]);std::swap(patch.xy[1],patch.xy[5]);CHECK(Contains(patch,track));}
    Quad crossing=track;crossing.xy[0]=-1;CHECK(!Contains(patch,crossing));
    Quad other=track;other.level=7.1f;CHECK(!Contains(patch,other));
    other=track;other.xy[0]=std::numeric_limits<float>::quiet_NaN();CHECK(!Contains(patch,other));
    other=track;other.xy[4]=other.xy[2];other.xy[5]=other.xy[3];CHECK(!Contains(patch,other));
    Quad concave={{0,0,100,0,25,25,0,100},7};CHECK(!Contains(concave,track));
    // Real fallback: a surf quad crossing two adjacent supported patches cannot be
    // incorrectly admitted by either patch independently.
    Quad right={{100,0,200,0,200,100,100,100},7};track={{90,20,110,20,110,70,90,70},7};
    CHECK(!Contains(patch,track)&&!Contains(right,track));
}

TEST(shore_overlay_requires_same_live_capture_view_target_and_size)
{
    using namespace WaterShoreOverlay;
    View a;a.colour=1;a.depth=2;a.capture=3;a.width=1280;a.height=720;
    a.viewport[2]=1280;a.viewport[3]=720;
    for(unsigned i=0;i<4;++i)a.worldFromClip[i*5]=1;
    CHECK(SameView(a,a));
    View b=a;b.capture=4;CHECK(!SameView(a,b)); // same textures recaptured after clear/frame
    b=a;b.capture=0;CHECK(!SameView(a,b));
    b=a;b.colour=3;CHECK(!SameView(a,b)); // intermediate colour target
    b=a;b.depth=3;CHECK(!SameView(a,b));
    b=a;b.width=1920;CHECK(!SameView(a,b));
    b=a;b.viewport[0]=.5f;CHECK(!SameView(a,b));
    b=a;b.worldFromClip[12]=1;CHECK(!SameView(a,b)); // changed camera
    b=a;b.worldFromClip[0]=std::numeric_limits<float>::quiet_NaN();CHECK(!SameView(b,b));
}

TEST(shore_overlay_removes_valid_dry_strand_but_not_missing_depth_or_objects)
{
    using WaterShoreOverlay::Replacement;
    CHECK(Replacement(1,.25f,0,1,0)==1); // wet terrain
    CHECK(Replacement(1,.75f,0,1,0)==1); // dry terrain: no wetness test on removal
    CHECK(Replacement(1,0,0,1,0)==1); // a real near-plane hit is valid
    CHECK(Replacement(1,.75f,0,.25f,0)==1);
    CHECK(Replacement(1,.75f,0,1,1)==.5f);
    CHECK(Replacement(1,.75f,0,1,2)==0); // object well above terrain
    CHECK(Replacement(0,.25f,0,1,0)==0);
    CHECK(Replacement(.5f,.25f,0,1,0)==0);
    for(float depth:{-1.f,.999999f,1.f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()})
        CHECK(Replacement(1,depth,0,1,0)==0);
    // Replacement ownership comes from the accepted modern patch, independently
    // of narrow fresh-source support or analytic/FFT wave weighting.
    for(float offshore:{0.f,.0001f,.5f,1.f})for(float support:{0.f,.25f,1.f})
        CHECK(Replacement(1,.25f,offshore,support,0)==1);
    CHECK(Replacement(1,.25f,NAN,1,0)==0);
    CHECK(Replacement(1,.25f,0,NAN,0)==0);
    // This decision has no wave-rate input: a supported trough must not keep an old permanent rim.
}
