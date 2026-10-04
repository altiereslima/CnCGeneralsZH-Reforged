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

// d3dxportable.h has to compute what d3dx9_43.dll computes, bit for bit, because
// DumbProjectileBehavior flies a shell along its output and the flight is in the CRC.
//
// The expected values are absolute and were not produced by this repository.  d3dx_golden.h holds
// outputs of Microsoft's own machine code, and Tests/d3dx_oracle re-derives them from the DLL
// whenever one is supplied.  So this is not a check that two copies of the same arithmetic agree.
//
// Links nothing and needs no DLL, so it runs on every build.
//
// WHAT THIS DOES NOT PROVE:
//   - Which DLL body a Windows machine would have run.  The DLL chooses by CPU vendor.  This
//     matches the scalar and non-Intel bodies and deliberately NOT the GenuineIntel one; see
//     d3dxportable.h.  The game no longer calls the DLL for this on any platform, so the question
//     only matters for replays recorded before that change.  dx9_smoke is how to observe it.
//   - Anything about MSVC, until this runs on Windows.  It has only ever run under clang.
//   - Every input.  These are fifteen rows.  The oracle sweeps millions when the DLL is present,
//     and that is the stronger check.  Without the DLL, this table is all there is.
//   - The architectures against each other.  That is Tests/arch_diff's d3dx section.
//   - The game's own call sites.  BezierSegment.cpp and BezFwdIterator.cpp cannot compile on
//     macOS yet (PreRTS.h needs atlbase.h), so the arithmetic is pinned here and not yet
//     exercised through BezFwdIterator.

#include "d3dxportable.h"
#include "d3dx_golden.h"

#include "d3dx9math.h"

#include <cstdio>
#include <cstring>

static int failures = 0;

