// The device-free half of the -volsmoke fires (VolumeFireCore): the template table, Heskestad's
// flame height both ways, the black body's colour, the fuel an emitter lays, the push out of a
// building's boxes, and the sites' life following their systems.
#include "test_harness.h"
#include "W3DDevice/GameClient/VolumeFireCore.h"
#include "W3DDevice/GameClient/VolumeShockCore.h"

#include <cmath>
#include <vector>

using namespace VolumeFire;

TEST(volume_fire_takes_only_building_and_wreck_fires)
{
	for (const char *name : { "SmolderingFire", "SmolderingFlameCore", "FireFactionLarge", "FireBuildingSmall",
			"smolderingfire", "airCarrierDeckFire" })
		CHECK_EQ((int)classOf(name), (int)Building);
	// sparks, flares, embers, infantry, aircraft, fields and explosion flames stay billboards (stage 1)
	for (const char *name : { static_cast<const char *>(nullptr), "", "FireTreeSparks", "FireInfantrySmall",
			"FireInfantrySmallSub3", "JetFireLarge", "InfernoCannonFlare", "FireStormMain", "TankFire",
			"SmolderingSmoke", "SmolderingFireX", "GLAPowerPlantFlame" })
		CHECK_EQ((int)classOf(name), (int)NotFire);
}

TEST(volume_fire_heskestad_round_trips_and_grows_with_heat)
{
	for (float height : { 8.0f, 16.0f, 30.0f, 60.0f })
		for (float width : { 2.0f, 4.0f, 9.0f })
			CHECK_NEAR(flameHeight(heatRelease(height, width), width), height, 0.01f * height);
	// a 4 m flame over a 1 m opening is a room fire venting: about 2 MW
	const float kW = heatRelease(4.0f / MetresPerUnit, 1.0f / MetresPerUnit);
	CHECK(kW > 1800.0f && kW < 2400.0f);
	CHECK(heatRelease(20.0f, 4.0f) > heatRelease(10.0f, 4.0f));
	CHECK_EQ(flameHeight(0.0f, 4.0f), 0.0f);
	// a metre-wide pool puffs about one and a half times a second, a wider one slower
	CHECK_NEAR(puffingHz(1.0f / MetresPerUnit), 1.5f, 0.01f);
	CHECK(puffingHz(20.0f) < puffingHz(5.0f));
}

TEST(volume_fire_black_body_goes_from_red_to_white)
{
	float table[8 * 3];
	blackbodyTable(table, 8, 800.0f, 3000.0f);
	for (unsigned i = 0; i < 8; ++i) {
		const float *rgb = table + i * 3;
		CHECK_NEAR(rgb[0], 1.0f, 1e-4f);		// red is the brightest channel at flame temperatures
		CHECK(rgb[1] <= rgb[0] && rgb[2] <= rgb[1]);
		if (i) {
			CHECK(rgb[1] > table[(i - 1) * 3 + 1]);	// hotter: more green and blue, towards white
			CHECK(rgb[2] >= table[(i - 1) * 3 + 2]);
		}
	}
	// linear sRGB, not gamma: 3000 K is (255, 180, 107) on screen, about (1, 0.46, 0.15) linear
	CHECK(table[1] < 0.1f);			// 800 K is a dull red
	CHECK(table[7 * 3 + 1] > 0.35f && table[7 * 3 + 1] < 0.6f);
	CHECK(table[7 * 3 + 2] > 0.08f);
	// visible light climbs far faster than T^4 at flame temperatures
	CHECK_NEAR(blackbodyLuminance(1500.0f), 1.0f, 1e-5f);
	CHECK(blackbodyLuminance(800.0f) < 0.05f);
	CHECK(blackbodyLuminance(2500.0f) > 5.0f);
	CHECK(blackbodyLuminance(2500.0f) / blackbodyLuminance(2000.0f) > std::pow(2500.0f / 2000.0f, 4.0f));
	float hot[3];
	blackbody(6500.0f, hot);
	CHECK(hot[2] > 0.8f);			// near the daylight white point every channel is close
}

TEST(volume_fire_emitter_lays_the_fuel_its_heat_asks_for)
{
	// sum the (1 - r^2)^2 falloff over the cells of a sphere: the sphere lays fuelPerSecond(kW)
	const float kW = 2000.0f, radius = 3.0f, cell = 2.5f;
	const float rate = emitterRate(kW, radius, cell);
	double total = 0;
	for (int z = -4; z <= 4; ++z)
		for (int y = -4; y <= 4; ++y)
			for (int x = -4; x <= 4; ++x) {
				const float r2 = (x * x + y * y + z * z) / (radius * radius);
				if (r2 < 1) total += rate * (1 - r2) * (1 - r2);
			}
	total *= cell * cell * cell;
	CHECK_NEAR((float)total, fuelPerSecond(kW), 0.12f * fuelPerSecond(kW));
	CHECK(emitterRate(kW, radius, cell * 2) < rate);
	CHECK_EQ(fuelPerSecond(-5.0f), 0.0f);
}

