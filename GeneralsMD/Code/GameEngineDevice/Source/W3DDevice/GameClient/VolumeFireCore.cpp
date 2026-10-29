// The device-free half of the -volsmoke fires; see VolumeFireCore.h.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#include "W3DDevice/GameClient/VolumeFireCore.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace VolumeFire {

namespace {

bool sameName(const char *a, const char *b)
{
	for (; *a && *b; ++a, ++b)
		if (std::tolower((unsigned char)*a) != std::tolower((unsigned char)*b)) return false;
	return *a == *b;
}

// The fires that burn on a building or a wreck for as long as it stays damaged or aflame: the art's
// fire bones (DAMAGED, REALLYDAMAGED), the body's own (GameData.ini AutoFire*, AutoAflame*), and the
// carrier deck's.  Flares, sparks, embers, explosion flames, aircraft and infantry fires are not
// here: they stay the game's billboards.
const char *const BuildingFires[] = {
	"SmolderingFire", "SmolderingFlameCore", "SmolderingFireLarge", "SmolderingFireHuge", "SmolderingFireTower",
	"SmolderingFlameCoreLarge", "SmolderingFlameCoreHuge", "FireFactionSmall", "FireFactionMedium", "FireFactionLarge",
	"FireBuildingSmall", "AutoFireSmall01", "AutoFireMedium01", "AutoFireLarge01", "FireSmallContinuous",
	"airCarrierDeckFire", "airCarrierDeckFireBig",
};

float gaussian(float x, float mean, float below, float above)
{
	const float t = (x - mean) / (x < mean ? below : above);
	return std::exp(-0.5f * t * t);
}

unsigned hashOf(unsigned v)
{
	v *= 2654435761u;
	v ^= v >> 15;
	v *= 0x5bd1e995u;
	return v ^ (v >> 13);
}

} // namespace

FireClass classOf(const char *templateName)
{
	if (!templateName) return NotFire;
	for (const char *name : BuildingFires)
		if (sameName(name, templateName)) return Building;
	return NotFire;
}

float flameHeight(float kW, float diameterUnits)
{
	const float metres = 0.235f * std::pow((std::max)(kW, 0.0f), 0.4f) - 1.02f * diameterUnits * MetresPerUnit;
	return (std::max)(metres, 0.0f) / MetresPerUnit;
}

float heatRelease(float flameHeightUnits, float diameterUnits)
{
	const float metres = ((std::max)(flameHeightUnits, 0.0f) + 1.02f * (std::max)(diameterUnits, 0.0f)) * MetresPerUnit;
	return std::pow(metres / 0.235f, 2.5f);
}

float puffingHz(float diameterUnits)
{
	return 1.5f / std::sqrt((std::max)(diameterUnits * MetresPerUnit, 0.05f));
}

namespace {

// The CIE XYZ of a black body's visible light, in Planck's units (no constants: only ratios are used).
void planckXYZ(float kelvin, double &X, double &Y, double &Z)
{
	const double c2 = 1.4388e7;	// nm K
	X = Y = Z = 0;
	for (int nm = 380; nm <= 780; nm += 5) {
		const float l = (float)nm;
		const double planck = 1.0 / (std::pow((double)l, 5.0) * (std::exp(c2 / (l * (double)kelvin)) - 1.0));
		X += planck * (1.056f * gaussian(l, 599.8f, 37.9f, 31.0f) + 0.362f * gaussian(l, 442.0f, 16.0f, 26.7f)
			- 0.065f * gaussian(l, 501.1f, 20.4f, 26.2f));
		Y += planck * (0.821f * gaussian(l, 568.8f, 46.9f, 40.5f) + 0.286f * gaussian(l, 530.9f, 16.3f, 31.1f));
		Z += planck * (1.217f * gaussian(l, 437.0f, 11.8f, 36.0f) + 0.681f * gaussian(l, 459.0f, 26.0f, 13.8f));
	}
}

} // namespace

