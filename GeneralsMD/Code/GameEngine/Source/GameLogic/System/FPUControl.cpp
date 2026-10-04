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
// Modified 2026 by İlyas Akın for the macOS/Linux port; see NOTICE.md and the git history.

// FILE: FPUControl.cpp ///////////////////////////////////////////////////////////////////////////
// Desc:   Pinning the FPU so two machines compute the same numbers.
////////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"
#include "GameLogic/FPUControl.h"
#include <float.h>
#if !defined(_WIN32)
#include <fenv.h>
#endif

/* This lives in its own translation unit rather than in GameLogic.cpp, where EA had it, so that a
	 test can call it without linking the whole of the game logic behind it. */

#if !defined(_WIN32)
/* After fesetenv(FE_DFL_ENV), denormals must survive and NaNs must propagate, as they do under
	 Windows' default MXCSR.  Nothing else in the process asserts it, and a flush-to-zero default would
	 change results silently rather than loudly.  The ctest fpucontrol_selfcheck checks the arithmetic;
	 this checks the control register itself, in debug builds. */
static void assertDenormalsAndNaNsAsWindows( void )
{
#if defined(__aarch64__)
	unsigned long long fpcr;
	__asm__ volatile( "mrs %0, fpcr" : "=r"( fpcr ) );
	DEBUG_ASSERTCRASH( (fpcr & ((1ull << 24) | (1ull << 25))) == 0, ("FPCR has FZ or DN set: 0x%llx", fpcr) );
#elif defined(__x86_64__)
	unsigned int mxcsr;
	__asm__ volatile( "stmxcsr %0" : "=m"( mxcsr ) );
	DEBUG_ASSERTCRASH( (mxcsr & ((1u << 15) | (1u << 6))) == 0, ("MXCSR has FTZ or DAZ set: 0x%x", mxcsr) );
#endif
}
#endif

void setFPMode( void )
{
	/* Every float the simulation computes has to come out the same on both machines, and the x87
		 control word decides that: precision and rounding mode.  Nothing in the process guarantees
		 it stays put - Direct3D sets it when it creates a device without D3DCREATE_FPU_PRESERVE,
		 audio and video drivers have historically set it on their own threads, and any DLL loaded
		 into the process may set it once and never restore it.  So it is not set once at startup;
		 GameLogic::update re-asserts it at the top of every logic frame, and so does anything that
		 hands control to the renderer inside a logic loop (the load screens, INI parsing).

		 _fpreset() first, because it puts the whole word - exception masks included - into a known
		 state rather than only the two fields written below. */
#if !defined(_WIN32)
	/* The POSIX half of the same two steps.  fesetenv(FE_DFL_ENV) is _fpreset's counterpart: the
		 whole floating-point environment back to the C library's default - exception masks, sticky
		 flags, and on arm64 FPCR's FZ (flush denormals to zero) and DN (default NaN) off, which is
		 also what Windows' default MXCSR has.  On x86-64 it resets MXCSR the same way, and on glibc
		 it sets the x87 control word to 0x037F - 64-bit extended precision, where Windows' _fpreset
		 leaves 53-bit (0x027F).  That difference reaches long double and x87 code only: float and
		 double go through SSE/NEON, and the simulation has no long double and no x87 code (checked
		 2026-09-25).  Then round to nearest, the one field the Windows branch sets. */
	fesetenv( FE_DFL_ENV );
#if defined(__aarch64__)
	/* FE_DFL_ENV is not enough on glibc.  Measured on Ubuntu 24.04, glibc 2.39, aarch64: after
		 fesetenv(FE_DFL_ENV) FPCR's FZ is clear but DN is still set, because glibc's _FPU_RESERVED
		 (0xfe0fe0f8) keeps bit 25.  macOS clears both.  So clear FZ and DN here, whatever the C
		 library does: with DN set a NaN's payload is not propagated, as it is on Windows. */
	unsigned long long fpcr;
	__asm__ volatile( "mrs %0, fpcr" : "=r"( fpcr ) );
	fpcr &= ~((1ull << 24) | (1ull << 25));
	__asm__ volatile( "msr fpcr, %0" : : "r"( fpcr ) );
#endif
	fesetround( FE_TONEAREST );
	assertDenormalsAndNaNsAsWindows();
#else
	_fpreset();

	/* Rounding to nearest.  EA also asked for 24-bit precision here, which was the x87's way of
		 computing a float at a float's width; SSE does that by itself and the field is gone.

		 EA read the current word with _statusfp(), which returns the *status* word - the sticky
		 exception flags - not the control word.  It happened to be harmless, because the mask below
		 keeps everything except the rounding field and no status flag lands in it.
		 _controlfp(0, 0) is what they meant: it reads the control word without writing. */
	UnsignedInt curVal = _controlfp( 0, 0 );
	UnsignedInt newVal = curVal;
	newVal = (newVal & ~_MCW_RC) | (_RC_NEAR & _MCW_RC);

	_controlfp( newVal & FP_MODE_FIELDS, FP_MODE_FIELDS );
#endif
}

UnsignedInt getFPMode( void )
{
#if !defined(_WIN32)
	return (UnsignedInt)fegetround();
#else
	return _controlfp( 0, 0 ) & FP_MODE_FIELDS;
#endif
}

UnsignedInt expectedFPMode( void )
{
#if !defined(_WIN32)
	return (UnsignedInt)FE_TONEAREST;
#else
	return (_RC_NEAR & _MCW_RC) & FP_MODE_FIELDS;
#endif
}

void restoreFPMode( UnsignedInt mode )
{
#if !defined(_WIN32)
	fesetround( (int)mode );
#else
	_controlfp( mode, FP_MODE_FIELDS );	// exactly the call its two callers used to make themselves
#endif
}