TEST(volume_fire_bone_inside_a_box_goes_out_through_the_nearest_face)
{
	// a 40 by 20 box turned a quarter, 0 to 30 up
	const PartBox boxes[2] = { { 100, 100, 0, 1, 20, 10, 0, 30 }, { 100, 100, 1, 0, 3, 3, 30, 50 } };
	float x = 103, y = 100, z = 10;	// near the box's side (across is -3 of 10)
	CHECK(pushOut(x, y, z, boxes, 1, 2.0f));
	CHECK_NEAR(x, 112.0f, 1e-3f);
	CHECK_NEAR(y, 100.0f, 1e-3f);
	CHECK_NEAR(z, 10.0f, 1e-3f);
	x = 100; y = 110; z = 29;		// just under the roof: onto it
	CHECK(pushOut(x, y, z, boxes, 1, 2.0f));
	CHECK_NEAR(z, 32.0f, 1e-3f);
	// onto the roof, into the chimney on it, and out of the chimney's side
	x = 101; y = 100; z = 29;
	CHECK(pushOut(x, y, z, boxes, 2, 1.0f));
	for (const PartBox &b : boxes) {
		const float dx = x - b.x, dy = y - b.y, along = dx * b.c + dy * b.s, across = -dx * b.s + dy * b.c;
		CHECK(!(std::fabs(along) <= b.halfAlong && std::fabs(across) <= b.halfAcross && z >= b.bottom && z <= b.top));
	}
	x = 200; y = 200; z = 5;
	CHECK(!pushOut(x, y, z, boxes, 2, 1.0f));
	CHECK_EQ(x, 200.0f);
}

TEST(volume_fire_sites_follow_their_systems)
{
	const int fps = 60;
	Fires fires;
	// two bones of one building, one loose fire nearby, one far off
	const int a = fires.sample({ 1, 77, 100, 100, 10, 16, 4 }, 0);
	CHECK_EQ(fires.sample({ 2, 77, 130, 100, 25, 20, 6 }, 0), a);
	const int loose = fires.sample({ 3, 0, 150, 100, 0, 12, 4 }, 0);
	CHECK(loose != a);
	CHECK_EQ(fires.sample({ 4, 0, 170, 110, 0, 12, 4 }, 0), loose);
	const int far = fires.sample({ 5, 0, 900, 900, 0, 12, 4 }, 0);
	CHECK(far != loose && far != a);
	CHECK_EQ(fires.sample({ 6, 0, 2000, 2000, 0, 12, 4 }, 0, false), -1);
	CHECK(!fires.knows(6));

	// the heat grows in as t squared, full after RiseSeconds
	std::vector<Emitter> list;
	fires.emitters(a, 0, fps, list);
	CHECK_EQ((int)list.size(), 2);
	CHECK_EQ(list[0].kW, 0.0f);
	const int full = (int)(RiseSeconds * fps);
	fires.sample({ 1, 77, 100, 100, 10, 16, 4 }, full);	// still drawn, so still fed
	fires.sample({ 2, 77, 130, 100, 25, 20, 6 }, full);
	CHECK_NEAR(fires.heat(a, full / 2, fps), 0.25f * fires.heat(a, full, fps), 1.0f);
	fires.emitters(a, full, fps, list);
	CHECK(list[0].kW >= list[1].kW);	// the biggest first
	CHECK_NEAR(list[0].x, 130.0f, 1e-3f);
	CHECK_NEAR(list[0].kW, heatRelease(20, 6), 1.0f);

	// the fire ends with the game's systems: no fuel once the last one goes, freed after the burnout
	fires.forget(1, 100);
	CHECK(fires.sites[a].fuelEnd < 0);
	fires.forget(2, 120);
	CHECK_EQ(fires.sites[a].fuelEnd, 120);
	fires.emitters(a, 121, fps, list);
	CHECK(list.empty());
	CHECK_EQ(fires.prune(120 + (int)(BurnoutSeconds * fps), fps), 0u);
	CHECK_EQ(fires.prune(121 + (int)(BurnoutSeconds * fps), fps), 1u);
	CHECK(!fires.sites[a].live);
	// a fire that comes back on the same building starts a site again, in the freed slot
	CHECK_EQ(fires.sample({ 7, 77, 100, 100, 10, 16, 4 }, 500), a);
	CHECK_EQ(fires.sites[a].born, 500);
}

