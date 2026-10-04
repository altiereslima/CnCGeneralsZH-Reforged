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
 * The engine's Debug-build entry points, for the tests that link without the engine (W3, MSVC Debug).
 *
 * In a Debug build, WWVegas routes its asserts and logs to the engine's DebugCrash and DebugLog
 * (WWDebug/wwdebug.h), and its NEW to the file-and-line operator new that GameMemory.cpp defines.
 * GameMemory.cpp's own Debug code reads the debug flags, the stack dumper and the client's random
 * values.  A Release build compiles none of those calls, and neither does this file: outside _DEBUG
 * its object is empty.
 *
 * The target picks what it lacks:
 *   ZH_DEBUG_STUBS_CRASH       DebugCrash and TheCurrentIgnoreCrashPtr, for the tests that compile engine
 *                              sources with their asserts but no Debug.cpp
 *   ZH_DEBUG_STUBS_WWVEGAS     the WWVegas tests: those two, DebugLog, and the file-and-line operators
 *                              new and delete, onto the ordinary ones
 *   ZH_DEBUG_STUBS_GAMEMEMORY  the tests that build GameMemory.cpp themselves: the debug flags, the
 *                              stack dumper, GetGameClientRandomValue
 *   ZH_DEBUG_STUBS_GLOBALDATA  and TheWritableGlobalData, for the two that read it
 *   ZH_DEBUG_STUBS_ENGINE      the tests that link the engine without its device and WinMain: the
 *                              DUMP_PERF_STATS counters, the AI's debug icons, the stats display, the
 *                              mouse position and the instance handle
 *
 * An assertion that fires in a test is logged and the test goes on, as an unattended game does (Debug.cpp):
 * several tests reach an assertion on purpose (an unknown format, a missing file) and check what comes back,
 * which is what a Release build, with no assertions at all, does too.
 */
#if defined(_DEBUG)

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <new>

#if defined(ZH_DEBUG_STUBS_WWVEGAS)
#define ZH_DEBUG_STUBS_CRASH

extern "C" void DebugLog(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	fputs("  [DebugLog] ", stderr);
	vfprintf(stderr, format, args);
	va_end(args);
}

void *operator new(size_t size, const char *, int) { return ::operator new(size); }
void *operator new[](size_t size, const char *, int) { return ::operator new[](size); }
void operator delete(void *p, const char *, int) { ::operator delete(p); }
void operator delete[](void *p, const char *, int) { ::operator delete[](p); }

#endif

#if defined(ZH_DEBUG_STUBS_CRASH)

extern "C" {
char *TheCurrentIgnoreCrashPtr = NULL;

void DebugCrash(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	fputs("DEBUG CRASH (auto-ignored): ", stderr);
	vfprintf(stderr, format, args);
	fputs("\n", stderr);
	va_end(args);
	fflush(stderr);
}
}

#endif

#if defined(ZH_DEBUG_STUBS_GAMEMEMORY)

extern "C" int DebugGetFlags() { return 0; }
extern "C" void DebugSetFlags(int) {}

int GetGameClientRandomValue(int lo, int, char *, int) { return lo; }

void FillStackAddresses(void **addresses, unsigned int count, unsigned int)
{
	for (unsigned int i = 0; i < count; ++i)
		addresses[i] = NULL;
}

void StackDumpFromAddresses(void **, unsigned int, void (*)(const char *)) {}

#endif

#if defined(ZH_DEBUG_STUBS_ENGINE)
#include <stdint.h>
#include "Lib/BaseType.h"

int64_t Total_Create_Render_Obj_Time = 0;
int64_t Total_Get_HAnim_Time = 0;
int64_t Total_Get_Texture_Time = 0;
int64_t Total_Load_3D_Assets = 0;
struct HINSTANCE__;
HINSTANCE__ *ApplicationHInstance = NULL;
ICoord2D TheMousePos;
void addIcon(const Coord3D *, Real, Int, RGBColor) {}
class DebugDisplayInterface;
void StatDebugDisplay(DebugDisplayInterface *, void *, FILE *) {}
#endif

#if defined(ZH_DEBUG_STUBS_GLOBALDATA)
class GlobalData;
GlobalData *TheWritableGlobalData = NULL;
#endif

#endif
