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

// The Microsoft spellings this tree was written against, and the standard ones it is being moved
// to.  One header rather than an #ifdef at each of a few hundred call sites, per the port's rule
// (PORTING.md): a platform difference lives behind a named header with the reason in
// its comment.
//
// Which way round, and why.  The call sites are being renamed to the POSIX and C99 names -
// strcasecmp, snprintf, access - and this header teaches MSVC those names.  The other direction
// would have been less work now and wrong later: it would have left the Microsoft spellings spread
// across 70-odd files with a #define somewhere making them mean something else, which is the thing
// B3 set out not to do.  What is left here is small, and it shrinks as call sites move.
//
// Inline functions, not macros, wherever a function will do.  A macro named strcasecmp is a trap
// for whatever declares strcasecmp next; an inline function is overload-resolved and scoped like
// the code around it.
//
// Reached from always.h (all of WWVegas) and from Lib/BaseType.h (GameEngine and compression).
// PreRTS.h belongs to B5 and is deliberately not touched here.

// NOTE TO ANYONE RUNNING A BULK RENAME OVER THE TREE: exclude this file.  The MSVC wrappers below
// are the one place the Microsoft spellings have to survive, because they are what the standard
// names are implemented in terms of.  A sweep that rewrote _stricmp to strcasecmp in here turned
// strcasecmp into a call to itself - which compiles, and recurses forever, and only on Windows.

#pragma once

#ifndef MSVCCOMPAT_H
#define MSVCCOMPAT_H

// _UNIX switches on Westwood's abandoned 1990s UNIX port, about sixty sites across WWVegas, and the
// port's rules say never to define it (PORTING.md, "Never define _UNIX").  The vendored
// GameSpy SDK used to define it; Tools/vendor.sh renames the SDK's macro, and the ctest
// unix_define_check keeps any definition out of the tree and the compile flags.  This catches one
// that arrives before every WWVegas and engine file reaches this header anyway.
#if defined(_UNIX)
#error "_UNIX is defined. See PORTING.md, \"Never define _UNIX\"."
#endif

// ---------------------------------------------------------------------------
// Compiler keywords.
//
// On x64 MSVC __cdecl and __stdcall are already the one calling convention the ABI has, so these
// annotations do nothing there and are kept only so that the Windows compiler command line and
// the declarations it reads do not change.  Everywhere else they have to become nothing, because
// clang on arm64 does not have them at all.
// ---------------------------------------------------------------------------
#if !defined(_MSC_VER)

#ifndef __cdecl
#define __cdecl
#endif
#ifndef _cdecl
#define _cdecl
#endif
#ifndef __stdcall
#define __stdcall
#endif
#ifndef __fastcall
#define __fastcall
#endif

// MSVC's __forceinline is a stronger request than inline and warns when it cannot be honoured.
// always.h already funnels this through WWINLINE for its own use; this covers the direct uses.
#ifndef __forceinline
#define __forceinline inline __attribute__((always_inline))
#endif

// __declspec(dllexport|dllimport|align|noreturn|...) - only the storage-class spellings appear in
// this tree, and nothing here is built as a DLL on a Mac, so it becomes nothing.  If a future use
// needs align or noreturn, give it its own spelling rather than widening this.
#ifndef __declspec
#define __declspec(x)
#endif

#endif // !_MSC_VER

// ---------------------------------------------------------------------------
// The exception specification of the global operator delete.
//
// <new> declares `operator delete(void*)` and `operator delete[](void*)` noexcept, and has since
// C++11.  WWLib/always.h and Common/GameMemory.h redeclare both, and GameMemory.cpp replaces them,
// without it.  GCC rejects that mismatch ("has a different exception specifier"): on Linux it
// stopped 47 of the 49 failing GCC edges.  Clang accepts it.
//
// Empty under MSVC on purpose, so the Windows build sees exactly the declarations it always has.
// vcruntime_new.h declares them noexcept too, so MSVC would very likely accept the keyword, but
// nobody here can compile with MSVC to find out, and empty is the choice that is safe either way.
// Declarations and definitions both use it, so they agree with each other on every compiler.
// ---------------------------------------------------------------------------
#if defined(_MSC_VER)
#define WW_NOEXCEPT_DELETE
#else
#define WW_NOEXCEPT_DELETE noexcept
#endif

