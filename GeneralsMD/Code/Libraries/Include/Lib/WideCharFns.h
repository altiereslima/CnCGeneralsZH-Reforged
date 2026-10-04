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

// WideCharFns.h
// The engine's own string functions over WideChar.
// Added for the macOS port, B1.  See PORTING.md ("Wide characters").

#pragma once

#include <string>

#ifndef _WIDECHARFNS_H_
#define _WIDECHARFNS_H_

#include "Lib/WideCharFns.h"
#include "Lib/BaseType.h"
#include <stdarg.h>
#include <stdio.h>

/* The engine carries text in WideChar.  The C library has no string functions for it: the wcs*
	 family is wchar_t, which MSVC makes 2 bytes and clang makes 4, so calling wcslen on engine
	 text is a width bug that only shows up when the compiler changes.  These are the engine's
	 own, and they are correct at either width - which is why they can be written and reviewed
	 before the typedef moves rather than after.

	 They are deliberately NOT named wcs*, and deliberately not macros or overloads over those
	 names.  Common/Audio/urllaunch.cpp and simpleplayer.cpp call the real CRT wcsstr, wcscpy,
	 wcsncpy, wcslen, wcspbrk and swprintf on real LPWSTRs handed to them by the Windows shell and
	 by Windows Media, and they must keep doing exactly that.  A macro over wcslen would break
	 those two files from three directories away, which is the worst way to find out.

	 On Windows every function below that can forward to the CRT does, through a cast, so the
	 Windows build keeps the behaviour it has today.  char16_t and wchar_t are layout-compatible
	 there, so the cast is honest.  Off Windows these are the implementation.

	 Two exceptions, both marked at their declaration: WideCharICmp and WideCharNICmp fold ASCII
	 only, on every platform, and do not forward to _wcsicmp. */

//-----------------------------------------------------------------------------------------------
// Length, copy, concatenate
//-----------------------------------------------------------------------------------------------

/** A std::basic_string of WideChar, for code that wants the standard container's find/substr and a
	  c_str() to hand straight to UnicodeString.  std::wstring used to be that type, while WideChar
	  was wchar_t; it stopped being so when WideChar became char16_t (B1).  UnicodeString is still
	  the engine's string - this is for the few places that already used the standard one. */
typedef std::basic_string<WideChar> WideCharString;

size_t    WideCharLen  ( const WideChar *s );
WideChar *WideCharCpy  ( WideChar *dst, const WideChar *src );
WideChar *WideCharCat  ( WideChar *dst, const WideChar *src );

/** wcsncpy's semantics exactly, pad-with-zeros and all: at most n code units are copied, and if
	  src is shorter than n the remainder of dst is filled with zeros.  Twenty-three of the
	  twenty-four call sites this replaces are the fixed-buffer idiom
	  `WideCharNCpy(dst, src, N); dst[N] = 0;` on a wire or UI buffer, and they rely on it. */
WideChar *WideCharNCpy ( WideChar *dst, const WideChar *src, size_t n );

//-----------------------------------------------------------------------------------------------
// Compare
//-----------------------------------------------------------------------------------------------

Int WideCharCmp  ( const WideChar *a, const WideChar *b );
Int WideCharNCmp ( const WideChar *a, const WideChar *b, size_t n );

/** Case-insensitive compare, folding **ASCII A-Z only**, on every platform.

	  This is not _wcsicmp.  _wcsicmp folds by the current C locale on MSVC, which means the same
	  two strings can compare differently on two machines with two locales, and the engine reaches
	  this from UnicodeString::compareNoCase - which sorts the lobby's game list and matches UI
	  labels.  A port that quietly kept a locale-sensitive compare would be trading one silent
	  cross-machine disagreement for another.  Pinned here so it is the same answer everywhere.

	  Nothing calls these yet.  When UnicodeString::compareNoCase is moved onto them, that IS a
	  behaviour change on Windows for strings holding non-ASCII letters, and it needs its own row
	  in docs/porting/windows-impact.md at the time. */
Int WideCharICmp  ( const WideChar *a, const WideChar *b );
Int WideCharNICmp ( const WideChar *a, const WideChar *b, size_t n );

//-----------------------------------------------------------------------------------------------
// Search
//-----------------------------------------------------------------------------------------------

const WideChar *WideCharChr  ( const WideChar *s, WideChar c );
const WideChar *WideCharRChr ( const WideChar *s, WideChar c );
const WideChar *WideCharStr  ( const WideChar *haystack, const WideChar *needle );

