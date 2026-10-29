// The device-free half of the -volsmoke fires (DX11VolumeSmoke.cpp draws them): which particle
// systems are fires, how they gather into sites, how much heat each releases and where its fuel
// goes, and the colour of a hot body.  Client side and pure arithmetic: nothing here reads the
// logic or draws a random number.  Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#pragma once

#include <unordered_map>
#include <vector>

namespace VolumeFire {

// A game unit is about 0.23 m (a tank is about 30 units, 7 m).
const float MetresPerUnit = 0.23f;

// What a template's systems are to the fire: not a fire (the game's own billboards), or a
// building's or a wreck's fire, the stage-1 class.
enum FireClass { NotFire, Building };
FireClass classOf(const char *templateName);

// Heskestad's mean flame height, L = 0.235 Q^(2/5) - 1.02 D (L and D in metres, Q in kW), and its
// inverse: the heat release a flame that tall on a base that wide stands for.  Taken in game units.
float flameHeight(float kW, float diameterUnits);
float heatRelease(float flameHeightUnits, float diameterUnits);

// A pool fire puffs at about 1.5 / sqrt(D) times a second, D in metres.
float puffingHz(float diameterUnits);

// The colour of a black body at this temperature (kelvin): Planck's law weighed by the CIE 1931
// matching functions (Wyman, Sloan and Shirley's fit), taken to linear sRGB, brightest channel 1.
void blackbody(float kelvin, float rgb[3]);
// How bright its visible light is against a 1500 K body's (the CIE Y ratio): about a hundredth at
// 800 K and twelve times at 2500 K, far steeper than T^4.
float blackbodyLuminance(float kelvin);
// `count` texels from lowKelvin to highKelvin, three floats each.
void blackbodyTable(float *rgb, unsigned count, float lowKelvin, float highKelvin);

// The fuel the sim lays for a flame of this heat release: heat (in the sim's heat units times
// cubic world units) per kJ, over the heat one unit of fuel density gives when it burns.
// ponytail: HeatPerKW is set by eye against the stock flames; measure a flame's steady height in
// the sim against flameHeight() if it needs to be exact.  A building's slab has cells of 3 to 6
// units, wider than most of its flames, so this is far over the heat a flame that size holds
// (0.15 by the cell's volume): at 0.15 a damaged War Factory's flames heated their cells to a
// third of the ignition and nothing showed, and at 4 they stood twice the stock flames' size.
const float HeatPerKW = 2.0f;
const float HeatPerFuel = 2.5f;
// Fuel density a second at an emitter sphere's middle, for its (1 - r^2)^2 falloff over
// radiusCells, so the sphere lays fuelPerSecond(kW) in all.
float fuelPerSecond(float kW);
float emitterRate(float kW, float radiusCells, float cellUnits);

// A mesh's box in the world (a building's part): middle, heading, half sizes, bottom and top.
struct PartBox { float x, y, c, s, halfAlong, halfAcross, bottom, top; };
// A fire bone often lies inside the box the sim makes solid, which would put its fuel out: the
// point goes out through the nearest face, side or roof, and `margin` past it.  False if it was
// in no box.
bool pushOut(float &x, float &y, float &z, const PartBox *boxes, unsigned count, float margin);

// One fire system as seen this frame: its id, the object it hangs on (0 for a loose one), its
// base (the young particles' lowest point), and the flame its live particles draw.
struct Sample { unsigned id; unsigned object; float x, y, z, height, width; };

// One fuel source of a site, for the sim: where, how wide, how much heat, and its puffing.
struct Emitter { float x, y, z, radius, kW, hz, phase; unsigned id; };

struct Site {
	bool live = false;
	unsigned object = 0;			// the building or wreck it burns on, 0 for loose fires
	int born = 0;
	int fuelEnd = -1;				// the frame its last system went, -1 while one burns
	float low[3] = {}, high[3] = {};	// its systems' bases so far
	float flame = 0;				// its tallest flame, world units
};

// Seconds to a site's full heat (t-squared growth) and a site's life after its last system goes:
// the fuel in the grid burns out and the soot drifts off.
const float RiseSeconds = 1.5f;
const float BurnoutSeconds = 2.0f;
const float StaleSeconds = 1.0f;
// Loose fires this close join a site of loose fires.
const float JoinUnits = 40.0f;
const unsigned MaxEmitters = 24;

class Fires {
public:
	// The site this system's fire belongs to, made if need be; its sample kept.  -1 when it may not
	// make one (`allowNew` false and none fits).
	int sample(const Sample &s, int now, bool allowNew = true);
	// A system that is gone: its site starts burning out when it was the last.
	void forget(unsigned id, int now);
	// Sites whose burnout is over are freed.  Returns how many were.
	unsigned prune(int now, int framesPerSecond);
	// The site's emitters at `now`, the biggest first, at most MaxEmitters, their heat grown in over
	// RiseSeconds.  Empty once its fuel has stopped.  A system not sampled for StaleSeconds is left
	// out: the game stopped drawing it (a stealthed building's fires), so the volume must too.
	void emitters(int site, int now, int framesPerSecond, std::vector<Emitter> &out) const;
	// Whether any of the site's systems was sampled in the last StaleSeconds.
	bool fresh(int site, int now, int framesPerSecond) const;
	// Its total heat release at `now`, kW.
	float heat(int site, int now, int framesPerSecond) const;
	bool knows(unsigned id) const { return systems.count(id) != 0; }
	int siteOf(unsigned id) const;
	void clear() { sites.clear(); systems.clear(); }
	std::vector<unsigned> systemIds() const;

	std::vector<Site> sites;

private:
	struct System { int site; Sample last; float height, width; int frame; };
	std::unordered_map<unsigned, System> systems;
};

// The puffing modulation of one emitter at `seconds`: 1 +- 0.35.
float puff(const Emitter &e, float seconds);

} // namespace VolumeFire
