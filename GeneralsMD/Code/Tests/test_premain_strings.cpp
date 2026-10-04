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

/* Strings built before main, by a static constructor, before anything has started the memory
	 manager: the real AsciiString.cpp, UnicodeString.cpp and GameMemory.cpp.

	 The engine has such constructors - LogClass objects at file scope (WOLLobbyMenu.cpp,
	 WOLQuickMatchMenu.cpp, PeerThread.cpp) build an AsciiString - and on macOS the process died in
	 one with EXC_BAD_ACCESS: the strings allocate from TheDynamicMemoryAllocator directly, which is
	 NULL until something starts the memory manager, and on Windows that something is whichever static
	 constructor happens to call operator new first.  The strings now start it themselves, as operator
	 new does.  Here nothing else can have: this file's constructor is the only one that allocates.

	 Whichever string is built first starts the memory manager, so the test is built twice:
	 test_premain_strings builds the AsciiString first, test_premain_unicode (ZH_PREMAIN_UNICODE_FIRST)
	 the UnicodeString.  A crash is the test failing: ctest reports the signal. */

#include "PreRTS.h"

#include "Common/AsciiString.h"
#include "Common/CriticalSection.h"
#include "Common/ExecutableDirectory.h"
#include "Common/GameMemory.h"
#include "Common/JobSystem.h"
#include "Common/UnicodeString.h"

#include <stdio.h>
#include <string.h>

static int s_failed = 0;
#define CHECK( x ) do { if (!(x)) { printf( "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x ); ++s_failed; } } while (0)

static bool s_allocatorWasNull = false;

struct BuiltBeforeMain
{
	AsciiString ascii;
	UnicodeString unicode;
	BuiltBeforeMain()
	{
		s_allocatorWasNull = (TheDynamicMemoryAllocator == NULL);
#if defined(ZH_PREMAIN_UNICODE_FIRST)
		unicode = UnicodeString( u"before main" );
		ascii = "Perf.txt";
#else
		ascii = "Perf.txt";				// what LogClass's constructor builds, give or take the folder
		unicode = UnicodeString( u"before main" );
#endif
	}
};
static BuiltBeforeMain s_early;

int main( void )
{
	CHECK( s_allocatorWasNull );								// nothing had started it: the case under test
	CHECK( TheDynamicMemoryAllocator != NULL );			// the strings did
	CHECK( !isMemoryManagerOfficiallyInited() );		// as a pre-main start, not main's
	CHECK( strcmp( s_early.ascii.str(), "Perf.txt" ) == 0 );
	CHECK( s_early.unicode.compare( UnicodeString( u"before main" ) ) == 0 );

	// main's own start, as WinMain makes it, is then quietly taken as done, and strings go on working.
	initMemoryManager();
	CHECK( isMemoryManagerOfficiallyInited() );
	AsciiString later = s_early.ascii;
	later.concat( ".bak" );
	CHECK( strcmp( later.str(), "Perf.txt.bak" ) == 0 );
	CHECK( strcmp( s_early.ascii.str(), "Perf.txt" ) == 0 );

	printf( "%s (%d failed)\n", s_failed ? "FAILED" : "OK", s_failed );
	return s_failed ? 1 : 0;
}

// What GameMemory.cpp, MemoryInit.cpp and the strings name that this test does not build.
CriticalSection *TheUnicodeStringCriticalSection = NULL;	// ScopedCriticalSection skips a null one
CriticalSection *TheDmaCriticalSection = NULL;
CriticalSection *TheMemoryPoolCriticalSection = NULL;
Bool JobSystem::isWorkerThread() { return FALSE; }
void JobSystem::noteWorkerAllocation() {}
void getExecutableDirectory( char *buf, size_t size, Bool ) { if (size > 0) buf[0] = 0; }	// no pool-size override
#ifdef ALLOW_DEBUG_UTILS
DEBUG_EXTERN_C void DebugInit( int ) {}		// the pre-main start opens the debug log; this test has none
#endif
#ifdef DEBUG_LOGGING
#include <stdarg.h>
DEBUG_EXTERN_C void DebugLog( const char *format, ... )
{
	va_list args;
	va_start( args, format );
	vfprintf( stderr, format, args );
	va_end( args );
}
#endif