/* size_t, not Int: UnicodeString::nextToken does pointer arithmetic with the result. */
size_t WideCharSpn  ( const WideChar *s, const WideChar *accept );
size_t WideCharCSpn ( const WideChar *s, const WideChar *reject );

//-----------------------------------------------------------------------------------------------
// Classification
//-----------------------------------------------------------------------------------------------

/* These take and return Int rather than WideChar because the CRT's isw* family is wint_t-based
	 and the call sites already rely on that - ControlBar.cpp casts the result of towupper back to
	 a WideChar by hand. */
Bool WideCharIsSpace ( Int c );
Bool WideCharIsDigit ( Int c );
Bool WideCharIsAlpha ( Int c );
Bool WideCharIsAlNum ( Int c );
Bool WideCharIsAscii ( Int c );
Int  WideCharToUpper ( Int c );

//-----------------------------------------------------------------------------------------------
// Formatting - the one funnel
//-----------------------------------------------------------------------------------------------

/* _vsnwprintf has no char16_t form on any platform and never will.  Every wide format in the
	 engine goes through here: UnicodeString::format_va (both overloads) and InGameUI's three
	 message() variants.  Do not add a second one.

	 **The contract is MSVC's `_vsnwprintf`, on both platforms, on purpose.**  At most `outCount`
	 code units are written, the terminator counted among them.  On success the return is the
	 number written not counting the terminator, and `out` is terminated.  **On truncation the
	 return is negative,** and, as MSVC does it (measured on Windows by W2's first Windows ctest),
	 all `outCount` units are written and none of them is a terminator: every call site passes one
	 less than its buffer and terminates the last unit itself, or throws.  All five call sites branch on that sign - two throw
	 ERROR_OUT_OF_MEMORY and three log and truncate - so a bare POSIX `vswprintf`, which returns
	 the would-be length instead, would silently turn a truncated chat line into a success on the
	 Mac and nobody would notice until a long player name reached the UI.

	 One deliberate difference from MSVC, in the single case where MSVC is already wrong: an
	 output of exactly `outCount` code units.  MSVC writes them, does NOT terminate, and returns
	 `outCount`; UnicodeString::format_va then calls set() on an unterminated buffer and reads
	 past its end.  Off Windows the same units are written and the return is negative, which is
	 what the call sites are written for: the sign alone differs, and it removes a buffer
	 overread.

	 The format's meaning is MSVC's LEGACY wide printf, on both platforms: %s, %ls and %ws are
	 WideChar strings and %c a WideChar; %S and %hs are narrow strings and %C/%hc a narrow char.
	 That is the opposite of C99's vswprintf for %s and %S, and it is what every wide format in
	 this tree was written against.  Off Windows the funnel therefore formats strings and
	 characters itself and hands the C library only the numeric conversions (WideCharFns.cpp
	 explains), so a WideChar* argument is read correctly at any width.  %n is refused.
	 Tests/test_widechar_format.cpp holds the expected output, and on Windows it checks the same
	 expectations against the real _vsnwprintf. */
Int WideCharFormatV ( WideChar *out, size_t outCount, const WideChar *format, va_list args );

/** The variadic spelling of WideCharFormatV, with the same contract. */
Int WideCharFormat ( WideChar *out, size_t outCount, const WideChar *format, ... );

/** swscanf over WideChar, which has no char16_t form either.  Two call sites, both parsing the
	  direct-connect IP address out of a combo box (NetworkDirectConnect.cpp). */
Int WideCharScan ( const WideChar *in, const WideChar *format, ... );

/** The replay header's strings (Recorder.cpp), as MSVC's fwprintf, fputwc and fgetwc wrote and read
	  them on a FILE* opened "wb"/"rb": in binary mode those convert nothing, so each code unit is its
	  two bytes, low byte first, and a string ends with a 0 unit.  These write and read the same bytes
	  with fputc and fgetc.

	  Why not the wide calls themselves: Recorder.cpp mixes them with fwrite and fread on one
	  FILE*.  That is undefined.  MSVC writes the units raw.  macOS's libc (measured) lets the calls
	  through but converts each unit to the locale's multibyte form - one byte for ASCII - so a Mac
	  replay would have read back on the Mac and nowhere else; a C library may also fail the calls
	  (C1, PR (g)).  The format does not change. */

/** What fgetwc returns at the end of a binary stream under MSVC: WEOF, which is 0xFFFF there.
	  Not EOF (-1) - so Recorder.cpp's `c == EOF` tests never fire on a truncated header, on
	  Windows or here (port defect 15). */
enum { WIDECHAR_FILE_EOF = 0xFFFF };

