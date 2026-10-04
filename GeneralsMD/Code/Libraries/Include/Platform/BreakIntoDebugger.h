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

// Break into the debugger, and carry on when the programmer continues.
//
// A platform service, not a spelling, so it lives behind this name rather than in MSVCCompat.h.
// Windows keeps DebugBreak() exactly.  Elsewhere it is raise(SIGTRAP), chosen over the compiler
// builtins because every caller means to continue afterwards: RefCountClass's BreakOnReference hook
// stops on one object and then counts on, and Debug.cpp's assertion box has "Retry", which breaks
// and then returns.  GCC's __builtin_trap is noreturn - the code after it may be dropped as
// unreachable, so Retry could not come back - and clang's __builtin_debugtrap does not exist in
// GCC.  raise(SIGTRAP) is POSIX and one mechanism under both compilers.  Measured on macOS: lldb
// stops on it, and with the signal not passed on to the process (process handle SIGTRAP --pass
// false) continues from it; without a debugger the process ends with status 133, SIGTRAP, as an
// unhandled DebugBreak ends it on Windows.  gdb is not measured.
//
// Not DebuggerBreak.h: GeneralsMD/.gitignore's [Dd]ebug* pattern would silently ignore that name.
//
// Only Debug-only code calls it today: the Release sweeps never compiled those branches, which is
// why nothing found DebugBreak until linux-check grew a Debug row.

#pragma once

#if defined(_WIN32)
#include <windows.h>
inline void breakIntoDebugger( void ) { DebugBreak(); }
#else
#include <signal.h>
inline void breakIntoDebugger( void ) { raise( SIGTRAP ); }
#endif
