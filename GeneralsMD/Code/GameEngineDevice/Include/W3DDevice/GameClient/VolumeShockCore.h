// The device-free half of the -volsmoke blast fronts (DX11VolumeSmoke.cpp runs them): which
// particle systems start a front, how far a front moves the smoke it passes, how much of it it
// mixes away, and how hard a nuclear cap holds its shape against another blast.  Client side and
// pure arithmetic: nothing here reads the logic or draws a random number.  Distributed under the
// repository's GPL-3.0 terms; see LICENSE.md.
//
// A front is a sphere from the blast, its radius growing at `speed`, a Gaussian shell w wide.  It
// carries no strength but the displacement D it gives a point it passes, falling as a blast's
// overpressure does past its fireball (Hopkinson's scaling, 1/R far out).  The smoke is moved by
// the shell's particle speed u, the one whose passage over a point adds up to D:
//   u * integral of exp(-((r - speed t) / w)^2) dt = u * w * sqrt(pi) / speed = D.
// The march's projection never sees it: the advect pass moves the smoke in its backtrace instead of
// the velocity (the design's option B), so the pressure solve has nothing of it to take away.
#pragma once

#include <cmath>
#include <cstring>

namespace VolumeShock {

const float Pi = 3.14159265f;
const float SqrtPi = 1.77245385f;
const float MaxPeakSpeed = 600.0f;		// units/s: under the blast itself the shift is held in cells by the shader too
const float DisperseScale = 150.0f;		// a passage that moves smoke this far mixes away up to its class's share
const float StirShare = 0.15f;			// the eddies a passage leaves, as a share of its particle speed
const float CapRelease = 60.0f;			// units another blast must move a nuclear cap's middle to let it go

// speed units/s, push units at the blast, r0 units (where D has halved), seconds it runs, the most of
// the smoke one passage mixes away
struct ShockClass { float speed, push, r0, seconds, disperse; };

// First numbers, tuned by eye on volshock; speeds and lifetimes are the ones addShock had.
const ShockClass Nuclear = { 420.0f, 300.0f, 150.0f, 2.5f, 0.6f };
const ShockClass NukeCannon = { 250.0f, 60.0f, 60.0f, 1.5f, 0.4f };
const ShockClass Scud = { 260.0f, 60.0f, 50.0f, 1.2f, 0.3f };
const ShockClass StructureFall = { 140.0f, 30.0f, 40.0f, 1.0f, 0.2f };
const ShockClass StructureStage = { 140.0f, 18.0f, 40.0f, 1.0f, 0.2f };
const ShockClass StockShock = { 200.0f, 20.0f, 30.0f, 1.0f, 0.2f };
const ShockClass AirExplosion = { 110.0f, 12.0f, 20.0f, 0.8f, 0.1f };

// A shell's front by its size against the smallest (VolumeSmoke's `scale`); a bomb's from 2 up.
inline ShockClass shell(float scale)
{
	if (scale >= 2.0f) return { 160.0f * scale, 25.0f * scale, 30.0f * scale, 1.0f, 0.25f };
	return { 160.0f * scale, 12.0f * scale, 20.0f, 1.0f, 0.15f };
}

// The front a template's systems start on first sight, for the systems that start no volume of
// their own (the Scud's warheads are all additive; the stock shock rings), or null.  A volume's own
// front is started with its volume, and a merge with this one keeps the class that pushes harder.
inline const ShockClass *frontOf(const char *name)
{
	if (!name) return nullptr;
	// a nuke's flare goes off at its impact, also when it joins a nuke before it and starts no volume
	if (!std::strcmp(name, "NukeFlare") || !std::strcmp(name, "NukeGLAFlare")) return &Nuclear;
	if (!std::strcmp(name, "ScudMissleExplosionArms") || !std::strcmp(name, "ScudMissleLauncherExplosionArms")) return &Scud;
	if (!std::strcmp(name, "NukeCannonBlastWave")) return &NukeCannon;
	if (std::strstr(name, "Shockwave") || std::strstr(name, "BlastWave") || !std::strcmp(name, "CarpetBombWave")
			|| !std::strcmp(name, "MOABDustWave"))
		return &StockShock;
	return nullptr;
}

inline float frontRadius(const ShockClass &c, float age) { return c.speed * age; }
inline float frontWidth(float radius) { return 10.0f + 0.15f * radius; }

inline float smooth(float x, float low, float high)
{
	const float t = x <= low ? 0.0f : x >= high ? 1.0f : (x - low) / (high - low);
	return t * t * (3.0f - 2.0f * t);
}

// What the front moves a point it passes now, in units: the class's push falling past r0, and the
// front dying out over the last 40% of its time.
inline float displacement(const ShockClass &c, float age)
{
	const float radius = frontRadius(c, age);
	return c.push * c.r0 / (c.r0 + radius) * (1.0f - smooth(age, 0.6f * c.seconds, c.seconds));
}

// The shell's particle speed now, units/s.
inline float peakSpeed(const ShockClass &c, float age)
{
	const float u = displacement(c, age) * c.speed / (frontWidth(frontRadius(c, age)) * SqrtPi);
	return u < MaxPeakSpeed ? u : MaxPeakSpeed;
}

// The share of the smoke a passage mixes away now, and the rate a second over the shell that gives it.
inline float dispersed(const ShockClass &c, float age)
{
	const float f = displacement(c, age) / DisperseScale;
	return f < c.disperse ? f : c.disperse;
}
inline float mixRate(const ShockClass &c, float age)
{
	return -std::log(1.0f - dispersed(c, age)) * c.speed / (frontWidth(frontRadius(c, age)) * SqrtPi);
}

// The eddy kick behind the front, units/s^2: over one passage it adds up to StirShare of u.
inline float stirAccel(const ShockClass &c, float age)
{
	return StirShare * peakSpeed(c, age) * c.speed / (frontWidth(frontRadius(c, age)) * SqrtPi);
}

// The shell's profile at distance r from the blast: 1 on the front, 0 past three widths.
inline float profile(float r, float radius, float width)
{
	const float x = (r - radius) / width;
	return x > 3.0f || x < -3.0f ? 0.0f : std::exp(-x * x);
}

// The shift this step at distance r, units along the outward radius (the CPU's copy of the shader's
// shockShift, for the air's particles: no width floor, no cell cap).
inline float shiftAt(const ShockClass &c, float age, float r, float dt)
{
	const float radius = frontRadius(c, age);
	return peakSpeed(c, age) * profile(r, radius, frontWidth(radius)) * dt;
}

// How firmly a nuclear cap holds its shape once other blasts have moved its middle this far, units:
// 1 untouched, 0 let go for good.
inline float capHold(float pushed)
{
	const float hold = 1.0f - pushed / CapRelease;
	return hold < 0.0f ? 0.0f : hold > 1.0f ? 1.0f : hold;
}

}	// namespace VolumeShock
