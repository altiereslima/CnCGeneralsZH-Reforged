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

/*
** The two questions this engine asks a clock, and nothing else.
**
** Five hundred call sites across eighty-four files read three different Windows clocks -
** timeGetTime, GetTickCount and QueryPerformanceCounter - and between them they are asking two
** questions: "what time is it, in milliseconds" and "give me the highest-resolution tick this
** machine has".  Everything here answers one of those.  There is no clock object, no duration
** type and no std::chrono: the call sites are a subtraction and a comparison, and they stay that
** way.
**
** ON WINDOWS EACH FUNCTION FORWARDS TO EXACTLY THE CALL IT REPLACED.  That is the whole of the
** Windows contract: a Windows build after this change reads the same counter, in the same units,
** with the same resolution and the same wrap, as the build before it.  Nothing here is an
** improvement on what was there.
**
** WHY THERE ARE TWO MILLISECOND CLOCKS.  timeGetTime and GetTickCount both answer milliseconds
** since boot as a 32-bit unsigned, and differ only in resolution: timeGetTime is 1ms once
** something has asked for fine resolution below, GetTickCount is the scheduler's tick, about
** 15.6ms.  Folding them into one function would be tidier and would quietly give fifty-one
** GetTickCount call sites a finer clock than they have ever had.  That is a timing change on
** Windows, it is small, and nobody on this project can measure it - so they stay apart until
** somebody with a Windows machine can say the merge is safe.  On macOS they are the same clock;
** merging them later is one line here rather than fifty-one call sites again.
**
** THE 32-BIT WRAP IS PART OF THE CONTRACT, NOT AN ACCIDENT.  The millisecond clocks wrap every
** 49.7 days and some interval arithmetic in this engine relies on it - SysTimeClass::Reset writes
** `WrapAdd = 0 - StartTime`, which is only correct in 32-bit unsigned arithmetic.  So these return
** `unsigned int` and the macOS side truncates to 32 bits deliberately rather than handing back a
** wider value that would not wrap where the callers expect it to.  Callers that store the result
** in a signed `Int` were already wrong at 24.8 days and are no more wrong now; that is a
** pre-existing bug, and not this header's to fix.
**
** THE TICK CLOCK IS NOT A SIMULATION INPUT.  Every reader of it in GameLogic was checked, one at a
** time, when this header was written: all of them are measurement that ends in a DEBUG_LOG or an
** on-screen statistic, and none of them reaches a branch the simulation takes.  Keep it that way.
** If you are about to read a clock to decide something the simulation decides, stop: two machines
** will answer differently and the replay will not match.  A sweep of every clock read (B2)
** established this.
*/

#pragma once

#ifndef _LIB_CLOCK_H_
#define _LIB_CLOCK_H_

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>		// timeGetTime, timeBeginPeriod; the callers linked winmm for these already
#else
#include <time.h>
#endif

/*
** What time is it, in milliseconds.
**
** Monotonic, counting from some moment before the process started, wrapping at 2^32.  The origin
** is not defined and no caller may depend on it: the only correct use is the difference between
** two readings.
*/
inline unsigned int Clock_Milliseconds(void)
{
#ifdef _WIN32
	return (unsigned int)timeGetTime();
#else
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC_RAW, &now);
	// Truncated on purpose - see the note on the wrap above.
	return (unsigned int)(((unsigned long long)now.tv_sec * 1000ULL
		+ (unsigned long long)now.tv_nsec / 1000000ULL) & 0xFFFFFFFFULL);
#endif
}

/*
** The same question asked of the coarse clock, for the call sites that asked GetTickCount.
**
** Same units, same wrap, same rules as above.  On Windows this is deliberately the coarser of the
** two clocks; on macOS there is only one clock and this is it.  See the note above for why the
** two have not been merged.
*/
inline unsigned int Clock_Milliseconds_Coarse(void)
{
#ifdef _WIN32
	return (unsigned int)GetTickCount();
#else
	return Clock_Milliseconds();
#endif
}

/*
** The highest-resolution tick this machine has, and how many of them go in a second.
**
** A tick means nothing on its own and the rate is not a constant - ask for it rather than
** assuming one.  The pair is only ever used as (later - earlier) / rate, and every caller in the
** tree does exactly that.  Zero from Clock_Ticks_Per_Second means the machine would not answer,
** which the Windows call could always do and which every caller already guards against.
*/
inline long long Clock_Ticks(void)
{
#ifdef _WIN32
	LARGE_INTEGER ticks;
	if (!QueryPerformanceCounter(&ticks)) return 0;
	return (long long)ticks.QuadPart;
#else
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC_RAW, &now);
	return (long long)now.tv_sec * 1000000000LL + (long long)now.tv_nsec;
#endif
}

inline long long Clock_Ticks_Per_Second(void)
{
#ifdef _WIN32
	LARGE_INTEGER rate;
	if (!QueryPerformanceFrequency(&rate)) return 0;
	return (long long)rate.QuadPart;
#else
	return 1000000000LL;	// Clock_Ticks counts nanoseconds
#endif
}

/*
** Ask the machine for the finest millisecond clock it will give, and give it back.
**
** On Windows this is the scheduler's timer period, which is a process-wide setting with a
** system-wide effect, which is why it is paired and why the game gives it back on the way out.
** macOS has no equivalent and needs none - its clock is already better than the period would buy -
** so these are a no-op there and answer true.
**
** False means the machine refused; two of the three callers assert on it today and keeping a
** return value lets them go on doing that.
*/
inline bool Clock_Begin_Fine_Resolution(void)
{
#ifdef _WIN32
	return timeBeginPeriod(1) == TIMERR_NOERROR;
#else
	return true;
#endif
}

inline bool Clock_End_Fine_Resolution(void)
{
#ifdef _WIN32
	return timeEndPeriod(1) == TIMERR_NOERROR;
#else
	return true;
#endif
}

#endif // _LIB_CLOCK_H_
