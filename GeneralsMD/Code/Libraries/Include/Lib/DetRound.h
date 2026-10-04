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

// Float to long, rounding half to even, the same way on x64 and on arm64.
//
// This is dettrig.h's problem in one instruction, and it is here for the same reason: the
// simulation's arithmetic has to give the same answer on every machine in a network game, so the
// places where an architecture is free to choose get pinned to one choice by hand.
//
// What it replaces, and why it is not a cast.  EA wrote `fld` / `fistp`, which rounds in the FPU's
// current mode; the x64 port wrote that as SSE2's cvtss2si, which rounds in MXCSR's current mode.
// A C cast would truncate instead, and callers depend on the rounding - see the comment on
// WWMath::Float_To_Long and every WWMath::Float_To_Long(x - 0.499999f) in hrawanim.cpp, which is
// arithmetic built on top of round-to-nearest.
//
// Which mode "current" is.  GameLogic/FPUControl.cpp's setFPMode() sets _RC_NEAR and nothing else,
// and GameLogic::update re-asserts it at the top of every logic frame precisely because a graphics
// or audio driver may have left it somewhere else.  So the mode this conversion is designed to run
// in is round-to-nearest, ties to even.
//
// On arm64 that is FCVTNS - "convert to signed integer, rounding to nearest with ties to even" -
// which is what vcvtns_s32_f32 emits.  Two things follow, and the second is an improvement rather
// than a compromise:
//
//   1. It is bit-identical to cvtss2si under _RC_NEAR, which is the only mode the simulation is
//      ever supposed to be in.  test_detround.cpp checks the tie cases and sweeps the rest.
//   2. FCVTNS ignores FPCR's rounding field, so on arm64 this conversion cannot be perturbed by
//      anything that leaves the rounding mode somewhere else.  The whole class of bug that makes
//      setFPMode() get called every frame on Windows does not exist here for this operation.
//
// Inputs outside the range of a 32-bit integer, and NaN, are where the two architectures genuinely
// differ, and they differ in two separate ways.  Measured on both, not read off a manual:
//
//                    x86 cvtss2si / cvtsd2si      arm64 FCVTNS, raw
//   out of range     0x80000000 ("indefinite")    saturates: 0x7FFFFFFF or 0x80000000
//   NaN              0x80000000                   0
//
// Neither is reachable from the simulation - the callers are frame numbers, scanline coordinates
// and pixel counts, and a NaN arriving here means the arithmetic upstream has already diverged - so
// the first version of this header recorded the difference and left it.  That was the wrong call.
// Undoing the saturation costs two compares that never branch, and it buys something worth more
// than two compares: DetRound::To_Long now returns the same bits as the x86 instruction for *every*
// input, so "is this conversion architecture-independent" stops being a question with a footnote.
// A property that holds only on the inputs somebody once reasoned about is a property the next
// person has to re-derive.
//
// What is NOT left to chance is the width.  `long` is 64 bits on Darwin's LP64 and 32 on Windows'
// LLP64, so the obvious arm64 spelling for the double overload - convert to a 64-bit integer and
// let the return type narrow it - quietly means two different things on the two platforms, and on
// macOS means no narrowing at all.  An earlier version of this header said "narrowing afterwards is
// exact for every value a long can hold", which was true only on Windows and was the kind of
// sentence someone later builds on.  Both overloads now produce a 32-bit result explicitly, which
// is what cvtss2si and cvtsd2si produce and what every call site in the tree assigns to an int.
//
// Found by the cross-architecture harness (Tests/arch_diff), which runs the real x86 instructions under
// Rosetta and compares them against these: 11 of 75 rows differed, all of them here, none of them
// a tie or an in-range value.  Worth knowing why this file's own sweep could not see it: the sweep
// checks against the C library's round-to-nearest, and the C library on this machine has the same
// 64-bit long and the same saturation as the code under test.  Reference and code agreed with each
// other while both differed from x86.  A reference that shares the property under test proves
// nothing about it.

#pragma once

#ifndef DETROUND_H
#define DETROUND_H

#include <stdint.h>   // INT32_MIN / INT32_MAX, for the width pinning below

#if defined(__aarch64__) || defined(_M_ARM64)
#include <arm_neon.h>
#define DETROUND_ARM64 1
#elif defined(__x86_64__) || defined(_M_X64) || defined(_M_AMD64)
#include <emmintrin.h>
#define DETROUND_SSE2 1
#else
#error "DetRound has no implementation for this architecture; add one and extend test_detround.cpp."
#endif

namespace DetRound
{

	/// float -> long, rounding half to even.
	inline long To_Long(float f)
	{
#if defined(DETROUND_ARM64)
		const int32_t v = vcvtns_s32_f32(f);
		// FCVTNS saturates where cvtss2si yields 0x80000000, so the two disagree at the edges unless
		// the saturation is undone.  INT32_MAX can only ever be saturation - 2147483647 is not a
		// float - so it always means "x86 would have said indefinite".  INT32_MIN needs no test at
		// all: whether it is saturation or the legitimate conversion of -2147483648.0f, x86 answers
		// INT32_MIN either way.  NaN is the one case FCVTNS does not signal, giving 0.
		if (v == INT32_MAX) return (long)INT32_MIN;
		if (f != f) return (long)INT32_MIN;
		return (long)v;
#else
		return (long)_mm_cvtss_si32(_mm_set_ss(f));
#endif
	}

	/// double -> long, rounding half to even.  The result is a 32-bit quantity on both platforms.
	inline long To_Long(double f)
	{
#if defined(DETROUND_ARM64)
		// There is no vcvtns_s32_f64 in clang's intrinsic set, only the 64-bit form.  Rounding in 64
		// bits and range-checking the result afterwards is exact - the rounding cannot overflow an
		// int64 for any double - and it is what makes the answer agree with a 32-bit cvtsd2si for
		// every input, infinities included, since those saturate to an int64 edge and fail the test.
		const int64_t wide = vcvtnd_s64_f64(f);
		if (f != f) return (long)INT32_MIN;                          // NaN: FCVTNS gives 0
		if (wide < INT32_MIN || wide > INT32_MAX) return (long)INT32_MIN;
		return (long)wide;
#else
		return (long)_mm_cvtsd_si32(_mm_set_sd(f));
#endif
	}

} // namespace DetRound

#endif // DETROUND_H
