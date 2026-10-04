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

// DetRound::To_Long has to round half to even, because the simulation's callers were written
// against an instruction that does and their arithmetic is built on it.  This is the test that
// says so on whichever architecture it is compiled for: x64 reaches cvtss2si and arm64 reaches
// FCVTNS, and both have to agree with the table below.
//
// Links nothing.  DetRound.h is header-only on purpose, so this runs before any library in the
// tree compiles - which is the whole reason it can be trusted as the first evidence that the
// determinism story survives a new architecture.

#include "Lib/DetRound.h"

#include <cmath>
#include <cstdio>

static int failures = 0;

static void expect_long(const char* what, double input, long got, long want)
{
	if (got != want) {
		std::printf("FAIL %s(%.9g): got %ld, want %ld\n", what, input, got, want);
		++failures;
	}
}

// Ties go to the even neighbour, in both directions and away from zero as well as towards it.
// Every one of these is hand-computed from the definition, not read off either instruction.
static void ties_round_to_the_even_neighbour()
{
	struct { float in; long out; } cases[] = {
		{ -3.5f, -4 }, { -2.5f, -2 }, { -1.5f, -2 }, { -0.5f,  0 },
		{  0.5f,  0 }, {  1.5f,  2 }, {  2.5f,  2 }, {  3.5f,  4 },
		{  4.5f,  4 }, {  5.5f,  6 }, { 16.5f, 16 }, { 17.5f, 18 },
	};
	for (unsigned i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i) {
		expect_long("To_Long(float) tie", cases[i].in,
		            DetRound::To_Long(cases[i].in), cases[i].out);
		expect_long("To_Long(double) tie", (double)cases[i].in,
		            DetRound::To_Long((double)cases[i].in), cases[i].out);
	}
}

// Everything that is not a tie rounds to the nearer side, which is the part a truncating cast
// would get wrong.  If someone ever "simplifies" DetRound to (long)f, these are what go red.
static void non_ties_round_to_the_nearer_side()
{
	struct { float in; long out; } cases[] = {
		{  0.4f,  0 }, {  0.6f,  1 }, {  1.4f,  1 }, {  1.6f,  2 },
		{ -0.4f,  0 }, { -0.6f, -1 }, { -1.4f, -1 }, { -1.6f, -2 },
		{  0.99f, 1 }, { -0.99f, -1 }, { 99.51f, 100 }, { -99.51f, -100 },
	};
	for (unsigned i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i) {
		expect_long("To_Long(float)", cases[i].in,
		            DetRound::To_Long(cases[i].in), cases[i].out);
	}
}

// hrawanim.cpp computes Float_To_Long(frame - 0.499999f) to floor a frame number, which only works
// if the conversion rounds to nearest.  Two separate claims here rather than one, and the reason is
// worth writing down.
//
// The first version of this function asserted the single obvious thing - that the idiom floors - and
// it went red at 33, 35, 37 and every odd integer up to 63.  The instinct was that arm64 had got the
// rounding wrong.  It had not: the test was wrong, and the code it was testing was faithfully
// reproducing what x64 does.  A test going red is not yet evidence about the code.
//
// The idiom is exact only below 33.  0.499999f sits 1e-6 below a half, and float spacing is 1.9e-6
// in [16,32) but 3.8e-6 in [32,64) - so from 32 upwards the subtraction lands exactly on x.5 and
// becomes a tie.  At an odd integer frame the tie then rounds to the even neighbour below, and the
// idiom returns frame-1: To_Long(33 - 0.499999f) is To_Long(32.5f) is 32, not 33.  Even integers
// survive it by luck, because there the even neighbour is the answer the caller wanted.
//
// That is not an arm64 artefact.  cvtss2si under _RC_NEAR does exactly the same thing, so the
// Windows build has always behaved this way and every architecture agrees - which is the property
// this port needs.  It is recorded (port defect 4) as something for whoever owns hrawanim.cpp,
// because an animation picking frame 32 where it means 33 is a bug wherever it runs.
static void the_frame_number_idiom_rounds_to_nearest()
{
	for (int frame = 0; frame < 64; ++frame) {
		for (int step = 0; step < 4; ++step) {
			const float f = (float)frame + (float)step * 0.25f;
			const float biased = f - 0.499999f;

			// What the conversion must do: agree with the C library's round-to-nearest-even.
			const long got = DetRound::To_Long(biased);
			const long want = (long)std::nearbyintf(biased);
			if (got != want) {
				std::printf("FAIL idiom rounding at %.9g: got %ld, nearbyintf %ld\n",
				            f, got, want);
				++failures;
			}

			// And below 33 it really is a floor, which is what the callers assume.
			if (f < 33.0f && got != (long)std::floor(f)) {
				std::printf("FAIL idiom floor at %.9g: got %ld, floor %ld\n",
				            f, got, (long)std::floor(f));
				++failures;
			}
		}
	}
}

// An independent implementation of the same rule.  nearbyint rounds in the current mode, which is
// round-to-nearest-ties-to-even by default and is never changed in this process, so it is a second
// opinion from the C library rather than a restatement of the instruction under test.
static void a_sweep_agrees_with_nearbyint()
{
	for (long i = -200000; i <= 200000; ++i) {
		const float f = (float)i * 0.25f;
		const long got = DetRound::To_Long(f);
		const long want = (long)std::nearbyintf(f);
		if (got != want) {
			std::printf("FAIL sweep at %.9g: got %ld, nearbyintf %ld\n", f, got, want);
			if (++failures > 10) return;
		}
	}
	for (long i = -100000; i <= 100000; ++i) {
		const double d = (double)i * 0.5 + 0.25;
		const long got = DetRound::To_Long(d);
		const long want = (long)std::nearbyint(d);
		if (got != want) {
			std::printf("FAIL double sweep at %.17g: got %ld, nearbyint %ld\n", d, got, want);
			if (++failures > 20) return;
		}
	}
}

int main(void)
{
	ties_round_to_the_even_neighbour();
	non_ties_round_to_the_nearer_side();
	the_frame_number_idiom_rounds_to_nearest();
	a_sweep_agrees_with_nearbyint();

	if (failures != 0) {
		std::printf("detround: %d failure(s)\n", failures);
		return 1;
	}
	std::printf("detround: rounds half to even on this architecture\n");
	return 0;
}
