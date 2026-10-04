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
 * test_wwdownload's stand-in for the engine's DebugLog (GameEngine/Source/Common/System/Debug.cpp).
 *
 * A Debug build of wwdownload compiles its DEBUG_LOG calls (FTP.cpp), and the test links wwdownload without
 * the engine, so the call needs a definition here.  A Release wwdownload compiles them out, and so does this
 * file: its object is empty there.
 */
#if defined(_DEBUG)

#include <stdarg.h>
#include <stdio.h>

extern "C" void DebugLog(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	fputs("  [DebugLog] ", stderr);
	vfprintf(stderr, format, args);
	va_end(args);
}

#endif