static unsigned int bits_of(float value)
{
	unsigned int bits;
	std::memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static float float_of(unsigned int bits)
{
	float value;
	std::memcpy(&value, &bits, sizeof(value));
	return value;
}

static void expect_bits(const char * what, unsigned int row, int lane, unsigned int got,
	unsigned int want)
{
	if (got != want) {
		std::printf("FAIL %s, row %u lane %d: got 0x%08x (%.9g), want 0x%08x (%.9g)\n", what, row,
			lane, got, float_of(got), want, float_of(want));
		++failures;
	}
}

static void load_row(unsigned int row, float in[4])
{
	for (int lane = 0; lane < 4; ++lane) {
		in[lane] = float_of(D3DX_GOLDEN_ROWS[row].in[lane]);
	}
}

// The whole point: the portable transform reproduces the DLL's output on every lane of every row.
static void transform_matches_the_dll_bodies()
{
	for (unsigned int row = 0; row < D3DX_GOLDEN_ROW_COUNT; ++row) {
		float in[4];
		float out[4];
		load_row(row, in);
		D3DXPortable::Vec4Transform(out, in, D3DX_GOLDEN_BASIS);
		for (int lane = 0; lane < 4; ++lane) {
			expect_bits("Vec4Transform", row, lane, bits_of(out[lane]), D3DX_GOLDEN_ROWS[row].out[lane]);
		}
	}
}

// A table the pairwise order also passes would prove nothing about term order.  So this checks
// that the table can tell the two orders apart: pairwise summation, written out here, has to
// reproduce the Intel body's lane x and miss the scalar body's on at least one row.
static void the_table_tells_the_two_orders_apart()
{
	unsigned int discriminating = 0;
	for (unsigned int row = 0; row < D3DX_GOLDEN_ROW_COUNT; ++row) {
		float v[4];
		load_row(row, v);
		const float * m = D3DX_GOLDEN_BASIS;
		const float pairwise_x = (v[0] * m[0] + v[1] * m[4]) + (v[2] * m[8] + v[3] * m[12]);
		expect_bits("pairwise order vs the Intel body", row, 0, bits_of(pairwise_x),
			D3DX_GOLDEN_ROWS[row].intel_x);
		if (D3DX_GOLDEN_ROWS[row].intel_x != D3DX_GOLDEN_ROWS[row].out[0]) {
			++discriminating;
		}
	}
	if (discriminating < 5) {
		std::printf("FAIL only %u rows separate the two term orders; the table has lost its teeth\n",
			discriminating);
		++failures;
	}
}

// All three DLL bodies read the whole vector before writing, so the output may alias the input.
static void transform_may_write_over_its_input()
{
	for (unsigned int row = 0; row < D3DX_GOLDEN_ROW_COUNT; ++row) {
		float v[4];
		load_row(row, v);
		D3DXPortable::Vec4Transform(v, v, D3DX_GOLDEN_BASIS);
		for (int lane = 0; lane < 4; ++lane) {
			expect_bits("Vec4Transform in place", row, lane, bits_of(v[lane]),
				D3DX_GOLDEN_ROWS[row].out[lane]);
		}
	}
}

// D3DXVec4Dot is SDK-inlined, not in the DLL, so its reference is d3dx8math.inl's expression and
// this value is derived by hand from it.  (1e8, 1, -1e8, 1) . (1, 1, 1, 1): left to right,
// 1e8 + 1 rounds to 1e8, minus 1e8 is 0, plus 1 is 1.  Pairwise it would be 0.
static void dot_sums_left_to_right()
{
	const float left[4] = { 1e8f, 1.0f, -1e8f, 1.0f };
	const float right[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	expect_bits("Vec4Dot", 0, -1, bits_of(D3DXPortable::Vec4Dot(left, right)), bits_of(1.0f));
}

// What BezierSegment and BezFwdIterator actually call, on every platform: d3dx9math.h's names,
// with the basis built through D3DXMATRIX's sixteen-float constructor exactly as s_bezBasisMatrix
// is.  This catches a transposed layout, where m[1][0] would not be _21, and on Windows it catches
// D3DXVec4Transform resolving to anything other than d3dxportable.h.
static void d3dx9math_names_reach_the_same_arithmetic()
{
	const D3DXMATRIX basis(
		-1.0f,  3.0f, -3.0f,  1.0f,
		 3.0f, -6.0f,  3.0f,  0.0f,
		-3.0f,  3.0f,  0.0f,  0.0f,
		 1.0f,  0.0f,  0.0f,  0.0f);
	if (basis.m[1][0] != 3.0f || basis._21 != 3.0f || basis(0, 3) != 1.0f) {
		std::printf("FAIL D3DXMATRIX's named elements and m[][] do not alias as D3DMATRIX's do\n");
		++failures;
	}
	for (unsigned int row = 0; row < D3DX_GOLDEN_ROW_COUNT; ++row) {
		float in[4];
		load_row(row, in);
		const D3DXVECTOR4 vector(in[0], in[1], in[2], in[3]);
		D3DXVECTOR4 out(0.0f, 0.0f, 0.0f, 0.0f);
		D3DXVECTOR4 * returned = D3DXVec4Transform(&out, &vector, &basis);
		if (returned != &out) {
			std::printf("FAIL D3DXVec4Transform does not return its output pointer\n");
			++failures;
		}
		const float lanes[4] = { out.x, out.y, out.z, out.w };
		for (int lane = 0; lane < 4; ++lane) {
			expect_bits("D3DXVec4Transform", row, lane, bits_of(lanes[lane]),
				D3DX_GOLDEN_ROWS[row].out[lane]);
		}
	}
	const D3DXVECTOR4 left(1e8f, 1.0f, -1e8f, 1.0f);
	const D3DXVECTOR4 right(1.0f, 1.0f, 1.0f, 1.0f);
	expect_bits("D3DXVec4Dot", 0, -1, bits_of(D3DXVec4Dot(&left, &right)), bits_of(1.0f));
}

int main()
{
	transform_matches_the_dll_bodies();
	the_table_tells_the_two_orders_apart();
	transform_may_write_over_its_input();
	dot_sums_left_to_right();
	d3dx9math_names_reach_the_same_arithmetic();

	if (failures != 0) {
		std::printf("d3dxportable: %d failure(s)\n", failures);
		return 1;
	}
	std::printf("d3dxportable: %u rows match d3dx9_43.dll's scalar and non-Intel bodies bit for bit "
		"(not its GenuineIntel body, by design; see d3dxportable.h)\n", D3DX_GOLDEN_ROW_COUNT);
	return 0;
}
