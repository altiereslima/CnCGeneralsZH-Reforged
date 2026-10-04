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

// One deterministic sweep of D3DXVec4Transform inputs, shared by two harnesses so that their
// results can be chained:
//
//   Tests/d3dx_oracle   Microsoft's DLL bodies == d3dxportable.h built for x86_64, over this sweep
//   Tests/arch_diff     d3dxportable.h on x86_64 == d3dxportable.h on arm64, over this sweep
//                       (as a fingerprint)
//
// Together they say that on these inputs, the arm64 build computes what the DLL's scalar and
// non-Intel bodies compute.  That only holds if both use the same inputs, which is why the inputs
// live here and not in either harness.
//
// Every input is built from integers by exact operations or by assembling bit patterns: no libc
// rand() and no transcendental, so the sweep is the same sequence on every platform and compiler.

#pragma once

#ifndef D3DX_SWEEP_H
#define D3DX_SWEEP_H

#include <string.h>

#include "d3dx_golden.h"

static const unsigned int D3DX_SWEEP_COUNT = 1000000;

struct D3DXSweepState
{
	unsigned int seed;
};

// xorshift32: fully specified integer arithmetic.
static inline unsigned int d3dx_sweep_next(D3DXSweepState * state)
{
	unsigned int x = state->seed;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	state->seed = x;
	return x;
}

// A finite float with a random sign and mantissa, and an exponent between 2^-27 and 2^27.  That
// is wide enough to exercise rounding at every scale while keeping four products and three sums
// clear of overflow, so no infinity or NaN arises.
static inline float d3dx_sweep_wide(D3DXSweepState * state)
{
	const unsigned int r = d3dx_sweep_next(state);
	const unsigned int exponent = 100u + (r >> 8) % 55u;
	const unsigned int bits = (r & 0x80000000u) | (exponent << 23) | (d3dx_sweep_next(state) & 0x007fffffu);
	float value;
	memcpy(&value, &bits, sizeof(value));
	return value;
}

// Input `index` of the sweep.  Returns true when `matrix` is the Bezier basis, which is the only
// matrix GameLogic ever passes; the rest use a general matrix, to pin term order on every lane.
//   index % 4 == 0, 1   the basis, with control-point coordinates shaped like a shell's flight:
//                       start and end anywhere on a 5000-unit map, inner points along the line,
//                       all in hundredths as the game's positions usually are
//   index % 4 == 2      the basis, with (t^3, t^2, t, 1) as evaluateBezSegmentAtT builds it
//   index % 4 == 3      a general matrix and vector from d3dx_sweep_wide
static inline bool d3dx_sweep_input(D3DXSweepState * state, unsigned int index, float vector[4],
	float matrix[16])
{
#if defined(__clang__)
	// The inputs themselves must not fuse either, or the two harnesses would sweep different
	// inputs and the chain between them would break.
#pragma clang fp contract(off)
#endif
	switch (index % 4u) {
	case 0:
	case 1: {
		memcpy(matrix, D3DX_GOLDEN_BASIS, sizeof(D3DX_GOLDEN_BASIS));
		const float start = (float)(d3dx_sweep_next(state) % 500000u) * 0.01f;
		const float end = (float)(d3dx_sweep_next(state) % 500000u) * 0.01f;
		const float wobble = (float)(d3dx_sweep_next(state) % 2000u) * 0.01f;
		vector[0] = start;
		vector[1] = start + (end - start) * 0.33f + wobble;
		vector[2] = start + (end - start) * 0.66f;
		vector[3] = end;
		return true;
	}
	case 2: {
		memcpy(matrix, D3DX_GOLDEN_BASIS, sizeof(D3DX_GOLDEN_BASIS));
		const float t = (float)(d3dx_sweep_next(state) % 100001u) * 0.00001f;
		vector[0] = t * t * t;
		vector[1] = t * t;
		vector[2] = t;
		vector[3] = 1.0f;
		return true;
	}
	default:
		for (int i = 0; i < 16; ++i) {
			matrix[i] = d3dx_sweep_wide(state);
		}
		for (int i = 0; i < 4; ++i) {
			vector[i] = d3dx_sweep_wide(state);
		}
		return false;
	}
}

// FNV-1a over output bit patterns.
static inline unsigned int d3dx_sweep_mix(unsigned int hash, const float values[4])
{
	for (int i = 0; i < 4; ++i) {
		unsigned int bits;
		memcpy(&bits, &values[i], sizeof(bits));
		hash = (hash ^ bits) * 16777619u;
	}
	return hash;
}

static const unsigned int D3DX_SWEEP_SEED = 0x2003d3dau;
static const unsigned int D3DX_SWEEP_HASH_BASIS = 2166136261u;

#endif // D3DX_SWEEP_H