TEST(volume_fire_caps_a_site_at_its_emitters_and_keeps_its_peak_flame)
{
	Fires fires;
	int site = -1;
	for (unsigned i = 0; i < MaxEmitters + 6; ++i) site = fires.sample({ 10 + i, 5, 100.0f + i, 100, 0, 10.0f + i, 4 }, 0);
	std::vector<Emitter> list;
	fires.emitters(site, 60, 60, list);
	CHECK_EQ((unsigned)list.size(), MaxEmitters);
	// a system the game stopped drawing (stealth) feeds nothing a second later, and its site goes stale
	CHECK(fires.fresh(site, 60, 60));
	fires.emitters(site, 61, 60, list);
	CHECK(list.empty());
	CHECK(!fires.fresh(site, 61, 60));
	// a burst's particles shrink between puffs: the flame is held near its peak
	Fires one;
	const int alone = one.sample({ 1, 9, 100, 100, 0, 20, 4 }, 0);
	one.sample({ 1, 9, 100, 100, 0, 2, 4 }, 601);
	CHECK(one.heat(alone, 602, 60) > heatRelease(19, 4));
}

// The blast fronts' half (VolumeShockCore.h): a point a front passes is moved by the displacement
// the front carries, the shell cut at three widths loses next to nothing, a passage mixes away its
// share, and the templates that start a front of their own.
TEST(volume_shock_passage_moves_a_point_by_the_fronts_displacement)
{
	using namespace VolumeShock;
	// a point well past the fireball, met before the front starts to die out (age under 0.6 T), where
	// the particle speed is under its cap: the first nuke's cap seen from the second's ground zero, a
	// Scud's earlier smoke, a bomb's neighbour
	const struct { ShockClass c; float r; } cases[] = { { Nuclear, 390.0f }, { Scud, 100.0f }, { shell(2.0f), 120.0f } };
	for (const auto &one : cases) {
		const float meet = one.r / one.c.speed;
		CHECK(meet < 0.6f * one.c.seconds);
		CHECK(peakSpeed(one.c, meet) < MaxPeakSpeed);
		double moved = 0;
		const float dt = 1.0f / 600.0f;
		for (float age = 0; age < one.c.seconds; age += dt) moved += shiftAt(one.c, age, one.r, dt);
		// D falls as the front runs through the point, so the sum sits within a few per cent of D there
		CHECK_NEAR((float)moved, displacement(one.c, meet), 0.05f * displacement(one.c, meet));
	}
	// the second nuke moves the first's cap about half its 160-unit radius
	CHECK(displacement(Nuclear, 390.0f / Nuclear.speed) > 70.0f);
	CHECK(displacement(Nuclear, 390.0f / Nuclear.speed) < 100.0f);
	// past three widths the shell is cut: erfc(3), about two parts in a hundred thousand
	CHECK_EQ(profile(100.0f + 3.01f * 25.0f, 100.0f, 25.0f), 0.0f);
	CHECK(std::erfc(3.0) < 1e-4);
	// a front is gone at its end
	CHECK_EQ(displacement(Scud, Scud.seconds), 0.0f);
}

TEST(volume_shock_passage_mixes_away_its_share)
{
	using namespace VolumeShock;
	for (const ShockClass &c : { Nuclear, Scud, shell(1.0f) }) {
		const float age = 0.3f * c.seconds;
		const float width = frontWidth(frontRadius(c, age));
		// the rate over the shell's passage time, w sqrt(pi) / speed, keeps 1 - f
		const float kept = std::exp(-mixRate(c, age) * width * SqrtPi / c.speed);
		CHECK_NEAR(kept, 1.0f - dispersed(c, age), 1e-4f);
		CHECK(dispersed(c, age) <= c.disperse);
	}
	// a Scud next to an earlier puff thins it by about a tenth, a nuke over the other's cap by half
	CHECK_NEAR(dispersed(Scud, 100.0f / Scud.speed), 0.13f, 0.03f);
	CHECK_NEAR(dispersed(Nuclear, 390.0f / Nuclear.speed), 0.55f, 0.06f);
	// a cap lets go once moved CapRelease units, and holds while barely touched
	CHECK_EQ(capHold(0.0f), 1.0f);
	CHECK_EQ(capHold(CapRelease), 0.0f);
	CHECK(capHold(10.0f) > 0.8f);
}

TEST(volume_shock_templates_that_start_a_front)
{
	using namespace VolumeShock;
	// the Scud's warheads are additive and start no volume, so the name is what starts their front
	CHECK(frontOf("ScudMissleExplosionArms") == &Scud);
	CHECK(frontOf("ScudMissleLauncherExplosionArms") == &Scud);
	CHECK(frontOf("NukeCannonBlastWave") == &NukeCannon);
	CHECK(frontOf("NukeFlare") == &Nuclear);
	CHECK(frontOf("NukeGLAFlare") == &Nuclear);
	for (const char *name : { "NukeShockwave", "ArtilleryBarrageShockwave", "MammothTankExplosionShockwave",
			"StructureToppleShockwave", "CarpetBombWave", "MOABDustWave" })
		CHECK(frontOf(name) == &StockShock);
	for (const char *name : { static_cast<const char *>(nullptr), "", "ScudMissleLenzFlare", "TankStruckSparks",
			"WaveSplashRight01", "DamWavesOutward", "NukeMushroomCap" })
		CHECK(frontOf(name) == nullptr);
}