void blackbody(float kelvin, float rgb[3])
{
	double X, Y, Z;
	planckXYZ(kelvin, X, Y, Z);
	double r = 3.2406 * X - 1.5372 * Y - 0.4986 * Z;
	double g = -0.9689 * X + 1.8758 * Y + 0.0415 * Z;
	double b = 0.0557 * X - 0.2040 * Y + 1.0570 * Z;
	r = (std::max)(r, 0.0); g = (std::max)(g, 0.0); b = (std::max)(b, 0.0);
	const double top = (std::max)((std::max)(r, g), (std::max)(b, 1e-30));
	rgb[0] = (float)(r / top);
	rgb[1] = (float)(g / top);
	rgb[2] = (float)(b / top);
}

float blackbodyLuminance(float kelvin)
{
	double X, Y, Z, X0, Y0, Z0;
	planckXYZ(kelvin, X, Y, Z);
	planckXYZ(1500.0f, X0, Y0, Z0);
	return (float)(Y / Y0);
}

void blackbodyTable(float *rgb, unsigned count, float lowKelvin, float highKelvin)
{
	for (unsigned i = 0; i < count; ++i)
		blackbody(lowKelvin + (highKelvin - lowKelvin) * i / (float)(count > 1 ? count - 1 : 1), rgb + i * 3);
}

float fuelPerSecond(float kW)
{
	return (std::max)(kW, 0.0f) * HeatPerKW / HeatPerFuel;
}

float emitterRate(float kW, float radiusCells, float cellUnits)
{
	// the (1 - r^2)^2 falloff over a unit sphere holds 32 pi / 105 of its middle's value
	const float r = (std::max)(radiusCells, 0.25f), cell = (std::max)(cellUnits, 0.01f);
	return fuelPerSecond(kW) / (0.95744f * r * r * r * cell * cell * cell);
}

bool pushOut(float &x, float &y, float &z, const PartBox *boxes, unsigned count, float margin)
{
	bool moved = false;
	// out of one box can be into the next: a few rounds
	for (unsigned round = 0; round < 4; ++round) {
		const PartBox *in = nullptr;
		float along = 0, across = 0;
		for (unsigned i = 0; i < count && !in; ++i) {
			const PartBox &b = boxes[i];
			const float dx = x - b.x, dy = y - b.y;
			along = dx * b.c + dy * b.s;
			across = -dx * b.s + dy * b.c;
			if (std::fabs(along) <= b.halfAlong && std::fabs(across) <= b.halfAcross && z >= b.bottom && z <= b.top) in = &b;
		}
		if (!in) break;
		moved = true;
		const float toTop = in->top - z, toEnd = in->halfAlong - std::fabs(along), toSide = in->halfAcross - std::fabs(across);
		if (toTop <= toEnd && toTop <= toSide) {
			z = in->top + margin;
			continue;
		}
		if (toEnd <= toSide) along = (along < 0 ? -1.0f : 1.0f) * (in->halfAlong + margin);
		else across = (across < 0 ? -1.0f : 1.0f) * (in->halfAcross + margin);
		x = in->x + along * in->c - across * in->s;
		y = in->y + along * in->s + across * in->c;
	}
	return moved;
}

float puff(const Emitter &e, float seconds)
{
	return 1.0f + 0.35f * std::sin(6.2831853f * e.hz * seconds + e.phase);
}

int Fires::siteOf(unsigned id) const
{
	auto found = systems.find(id);
	return found == systems.end() ? -1 : found->second.site;
}

std::vector<unsigned> Fires::systemIds() const
{
	std::vector<unsigned> ids;
	ids.reserve(systems.size());
	for (const auto &entry : systems) ids.push_back(entry.first);
	return ids;
}

