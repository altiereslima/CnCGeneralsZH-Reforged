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

/* fpucontrol_selfcheck: after setFPMode(), floating point behaves as it does under Windows' default
	 MXCSR - round to nearest, denormals produced and read as themselves, NaN payloads propagated.

	 ARMED.  Before calling setFPMode it puts the environment where setFPMode must undo it - round
	 upward, and flush-to-zero plus default-NaN (arm64 FPCR FZ and DN; x86-64 MXCSR FTZ and DAZ) - and
	 runs every check once WITHOUT setFPMode, requiring each group to fail.  A check that passes in that
	 state could not see what it is checking, and the test fails.  Then setFPMode, and everything must
	 pass.

	 Exact bits, not tolerances: FLT_MIN * 0.5 is exactly 0x00400000, a denormal; the smallest denormal
	 times two is exactly 0x00000002; a quiet NaN with payload 0x123 plus one is itself; 1 + 2^-24
	 rounds to even, to 1.

	 What it does NOT establish: that two machines compute the same simulation.  This is the control
	 register's state, measured through arithmetic, on this machine only.  And one thing is different
	 by architecture and cannot be set: the NaN that an invalid operation GENERATES (0/0) is 0xFFC00000
	 on x86 (Windows included) and 0x7FC00000 on arm64, whatever FPCR says.  That is pinned below per
	 architecture so a change is noticed; whether any NaN's bits reach a CRC or a save is a separate
	 question (B5). */

/* FPUControl.h alone, not PreRTS.h: the engine headers define key functions inline outside their
	 classes (GameMemory.h's EMPTY_DTOR), and GCC emits those classes' vtables in every TU that sees
	 them, which then need the engine's out-of-line pieces (AudioEventRTS, MemoryPool) that this
	 self-check does not link.  Clang does not when optimizing, which is why macOS never noticed;
	 Linux/gcc did.  (Clang at -O0 can: see the stub file.)
	 FPUControl.cpp itself must include PreRTS.h, so it names them anyway: they are stubbed, to abort
	 if ever called, in Tests/gcc_eager_vtable_stubs.cpp. */
#include "GameLogic/FPUControl.h"

#include <fenv.h>
#include <float.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* FPUControl.cpp's debug assert calls this, and gameengine, which defines it, does not link here.
	 A fired assert has to fail the test, so this prints and aborts.  Defined in every configuration:
	 unused in Release, and required in Debug. */
extern "C" char* TheCurrentIgnoreCrashPtr = NULL;		// Debug.cpp's, which does not link here either

extern "C" void DebugCrash( const char *format, ... )
{
	va_list args;
	va_start( args, format );
	vfprintf( stderr, format, args );
	va_end( args );
	fprintf( stderr, "\n" );
	abort();
}

static uint32_t bitsOf( float f ) { uint32_t u; memcpy( &u, &f, sizeof( u ) ); return u; }
static uint64_t bitsOf( double d ) { uint64_t u; memcpy( &u, &d, sizeof( u ) ); return u; }
static float floatFromBits( uint32_t u ) { float f; memcpy( &f, &u, sizeof( f ) ); return f; }

/* Each group returns its number of wrong answers, and says what they were when asked to. */
static int roundingGroup( bool say )
{
	volatile float one = 1.0f, halfUlp = 5.9604644775390625e-08f;		// 2^-24: exactly half an ulp of 1
	const uint32_t got = bitsOf( (float)(one + halfUlp) );
	if (got != 0x3F800000u && say) printf( "  FAIL rounding: 1 + 2^-24 = %08x, want 3f800000 (nearest even)\n", got );
	return got != 0x3F800000u;
}

