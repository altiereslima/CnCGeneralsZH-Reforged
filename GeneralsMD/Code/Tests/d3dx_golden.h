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

// D3DXVec4Transform over BezierSegment's basis matrix, as Microsoft's d3dx9_43.dll computes it.
//
// Every output here came out of Microsoft's machine code, not out of this repository's.  The
// three transform bodies of d3dx9_43.dll x64 9.29.952.3111 (sha256 84b900db...67b4, the June 2010
// redistributable every Steam install of the game carries) were run under Rosetta by
// Tests/d3dx_oracle, and the outputs recorded:
//
//   out        the scalar body (RVA 0x3efa0) and the non-Intel SSE body (RVA 0x2112c0), which
//              agreed on every lane of every row
//   intel_x    lane x from the GenuineIntel SSE body (RVA 0x211e60).  It agreed with `out` on
//              lanes y, z and w of every row, and differs on x wherever the two term orders do
//
// Two programs read this table:
//   - test_d3dxportable.cpp asserts that d3dxportable.h reproduces `out`.  It runs on every build
//     and needs no DLL.
//   - d3dx_oracle.cpp re-runs the DLL's own bytes over the same inputs whenever someone supplies
//     the DLL, so the table cannot quietly drift from what it claims to record.
//
// Values are IEEE-754 single-precision bit patterns, never decimals: a decimal rendering loses
// exactly the last-bit differences this exists to pin.

#pragma once

#ifndef D3DX_GOLDEN_H
#define D3DX_GOLDEN_H

// BezierSegment.cpp's s_bezBasisMatrix, row-major, as D3DMATRIX lays it out.
static const float D3DX_GOLDEN_BASIS[16] = {
	-1.0f,  3.0f, -3.0f,  1.0f,
	 3.0f, -6.0f,  3.0f,  0.0f,
	-3.0f,  3.0f,  0.0f,  0.0f,
	 1.0f,  0.0f,  0.0f,  0.0f,
};

struct D3DXGoldenRow
{
	unsigned int in[4];
	unsigned int out[4];
	unsigned int intel_x;
};

static const D3DXGoldenRow D3DX_GOLDEN_ROWS[] = {
	// dx9_smoke's unit vector: the columns sum to 0,0,0,1
	{ { 0x3f800000, 0x3f800000, 0x3f800000, 0x3f800000 }, { 0x00000000, 0x00000000, 0x00000000, 0x3f800000 }, 0x00000000 },
	// evaluateBezSegmentAtT's (t^3, t^2, t, 1) at t = 0.5, the only t the game evaluates at
	{ { 0x3e000000, 0x3e800000, 0x3f000000, 0x3f800000 }, { 0x3e000000, 0x3ec00000, 0x3ec00000, 0x3e000000 }, 0x3e000000 },
	// the same at t = 1/3: the orders disagree here too
	{ { 0x3d17b427, 0x3de38e3a, 0x3eaaaaab, 0x3f800000 }, { 0x3e97b428, 0x3ee38e36, 0x3e638e3b, 0x3d17b427 }, 0x3e97b427 },
	// t = 0.1
	{ { 0x3a83126f, 0x3c23d70b, 0x3dcccccd, 0x3f800000 }, { 0x3f3a9fbe, 0x3e78d4fe, 0x3cdd2f1b, 0x3a83126f }, 0x3f3a9fbe },
	// t = 0.7
	{ { 0x3eaf9db2, 0x3efae147, 0x3f333333, 0x3f800000 }, { 0x3cdd2f00, 0x3e418940, 0x3ee1cabc, 0x3eaf9db2 }, 0x3cdd2f00 },
	// t = 0.9
	{ { 0x3f3a9fbd, 0x3f4f5c28, 0x3f666666, 0x3f800000 }, { 0x3a831800, 0x3cdd2f00, 0x3e78d500, 0x3f3a9fbd }, 0x3a831800 },
	// signed zero: (-0, 1, 2, 3).  The DLL sums all four products, so w is -0 + 0 + 0 + 0 = +0.  A
	// transform that skipped the basis' zero entries would return -0 here.
	{ { 0x80000000, 0x3f800000, 0x40000000, 0x40400000 }, { 0x00000000, 0x00000000, 0x40400000, 0x00000000 }, 0x00000000 },
	// Control-point coordinates, as BezFwdIterator::start passes them (504.21 .. 4257.47).  Both
	// orders happen to agree on this one.
	{ { 0x43fc1ae1, 0x44db0126, 0x453a55ca, 0x45850bc3 }, { 0x42829040, 0xc25dff00, 0x4569f7a4, 0x43fc1ae1 }, 0x42829040 },
	// 3473.27 .. 3431.43.  x is 54.0612793 left to right and 54.0610352 pairwise.
	{ { 0x45591452, 0x455959f7, 0x45575a7d, 0x455676e1 }, { 0x42583ec0, 0xc2d9eb00, 0x4150ec00, 0x45591452 }, 0x42583e80 },
	// 826.32 .. 853.40, where both orders agree
	{ { 0x444e947b, 0x44520069, 0x44530c57, 0x4455599a }, { 0x41685580, 0xc1e40080, 0x42243cc0, 0x444e947b }, 0x41685580 },
	// 4861.82 .. 2291.26
	{ { 0x4597ee8f, 0x457c1253, 0x4545d401, 0x450f3429 }, { 0x42047fc0, 0xc2eb5280, 0xc51b6060, 0x4597ee8f }, 0x42047f80 },
	// 1528.73 .. 1278.36
	{ { 0x44bf175c, 0x44b6574b, 0x44aa6f8c, 0x449fcb85 }, { 0x420d6ca0, 0xc2977080, 0xc35201a0, 0x44bf175c }, 0x420d6c80 },
	// 3571.87 .. 718.01, with a negative x
	{ { 0x455f3dec, 0x4524d34d, 0x44d30a51, 0x443380a4 }, { 0xc0e6aa00, 0xc22aa480, 0xc52f3fdc, 0x455f3dec }, 0xc0e6a800 },
	// 2878.01 .. 2041.36
	{ { 0x4533e029, 0x45232e7d, 0x45115d23, 0x44ff2b85 }, { 0x4194d540, 0xc257c300, 0xc4485410, 0x4533e029 }, 0x4194d500 },
	// 2592.90 .. 1701.53
	{ { 0x45220e66, 0x450ff2d8, 0x44fa9310, 0x44d4b0f6 }, { 0x408c0a00, 0xc1d4a300, 0xc4594aa8, 0x45220e66 }, 0x408c0c00 },
};

static const unsigned int D3DX_GOLDEN_ROW_COUNT = sizeof(D3DX_GOLDEN_ROWS) / sizeof(D3DX_GOLDEN_ROWS[0]);

#endif // D3DX_GOLDEN_H