// ---------------------------------------------------------------------------
// The C runtime.
//
// Every name below is the standard one.  The MSVC branch is what makes the standard name work on
// a compiler whose library spells it with an underscore.
// ---------------------------------------------------------------------------
#if defined(_MSC_VER)

#include <string.h>
#include <stdio.h>
#include <io.h>
#include <direct.h>
#include <stdlib.h>

inline int strcasecmp(const char* a, const char* b) { return _stricmp(a, b); }
inline int strncasecmp(const char* a, const char* b, size_t n) { return _strnicmp(a, b, n); }

// access() and its mode bits.  MSVC has _access and does not define the POSIX names; F_OK and
// R_OK happen to have the same values in Microsoft's <io.h> documentation, but they are not named
// there, so they are named here.
#ifndef F_OK
#define F_OK 0
#endif
#ifndef R_OK
#define R_OK 4
#endif
#ifndef W_OK
#define W_OK 2
#endif
inline int access(const char* path, int mode) { return _access(path, mode); }

// mkdir: POSIX takes a mode, Windows has no use for one.  Callers pass a mode and it is dropped
// here, which is what the Windows build has always effectively done.
inline int mkdir(const char* path, int /* mode */) { return _mkdir(path); }

#else // !_MSC_VER

#include <strings.h>   // strcasecmp, strncasecmp
#include <unistd.h>    // access, F_OK, R_OK, W_OK
#include <sys/stat.h>  // mkdir
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <alloca.h>    // alloca, which Microsoft spells _alloca and declares in <malloc.h>
#include <ctype.h>     // tolower, toupper, for _strlwr and _strupr below

// _alloca: two call sites, chunkio.h's read macro and ini.cpp's line buffer.
#ifndef _alloca
#define _alloca alloca
#endif

// _strlwr and _strupr, and the unprefixed strlwr and strupr: MSVC's in-place case conversion, which
// neither POSIX nor C99 ever named, so unlike strcasecmp above there is no standard spelling to move
// the call sites to.  24 sites across WWSaveLoad, WW3D2, W3DDevice, WWAudio and Wwutil, nearly all of
// them lower-casing an asset or definition name into a hash key - so the answer has to be exactly
// the one MSVC gives, or a lookup built on one platform misses on the other.  MSVC's is
// locale-dependent; nothing in this tree or its vendored libraries calls setlocale, so both run in
// the "C" locale, where the conversion is ASCII only and bytes above 0x7F are left alone.  tolower
// and toupper on an unsigned char in the C locale are exactly that.  Each returns its argument, as
// MSVC's do (font3d.cpp assigns the result).  Not in the macOS SDK under any of the four names.
inline char* _strlwr(char* s) { for (char* p = s; *p; ++p) *p = (char)tolower((unsigned char)*p); return s; }
inline char* _strupr(char* s) { for (char* p = s; *p; ++p) *p = (char)toupper((unsigned char)*p); return s; }
inline char* strlwr(char* s) { return _strlwr(s); }
inline char* strupr(char* s) { return _strupr(s); }

// __max and __min: MSVC's <stdlib.h> macros, spelled here exactly as MSVC spells them, so each
// argument is evaluated exactly as often, and a NaN compares exactly as it does on Windows.
// Their nine callers are all simulation code (Player, PartitionManager, SpecialAbilityUpdate,
// ParticleUplinkCannonUpdate), where std::max would differ: std::max(NaN, x) is NaN, and
// __max(NaN, x) is x.
#ifndef __max
#define __max(a,b) (((a) > (b)) ? (a) : (b))
#endif
#ifndef __min
#define __min(a,b) (((a) < (b)) ? (a) : (b))
#endif

// _wcsnicmp: MSVC's in the "C" locale this tree never leaves - fold 'A'-'Z' only, compare the folded
// units, return their difference - stopping after n code units.  Its one caller is EarlyCommandLine.h,
// matching ASCII option names such as -logPrefix against the command line, which stays wchar_t by
// B1's decision.  Engine text (WideChar) compares through Lib/WideCharFns.h's WideCharICmp instead.
inline int _wcsnicmp(const wchar_t* a, const wchar_t* b, size_t n)
{
	for (; n != 0; --n) {
		wchar_t ca = *a++, cb = *b++;
		if (ca >= L'A' && ca <= L'Z') ca += L'a' - L'A';
		if (cb >= L'A' && cb <= L'Z') cb += L'a' - L'A';
		if (ca != cb || ca == 0) return (int)(ca - cb);
	}
	return 0;
}