static int denormalGroup( bool say )
{
	int bad = 0;
	volatile float fmin = FLT_MIN, half = 0.5f, smallest = floatFromBits( 0x00000001u ), two = 2.0f;
	volatile double dmin = DBL_MIN, dhalf = 0.5;
	const uint32_t out = bitsOf( (float)(fmin * half) );			// a denormal result: kept, not flushed
	const uint32_t in = bitsOf( (float)(smallest * two) );			// a denormal input: read as itself
	const uint64_t dout = bitsOf( (double)(dmin * dhalf) );
	if (out != 0x00400000u) { ++bad; if (say) printf( "  FAIL denormal result: FLT_MIN/2 = %08x, want 00400000\n", out ); }
	if (in != 0x00000002u) { ++bad; if (say) printf( "  FAIL denormal input: 2 * 0x00000001 = %08x, want 00000002\n", in ); }
	if (dout != 0x0008000000000000ull) { ++bad; if (say) printf( "  FAIL denormal double: DBL_MIN/2 = %016llx\n", (unsigned long long)dout ); }
	return bad;
}

static int nanPropagationGroup( bool say )
{
	volatile float payload = floatFromBits( 0x7FC00123u ), one = 1.0f;
	const uint32_t got = bitsOf( (float)(payload + one) );
	if (got != 0x7FC00123u && say) printf( "  FAIL NaN propagation: qNaN(0x123) + 1 = %08x, want 7fc00123\n", got );
	return got != 0x7FC00123u;
}

static void armBadState( void )
{
	fesetround( FE_UPWARD );
#if defined(__aarch64__)
	unsigned long long fpcr;
	__asm__ volatile( "mrs %0, fpcr" : "=r"( fpcr ) );
	fpcr |= (1ull << 24) | (1ull << 25);		// FZ, DN
	__asm__ volatile( "msr fpcr, %0" : : "r"( fpcr ) );
#elif defined(__x86_64__)
	unsigned int mxcsr;
	__asm__ volatile( "stmxcsr %0" : "=m"( mxcsr ) );
	mxcsr |= (1u << 15) | (1u << 6);				// FTZ, DAZ
	__asm__ volatile( "ldmxcsr %0" : : "m"( mxcsr ) );
#else
#error "fpucontrol_selfcheck: arm the flush-to-zero control for this architecture"
#endif
}

int main( void )
{
	int failures = 0;

	// 1. The control: in the armed state every group must see something wrong.
	armBadState();
	const int controlRounding = roundingGroup( false );
	const int controlDenormal = denormalGroup( false );
	const int controlNaN = nanPropagationGroup( false );
	/* NaN propagation can only be armed where the hardware has a default-NaN switch: arm64's FPCR.DN.
		 x86 has none - MXCSR's FTZ and DAZ do not touch NaNs - so there the NaN group has nothing to
		 detect and its control is not required.  Measured under Rosetta: it passes armed, as it must. */
#if defined(__aarch64__)
	const bool nanArmable = true;
#else
	const bool nanArmable = false;
#endif
	if (!controlRounding || !controlDenormal || (nanArmable && !controlNaN))
	{
		printf( "FAIL control: a group passed in the armed state (rounding %d, denormal %d, NaN %d), so it cannot see\n",
						controlRounding, controlDenormal, controlNaN );
		++failures;
	}

	// 2. After setFPMode everything must be as on Windows.
	setFPMode();
	if (getFPMode() != expectedFPMode()) { printf( "  FAIL getFPMode() != expectedFPMode()\n" ); ++failures; }
	failures += roundingGroup( true );
	failures += denormalGroup( true );
	failures += nanPropagationGroup( true );

	// 3. The one architecture difference setFPMode cannot remove, pinned so a change is noticed.
	volatile float zero = 0.0f;
	const uint32_t generated = bitsOf( (float)(zero / zero) );
#if defined(__aarch64__)
	const uint32_t expected = 0x7FC00000u;		// arm64's default NaN, whatever DN says
#else
	const uint32_t expected = 0xFFC00000u;		// x86's "real indefinite", as on Windows
#endif
	if (generated != expected) { printf( "  FAIL generated NaN: 0/0 = %08x, want %08x\n", generated, expected ); ++failures; }

	printf( "fpucontrol_selfcheck: %s (control saw: rounding %d, denormal %d, NaN %d)\n",
					failures ? "FAILED" : "OK", controlRounding, controlDenormal, controlNaN );
	return failures ? 1 : 0;
}
