/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
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
// Modified 2026 by Olcay Seygan for Zero Hour Reforged; see the git history.

// FILE: AudioMixPolicy.h
// The arithmetic behind MilesAudioManager's battle mix, kept free of the engine so a test can hold
// it: which playing sound gives way to a new one, how much louder a shot gets for the identical
// shots folded into it, how deep a hill or a building stands in the line between the microphone and
// a sound, and how fast the muffle that follows from it moves.  Nothing here reads or writes logic.

#pragma once

#include <math.h>

namespace AudioMixPolicy {

/// How much a sound matters when the voices run out: its priority first (EA's AP_LOWEST .. AP_CRITICAL,
/// 0 to 4), then how loud it is at the microphone (0 to 1), so among equals the quietest, which is
/// mostly the farthest, gives way first.
inline float importance(int priority, float loudness)
{
	if (loudness < 0.0f)
		loudness = 0.0f;
	if (loudness > 1.0f)
		loudness = 1.0f;
	return (float)priority + loudness;
}

/// The gain a shot plays at for the identical shots folded into it: a step for each, never past the
/// ceiling.  Many guns firing together sound like more than one gun, but not like all of them.
inline float coalescedGain(int merged, float step, float ceiling)
{
	const float gain = 1.0f + step * (float)merged;
	return gain < ceiling ? gain : ceiling;
}

/// The deepest that ground or a roof stands above the straight line from the microphone (l) to the
/// sound (s), in game units, or 0 for a clear line.  heightAt(x, y) is the highest blocker at a point.
/// The line is sampled every step units, leaving out margin units at either end, where the ground the
/// sound stands on and the slope under the microphone would count as hills.
template <class HeightAt>
float rayObstruction(float lx, float ly, float lz, float sx, float sy, float sz, float step, float margin,
	int maxSamples, HeightAt heightAt)
{
	const float dx = sx - lx;
	const float dy = sy - ly;
	const float length = sqrtf(dx * dx + dy * dy);
	if (length <= 2.0f * margin || step <= 0.0f)
		return 0.0f;
	int samples = (int)((length - 2.0f * margin) / step) + 1;
	if (samples > maxSamples)
		samples = maxSamples;
	float deepest = 0.0f;
	for (int i = 0; i < samples; ++i) {
		const float along = margin + (length - 2.0f * margin) * ((float)i + 0.5f) / (float)samples;
		const float t = along / length;
		const float lineZ = lz + (sz - lz) * t;
		const float depth = heightAt(lx + dx * t, ly + dy * t) - lineZ;
		if (depth > deepest)
			deepest = depth;
	}
	return deepest;
}

/// The share of the full muffle a depth earns: none at 0, all of it from fullDepth units of hill on.
inline float occlusionAmount(float depth, float fullDepth)
{
	if (depth <= 0.0f || fullDepth <= 0.0f)
		return depth > 0.0f ? 1.0f : 0.0f;
	const float amount = depth / fullDepth;
	return amount < 1.0f ? amount : 1.0f;
}

/// One step of a smoothed value towards its target, over elapsedMs, closing 63% of the gap every
/// timeMs.  No time at all leaves it where it is; a zero time jumps.
inline float approach(float current, float target, float elapsedMs, float timeMs)
{
	if (timeMs <= 0.0f)
		return target;
	if (elapsedMs <= 0.0f)
		return current;
	const float keep = expf(-elapsedMs / timeMs);
	return target + (current - target) * keep;
}

}	// namespace AudioMixPolicy
