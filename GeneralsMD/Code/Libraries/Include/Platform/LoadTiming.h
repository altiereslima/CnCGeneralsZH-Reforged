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

// PERF1's hitch hunt, off Windows only: ZH_LOAD_TIMING=1 logs every file open or read that takes over
// 20 ms (the system call alone: what a busy or slow disk costs), and every W3D model or texture load over
// 20 ms, split into the time its reads took and the rest - the engine's own parse and build, the part a
// player on a fast disk would still pay.  Each line says whether it ran on the main thread (a frame
// waits for it) or another (the texture loader's).

#pragma once

#ifndef LOADTIMING_H
#define LOADTIMING_H

#if !defined(_WIN32)

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

inline bool zhLoadTimingAsked()
{
	static const bool asked = getenv("ZH_LOAD_TIMING") != NULL;
	return asked;
}

inline double zhLoadNowMs()
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (double)now.tv_sec * 1000.0 + (double)now.tv_nsec / 1.0e6;
}

/// This thread's total time in read and open system calls, for a load to subtract.
inline double &zhLoadReadMs()
{
	static thread_local double total = 0.0;
	return total;
}

inline const char *zhLoadThread()
{
#if defined(__APPLE__)
	return pthread_main_np() ? "main" : "other";
#else
	return "?";
#endif
}

enum { ZH_LOAD_TIMING_REPORT_MS = 20 };

#endif // !_WIN32

#endif // LOADTIMING_H