int Fires::sample(const Sample &s, int now, bool allowNew)
{
	auto known = systems.find(s.id);
	int index = known != systems.end() ? known->second.site : -1;
	if (index < 0) {
		// a building's or a wreck's fires are one site; loose ones join loose ones nearby
		for (size_t i = 0; i < sites.size() && index < 0; ++i) {
			const Site &site = sites[i];
			if (!site.live || site.object != s.object) continue;
			if (s.object != 0
					|| (s.x > site.low[0] - JoinUnits && s.x < site.high[0] + JoinUnits && s.y > site.low[1] - JoinUnits
						&& s.y < site.high[1] + JoinUnits))
				index = (int)i;
		}
		if (index < 0) {
			if (!allowNew) return -1;
			for (size_t i = 0; i < sites.size() && index < 0; ++i)
				if (!sites[i].live) index = (int)i;
			if (index < 0) {
				index = (int)sites.size();
				sites.emplace_back();
			}
			Site &fresh = sites[index];
			fresh = Site();
			fresh.live = true;
			fresh.object = s.object;
			fresh.born = now;
			for (int a = 0; a < 3; ++a) fresh.low[a] = fresh.high[a] = (&s.x)[a];
		}
		System system = { index, s, s.height, s.width, now };
		known = systems.emplace(s.id, system).first;
		sites[index].fuelEnd = -1;
	}
	System &system = known->second;
	system.last = s;
	system.frame = now;
	// The flame its particles draw comes and goes with each burst: held at its peak, let down slowly.
	system.height = (std::max)(s.height, system.height * 0.98f);
	system.width = (std::max)(s.width, system.width * 0.98f);
	Site &site = sites[index];
	for (int a = 0; a < 3; ++a) {
		site.low[a] = (std::min)(site.low[a], (&s.x)[a]);
		site.high[a] = (std::max)(site.high[a], (&s.x)[a]);
	}
	site.flame = (std::max)(site.flame, system.height);
	return index;
}

void Fires::forget(unsigned id, int now)
{
	auto known = systems.find(id);
	if (known == systems.end()) return;
	const int index = known->second.site;
	systems.erase(known);
	for (const auto &entry : systems)
		if (entry.second.site == index) return;
	if (index >= 0 && index < (int)sites.size() && sites[index].live) sites[index].fuelEnd = now;
}

unsigned Fires::prune(int now, int framesPerSecond)
{
	unsigned freed = 0;
	for (Site &site : sites)
		if (site.live && site.fuelEnd >= 0 && now - site.fuelEnd > (int)(BurnoutSeconds * framesPerSecond)) {
			site.live = false;
			++freed;
		}
	return freed;
}

void Fires::emitters(int index, int now, int framesPerSecond, std::vector<Emitter> &out) const
{
	out.clear();
	if (index < 0 || index >= (int)sites.size() || !sites[index].live || sites[index].fuelEnd >= 0) return;
	const float age = (float)(now - sites[index].born) / (float)(framesPerSecond > 0 ? framesPerSecond : 1);
	const float grown = (std::min)(1.0f, (age / RiseSeconds) * (age / RiseSeconds));
	struct Keyed { Emitter e; unsigned id; };
	std::vector<Keyed> all;
	const int stale = (int)(StaleSeconds * framesPerSecond);
	for (const auto &entry : systems) {
		const System &system = entry.second;
		if (system.site != index || now - system.frame > stale) continue;
		const float width = (std::max)(system.width, 1.0f);
		Emitter e;
		e.x = system.last.x;
		e.y = system.last.y;
		e.z = system.last.z;
		e.radius = (std::max)(width * 0.5f, 1.0f);
		e.kW = heatRelease(system.height, width) * grown;
		e.hz = puffingHz(width);
		e.phase = (hashOf(entry.first) & 1023) * (6.2831853f / 1024.0f);
		e.id = entry.first;
		all.push_back({ e, entry.first });
	}
	// the biggest first, ties by id, so the same frame picks the same emitters whatever the map's order
	std::sort(all.begin(), all.end(), [](const Keyed &a, const Keyed &b) { return a.e.kW != b.e.kW ? a.e.kW > b.e.kW : a.id < b.id; });
	for (size_t i = 0; i < all.size() && i < MaxEmitters; ++i) out.push_back(all[i].e);
}

bool Fires::fresh(int index, int now, int framesPerSecond) const
{
	const int stale = (int)(StaleSeconds * framesPerSecond);
	for (const auto &entry : systems)
		if (entry.second.site == index && now - entry.second.frame <= stale) return true;
	return false;
}

float Fires::heat(int index, int now, int framesPerSecond) const
{
	std::vector<Emitter> list;
	emitters(index, now, framesPerSecond, list);
	float sum = 0;
	for (const Emitter &e : list) sum += e.kW;
	return sum;
}

} // namespace VolumeFire