/** fputwc(c, f) on a binary stream: two bytes, low first.  Returns the unit written, or
	  WIDECHAR_FILE_EOF if either byte could not be. */
Int WideCharFilePut ( FILE *f, WideChar c );

/** fgetwc(f) on a binary stream: the next two bytes as a unit, low first, or WIDECHAR_FILE_EOF if
	  the file ends before both are read. */
Int WideCharFileGet ( FILE *f );

/** fwprintf(f, L"%ls", s) on a binary stream: every code unit of s, without its terminator.
	  Returns the number of units written, or a negative value on error. */
Int WideCharFileWrite ( FILE *f, const WideChar *s );

//-----------------------------------------------------------------------------------------------
// WideChar text in a narrow printf
//-----------------------------------------------------------------------------------------------

/* `DEBUG_LOG(("player %ls", name.str()))` tells the C library that the argument is a wchar_t*.
	 It is a WideChar*.  Those are the same thing under MSVC today and will still be the same size
	 there after the typedef moves, so this has always worked on Windows and always will - and it
	 is wrong on a Mac twice over.  Once for the width, and once because macOS's narrow printf has
	 to convert wide to multibyte through LC_CTYPE, which in the C locale a process starts in
	 fails with EILSEQ on anything outside ASCII.

	 So the argument is converted here instead, to UTF-8, by arithmetic on code units with no C
	 library conversion anywhere in it and therefore no locale behaviour to get wrong.  The format
	 then says %s, like any other string.

	 Do NOT reach for AsciiString::translate for this.  It is `concat((char)getCharAt(i))` with a
	 @todo above it admitting it only works for 7-bit ASCII: U+00C7 becomes byte 0xC7 and U+0130
	 becomes 0x30, the digit zero.  It would make the logs worse than the spelling it replaced.

	 One consequence worth knowing before reading a log: on Windows these bytes change.  %ls
	 converted through the process ANSI codepage, which rendered a name outside it as '?'; this
	 writes UTF-8.  DebugLogFile.txt is the file players send back, so that is a real change, not
	 a no-op - an improvement, but a change. */

/** `s` as UTF-8 in `out`, always terminated, truncated at a whole character if it does not fit.
	  Returns the number of bytes written, not counting the terminator.

	  A valid surrogate pair becomes the one character it encodes; an unpaired surrogate becomes
	  U+FFFD.  That is the log's encoder making its output valid UTF-8, not the engine acquiring a
	  text model - nothing round-trips through here. */
size_t WideCharToUtf8 ( const WideChar *s, char *out, size_t outBytes );

/** The other direction: UTF-8 bytes into `out` as WideChar, always terminated, truncated at a
	  whole character if it does not fit.  Returns the number of WideChar units written, not counting
	  the terminator.

	  At a 2-byte WideChar a character outside the BMP becomes a surrogate pair; at 4 bytes it is one
	  unit - the mirror of WideCharToUtf8.  Anything that is not well-formed UTF-8 (a stray
	  continuation byte, a truncated or overlong sequence, an encoded surrogate, a value above
	  U+10FFFF) becomes one U+FFFD per bad byte.  For text the platform hands back as UTF-8 - a
	  strftime in the user's LC_TIME - where mbstowcs would decode by LC_CTYPE, which stays "C". */
size_t WideCharFromUtf8 ( const char *in, WideChar *out, size_t outUnits );

/** The same conversion with storage attached, for use as a printf argument:

			DEBUG_LOG(( "player %s joined", WideCharAsUtf8( name.str() ).str() ));

	  The temporary lives to the end of the full expression, so it outlives the call, and each one
	  carries its own buffer - which matters, because several call sites print two wide strings in
	  one statement and a shared scratch buffer would have the second overwrite the first.

	  Deliberately a fixed buffer and not an allocation: this is reached from DEBUG_CRASH and from
	  the crash handler, where taking the allocator is how a diagnostic becomes a second crash, and
	  from CRCDEBUG_LOG inside the simulation loop.  Truncating a log line is the better failure. */
class WideCharAsUtf8
{
public:
	explicit WideCharAsUtf8( const WideChar *s ) { WideCharToUtf8( s, m_buffer, sizeof( m_buffer ) ); }
	const char *str( void ) const { return m_buffer; }

private:
	enum { BUFFER_BYTES = 1024 };		///< 1023 bytes of ASCII, or 341 of anything; DebugLog's own line buffer is 8192
	char m_buffer[ BUFFER_BYTES ];

	WideCharAsUtf8( const WideCharAsUtf8 & );
	WideCharAsUtf8 &operator=( const WideCharAsUtf8 & );
};

#endif // _WIDECHARFNS_H_
