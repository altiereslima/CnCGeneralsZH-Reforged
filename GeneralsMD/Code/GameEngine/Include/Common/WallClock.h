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

// The local wall-clock date and time, as the engine stores and shows it.
//
// This is Windows' SYSTEMTIME, and its layout is part of a file format: RecorderClass writes the
// struct raw into every replay header (fwrite of sizeof(SYSTEMTIME), Recorder.cpp) and reads it
// back the same way, so a different size here would misalign every field after it and a replay
// recorded on one platform would not load on another.  Save games are not affected - they carry
// the eight fields through Xfer one at a time.
//
// On Windows WallClockTime IS SYSTEMTIME and getLocalWallClock IS GetLocalTime, so nothing there
// changes; this header adds no include there, since every file that names these already sees
// <windows.h>.  Elsewhere it is the same eight 16-bit fields in the same order, pinned by the
// asserts below, filled from gettimeofday and localtime_r.

#pragma once

#ifndef __WALLCLOCK_H_
#define __WALLCLOCK_H_

#if defined(_WIN32)

typedef SYSTEMTIME WallClockTime;

inline void getLocalWallClock(WallClockTime *t) { GetLocalTime(t); }

#else

#include "Lib/BaseType.h"
#include <stddef.h>
#include <sys/time.h>
#include <time.h>

struct WallClockTime
{
	UnsignedShort wYear;
	UnsignedShort wMonth;			// 1-12
	UnsignedShort wDayOfWeek;		// 0 = Sunday, as struct tm's tm_wday
	UnsignedShort wDay;				// 1-31
	UnsignedShort wHour;
	UnsignedShort wMinute;
	UnsignedShort wSecond;
	UnsignedShort wMilliseconds;
};

static_assert(sizeof(WallClockTime) == 16, "WallClockTime is SYSTEMTIME's 16 bytes: it is in the replay header");
static_assert(offsetof(WallClockTime, wYear) == 0 && offsetof(WallClockTime, wMonth) == 2 &&
              offsetof(WallClockTime, wDayOfWeek) == 4 && offsetof(WallClockTime, wDay) == 6 &&
              offsetof(WallClockTime, wHour) == 8 && offsetof(WallClockTime, wMinute) == 10 &&
              offsetof(WallClockTime, wSecond) == 12 && offsetof(WallClockTime, wMilliseconds) == 14,
              "WallClockTime's fields are in SYSTEMTIME's order");

inline void getLocalWallClock(WallClockTime *t)
{
	struct timeval now;
	gettimeofday(&now, NULL);
	const time_t seconds = now.tv_sec;
	struct tm local;
	localtime_r(&seconds, &local);
	t->wYear = (UnsignedShort)(local.tm_year + 1900);
	t->wMonth = (UnsignedShort)(local.tm_mon + 1);
	t->wDayOfWeek = (UnsignedShort)local.tm_wday;
	t->wDay = (UnsignedShort)local.tm_mday;
	t->wHour = (UnsignedShort)local.tm_hour;
	t->wMinute = (UnsignedShort)local.tm_min;
	t->wSecond = (UnsignedShort)(local.tm_sec > 59 ? 59 : local.tm_sec);	// a leap second; SYSTEMTIME has none
	t->wMilliseconds = (UnsignedShort)(now.tv_usec / 1000);
}

#endif

#endif // __WALLCLOCK_H_
