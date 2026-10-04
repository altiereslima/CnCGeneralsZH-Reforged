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

// Give up the calling thread for this many milliseconds.
//
// A platform service, not a spelling, so it lives behind this name rather than in MSVCCompat.h.
// Windows keeps Sleep() exactly.  Elsewhere it is std::this_thread::sleep_for.  Every caller in the
// engine waits in a load screen, a video or a network hand-shake - none of them is a simulation
// step, so how long a sleep really lasts reaches no computed value.
//
// Not for 0: Sleep(0) on Windows means "yield the rest of my slice", which sleep_for(0) is allowed
// not to do (ThreadClass::Sleep_Ms in WWLib handles that case).  Nothing here passes 0.

#pragma once

#if defined(_WIN32)
#include <windows.h>
inline void sleepMilliseconds( unsigned int milliseconds ) { ::Sleep( milliseconds ); }
#else
#include <chrono>
#include <thread>
inline void sleepMilliseconds( unsigned int milliseconds )
{
	std::this_thread::sleep_for( std::chrono::milliseconds( milliseconds ) );
}
#endif
