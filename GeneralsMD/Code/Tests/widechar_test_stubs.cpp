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

// What B1's two tests (widechar_crc_gate and widechar_format_selfcheck) stub, so that they can be
// built from the REAL string, xfer and formatting sources without linking the rest of gameengine.
// Everything here is either the memory manager - malloc-backed, which decides where bytes live and
// never what they are - or something the tested paths never reach, which stops the program loudly
// if it ever is reached rather than letting a test pass over garbage.

#include "PreRTS.h"

#include "Common/AudioEventRTS.h"
#include "Common/CriticalSection.h"
#include "Common/GameMemory.h"
#include "Common/GameState.h"
#include "Common/KindOf.h"
#include "Common/Science.h"
#include "Common/Upgrade.h"

#include <stdio.h>
#include <stdlib.h>

// The memory manager, malloc-backed.  UnicodeString sizes and frees its buffer through it; it
// decides where the bytes live, never what they are.
static unsigned char s_allocatorStorage[ sizeof( DynamicMemoryAllocator ) ];
DynamicMemoryAllocator *TheDynamicMemoryAllocator = (DynamicMemoryAllocator *)s_allocatorStorage;
void *DynamicMemoryAllocator::allocateBytesDoNotZeroImplementation( Int numBytes DECLARE_LITERALSTRING_ARG2 )
{
	return malloc( numBytes );
}
void DynamicMemoryAllocator::freeBytes( void *pMem ) { free( pMem ); }
Int DynamicMemoryAllocator::getActualAllocationSize( Int numBytes ) { return numBytes; }

CriticalSection *TheUnicodeStringCriticalSection = NULL;	// ScopedCriticalSection skips a null one

// Named by Xfer's members for sciences, upgrades, map paths and kind-of masks.  The hashing path
// calls none of them; if one ever runs, the gate says so and stops rather than hashing garbage.
static void notOnThisPath( const char *what )
{
	printf( "FAIL: the gate reached %s, which it stubs on the understanding that it never runs\n", what );
	exit( 2 );
}
// The strings call it when TheDynamicMemoryAllocator is NULL, which the one above never is.
void preMainInitMemoryManager() { notOnThisPath( "preMainInitMemoryManager" ); }
GameState *TheGameState = NULL;
ScienceStore *TheScienceStore = NULL;
UpgradeCenter *TheUpgradeCenter = NULL;
AsciiString GameState::portableMapPathToRealMapPath( const AsciiString & ) const { notOnThisPath( "GameState" ); return AsciiString(); }
AsciiString GameState::realMapPathToPortableMapPath( const AsciiString & ) const { notOnThisPath( "GameState" ); return AsciiString(); }
AsciiString ScienceStore::getInternalNameForScience( ScienceType ) const { notOnThisPath( "ScienceStore" ); return AsciiString(); }
ScienceType ScienceStore::getScienceFromInternalName( const AsciiString & ) const { notOnThisPath( "ScienceStore" ); return SCIENCE_INVALID; }
const UpgradeTemplate *UpgradeCenter::findUpgrade( const AsciiString & ) const { notOnThisPath( "UpgradeCenter" ); return NULL; }
UpgradeTemplate *UpgradeCenter::firstUpgradeTemplate( void ) { notOnThisPath( "UpgradeCenter" ); return NULL; }
template<> const char *KindOfMaskType::s_bitNameList[] = { NULL };
// GCC emits inline destructors that clang does not - MessageStream.h's GameMessageArgument, which
// names the pool allocator, and one that names AudioEventRTS's.  Never on the hashing path either;
// their stubs are shared with fpucontrol_selfcheck, in Tests/gcc_eager_vtable_stubs.cpp.


// Debug builds only, each under the same macro as its declaration, so it exists exactly when a build
// can name it.  Neither is code under test.  The log goes to stderr instead of the engine's log file,
// because a Debug build logs on ordinary paths and a stub that stopped the program there would fail
// the test for a message, not for a wrong byte.  The leak bookkeeping has nothing to keep here.
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
#ifdef MEMORYPOOL_DEBUG
void DynamicMemoryAllocator::debugIgnoreLeaksForThisBlock( void * ) {}
#endif