// ---------------------------------------------------------------------------
// TCHAR.
//
// Microsoft's answer to "narrow or wide, decided at compile time", from <tchar.h>.  _UNICODE is not
// defined anywhere in this build and never has been - CMakeLists.txt does not set it and no source
// defines it - so every one of these is the narrow spelling, and that is all this needs to be.
//
// This is deliberately NOT the wide-character story.  B1 is moving WideChar to char16_t and B15 is
// removing the wide %ls; TCHAR is a separate and much smaller thing, being a Microsoft name for a
// decision this build already made.  If _UNICODE is ever defined, this block must not quietly follow
// it - it should fail to compile and be dealt with properly.
// ---------------------------------------------------------------------------
#if defined(_UNICODE) || defined(UNICODE)
#error "TCHAR here is narrow-only by assumption; _UNICODE needs B1's char16_t work, not this shim."
#endif

// WCHAR is deliberately absent, and must not become a compatibility alias here.  The only
// implementations behind it are Win32 APIs - StringClass::Copy_Wide is two calls to
// WideCharToMultiByte - so a typedef would make files compile and leave declared functions with no
// possible body off Windows, or invite a second UTF-16-to-narrow conversion beside the engine's own.
// Where WW3D2's text interface takes a WCHAR it is really taking engine text and becomes WideChar
// under B1; where wwstring.h and widestring.h take one they are Win32-only and have a platform
// guard.  See PORTING.md ("Wide characters").

typedef char TCHAR;

#ifndef _T
#define _T(x) x
#endif
#define _tcscmp   strcmp
#define _tcsicmp  strcasecmp
#define _tcsncmp  strncmp
#define _tcsnicmp strncasecmp
#define _tcslen   strlen
#define _tcsclen  strlen
#define _tcscpy   strcpy
#define _tcsncpy  strncpy
#define _tcscat   strcat
#define _tcschr   strchr
#define _tcsstr   strstr

#endif

// ---------------------------------------------------------------------------
// Limits.
//
// _MAX_PATH stays, and stays 260, and that is a deliberate refusal to tidy it.
//
// The obvious move is to rename its 117 uses to POSIX's PATH_MAX.  PATH_MAX is 1024 on Darwin, so
// that would silently change the size of every `char name[_MAX_PATH]` in the tree - and some of
// those are members of structures that go into save games and into the multiplayer INI checksum.
// A port whose entire premise is that both builds compute the same bytes cannot afford to move a
// struct boundary for tidiness.  260 on both, spelled the same on both, and no call site changes.
//
// _isnan and _finite are the same kind of thing one level down: Microsoft's spellings of what C99
// calls isnan and isfinite.  Nine files use them, and the shim is one line each against fifty-four
// renames that would each have to be read for whether the int-versus-bool return matters.
// ---------------------------------------------------------------------------
#if defined(_MSC_VER)

#include <stdlib.h>   // _MAX_PATH

#else

#include <math.h>

#ifndef _MAX_PATH
#define _MAX_PATH 260   // Windows' value, on purpose - see above.  Not PATH_MAX.
#endif
#ifndef MAX_PATH
#define MAX_PATH _MAX_PATH
#endif
// _splitpath's component limits, from the same <stdlib.h>: MSVC's values, 256 each, on purpose and
// for the same reason as _MAX_PATH above - a buffer sized by them is the same size on every platform.
// Two filename buffers in WW3D2's meshmdlio.cpp are.
#ifndef _MAX_FNAME
#define _MAX_FNAME 256
#endif
#ifndef _MAX_EXT
#define _MAX_EXT 256
#endif

// Microsoft returns int from both; C99's are macros over a bool-ish result.  The casts keep the
// `if (_isnan(x))` and `if (!_finite(x))` call sites reading exactly as they do on Windows.
#ifndef _isnan
#define _isnan(x)  ((int)::isnan(x))
#endif
#ifndef _finite
#define _finite(x) ((int)::isfinite(x))
#endif

#endif

#endif // MSVCCOMPAT_H
