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

// The two D3DX functions that reach GameLogic, written out so that every machine computes them the
// same way.  d3dx9math.h routes both here on every platform, Windows included.
//
// BezierSegment and BezFwdIterator call D3DXVec4Transform and D3DXVec4Dot, and
// DumbProjectileBehavior flies a shell along the result, so this arithmetic is part of the replay
// and network CRC.  d3dx9math.h warns that "a hand-written 4x4 inverse or transform would be a
// rounding difference nobody could see until a replay diverged".  This file is that hand-written
// transform, so it is not written the way that reads best.  Its term order was taken from
// Microsoft's machine code.
//
// What the real DLL does, taken from d3dx9_43.dll x64, 9.29.952.3111 (June 2010 redistributable,
// sha256 84b900dbd7fa978d6e0caee26fc54f2f61d92c9c75d10b35f00e3e82cd1d67b4):
//
//   The export is a thunk through a function-pointer slot, and the slot is filled once, by CPU:
//
//     DisablePSGP / DisableD3DXPSGP = 1 under HKLM\Software\Microsoft\Direct3D,
//     or no SSE                                  scalar body, RVA 0x3efa0    ((a + b) + c) + d
//     CPUID vendor "GenuineIntel"                SSE body,    RVA 0x211e60   (a + b) + (c + d)
//     any other vendor                           SSE body,    RVA 0x2112c0   ((a + b) + c) + d
//
//   where a..d are x*m[0][j], y*m[1][j], z*m[2][j] and w*m[3][j].
//
// So the DLL does not have one term order.  It has two, and it chooses between them by the vendor
// of the processor.  The dispatch above was read out of the disassembly, not observed on a running
// Windows machine.  Each body's arithmetic was executed: Tests/d3dx_oracle runs Microsoft's own
// bytes for all three under Rosetta and compares them with this file.
//
// This file uses the left-to-right order, for three reasons:
//   - it is the SDK's reference C: the scalar body is what D3DX falls back to when its CPU-specific
//     paths are disabled;
//   - it is what every non-Intel x64 processor runs;
//   - it is what Wine's d3dx9 computes, so it is what a Mac player running the Windows build under
//     CrossOver computes today.
// It is bit-identical to the Intel path on every lane except x, the only lane where the Bezier
// basis has four nonzero terms.  There the two disagree on 46.8% of Tests/d3dx_oracle's inputs
// shaped like BezFwdIterator's, the ones a shell's flight is built from (35.7% over all its basis
// inputs, which include random-t vectors the game never evaluates).  While Windows took this function from the DLL, that was a defect of the shipping
// Windows game (port defect 7), and no Mac build could match an Intel and an
// AMD Windows machine at once.  So Windows now uses this file too, and every machine sums in this
// order.
//
// Rules for anyone editing this file:
//   - Do not reassociate, fuse, vectorise by hand or "improve" either function.  The goal is
//     bit-identity with a DLL, not numerical quality.
//   - Compute into locals before writing, as all three DLL bodies do, so `out` may alias `vector`.
//   - The one thing C cannot pin is which NaN comes out when two operands are both NaN.  A NaN
//     here means the simulation has already diverged, so that is recorded rather than fought.
//
// Self-contained on purpose: floats only, no Windows types, and no Lib/BaseType.h (which would
// redefine NULL against WWLib's headers).  test_d3dxportable.cpp, the arch_diff probe and the DLL
// oracle all include this file directly, so each of them tests this code and not a copy of it.

#pragma once

#ifndef D3DXPORTABLE_H
#define D3DXPORTABLE_H

namespace D3DXPortable
{

// D3DXVec4Transform: row vector times matrix.  `matrix` is sixteen floats, row-major, laid out as
// D3DMATRIX is (_11 _12 _13 _14 _21 ...).  out[j] = x*m[0][j] + y*m[1][j] + z*m[2][j] + w*m[3][j],
// summed strictly left to right.
inline float * Vec4Transform(float * out, const float * vector, const float * matrix)
{
#if defined(__clang__)
	// CMakeLists.txt gives every GCC and Clang build -ffp-contract=off.  Under clang, this also
	// keeps the function unfused in a translation unit that loses that flag and falls back to
	// clang's default.  Measured with -mfma on x86_64 and on arm64: with no flag, or with
	// -ffp-contract=on, the same expression without this pragma fuses 12 times and this function
	// fuses 0.  It does NOT hold against -ffp-contract=fast, which clang documents as overriding
	// the pragma.  GCC ignores it entirely, and GCC's own default IS fast.  So under GCC, and under
	// clang with fast, only the build flag protects this.  d3dxportable_selfcheck asserts absolute
	// bit patterns and fails if either compiler fuses here.  MSVC has nothing to fuse into on x64
	// without /arch:AVX2.
#pragma clang fp contract(off)
#endif
	const float x = vector[0];
	const float y = vector[1];
	const float z = vector[2];
	const float w = vector[3];

	const float out_x = x * matrix[0] + y * matrix[4] + z * matrix[8]  + w * matrix[12];
	const float out_y = x * matrix[1] + y * matrix[5] + z * matrix[9]  + w * matrix[13];
	const float out_z = x * matrix[2] + y * matrix[6] + z * matrix[10] + w * matrix[14];
	const float out_w = x * matrix[3] + y * matrix[7] + z * matrix[11] + w * matrix[15];

	out[0] = out_x;
	out[1] = out_y;
	out[2] = out_z;
	out[3] = out_w;
	return out;
}

// D3DXVec4Dot, which the SDK inlined: d3dx8math.inl's expression, term for term, the same one
// d3dx9math.h's Windows branch carries.
inline float Vec4Dot(const float * left, const float * right)
{
#if defined(__clang__)
#pragma clang fp contract(off)	// see Vec4Transform
#endif
	return left[0] * right[0] + left[1] * right[1] + left[2] * right[2] + left[3] * right[3];
}

} // namespace D3DXPortable

#endif // D3DXPORTABLE_H
