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

// WideCharFns.cpp
// The engine's own string functions over WideChar.  See Lib/WideCharFns.h for why these exist
// and why they are not called wcs*.
// Added for the macOS port, B1.  See PORTING.md ("Wide characters").

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Lib/WideCharFns.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

#ifndef _WIN32
#include <locale.h>
#ifdef __APPLE__
#include <xlocale.h>			// where macOS declares newlocale/uselocale
#endif
#endif

/* Two shapes of implementation below, and the split is not the obvious one.

	 The functions that take a WideChar POINTER care about the width, so on Windows they forward to
	 the CRT through a cast - char16_t and wchar_t are layout-compatible there and the Windows build
	 keeps the code it has today - and off Windows they are written out.  The written-out versions
	 are correct at either width, which is what lets this file be reviewed before the typedef moves.

	 The functions that take a character VALUE do not care about the width at all: iswspace and
	 friends take a wint_t, which is at least 32 bits everywhere, so there was never a portability
	 problem with them.  They forward to the CRT on both platforms.  They are in this file only so
	 that Language.h's Game* macros have one place to point at. */

#ifdef _WIN32
#define AS_CRT(p)  (reinterpret_cast<const wchar_t *>(p))
#define AS_CRTW(p) (reinterpret_cast<wchar_t *>(p))
#endif

//-----------------------------------------------------------------------------------------------
// Length, copy, concatenate
//-----------------------------------------------------------------------------------------------

size_t WideCharLen( const WideChar *s )
{
#ifdef _WIN32
	return ::wcslen( AS_CRT( s ) );
#else
	const WideChar *p = s;
	while (*p)
		++p;
	return (size_t)(p - s);
#endif
}

WideChar *WideCharCpy( WideChar *dst, const WideChar *src )
{
#ifdef _WIN32
	::wcscpy( AS_CRTW( dst ), AS_CRT( src ) );
	return dst;
#else
	WideChar *out = dst;
	while ((*out++ = *src++) != 0)
		;
	return dst;
#endif
}

WideChar *WideCharCat( WideChar *dst, const WideChar *src )
{
#ifdef _WIN32
	::wcscat( AS_CRTW( dst ), AS_CRT( src ) );
	return dst;
#else
	WideChar *out = dst;
	while (*out)
		++out;
	while ((*out++ = *src++) != 0)
		;
	return dst;
#endif
}

WideChar *WideCharNCpy( WideChar *dst, const WideChar *src, size_t n )
{
#ifdef _WIN32
	::wcsncpy( AS_CRTW( dst ), AS_CRT( src ), n );
	return dst;
#else
	// wcsncpy's semantics, pad-with-zeros included - see the header for why that matters.
	size_t i = 0;
	while (i < n && src[i] != 0)
	{
		dst[i] = src[i];
		++i;
	}
	while (i < n)
		dst[i++] = 0;
	return dst;
#endif
}

//-----------------------------------------------------------------------------------------------
// Compare
//-----------------------------------------------------------------------------------------------

/* Three-way rather than a subtraction: at a 4-byte WideChar the difference of two code units does
	 not fit in an Int, and these have to be correct at either width. */
static inline Int compareUnits( WideChar a, WideChar b )
{
	if (a < b) return -1;
	if (a > b) return 1;
	return 0;
}

Int WideCharCmp( const WideChar *a, const WideChar *b )
{
#ifdef _WIN32
	return ::wcscmp( AS_CRT( a ), AS_CRT( b ) );
#else
	while (*a && *a == *b)
	{
		++a;
		++b;
	}
	return compareUnits( *a, *b );
#endif
}

Int WideCharNCmp( const WideChar *a, const WideChar *b, size_t n )
{
#ifdef _WIN32
	return ::wcsncmp( AS_CRT( a ), AS_CRT( b ), n );
#else
	while (n > 0 && *a && *a == *b)
	{
		++a;
		++b;
		--n;
	}
	if (n == 0)
		return 0;
	return compareUnits( *a, *b );
#endif
}

/* ASCII only, on every platform, and NOT _wcsicmp.  The header says why. */
static inline WideChar foldAscii( WideChar c )
{
	return (c >= 'A' && c <= 'Z') ? (WideChar)(c + ('a' - 'A')) : c;
}

Int WideCharICmp( const WideChar *a, const WideChar *b )
{
	for (;;)
	{
		const WideChar ca = foldAscii( *a++ );
		const WideChar cb = foldAscii( *b++ );
		if (ca != cb)
			return compareUnits( ca, cb );
		if (ca == 0)
			return 0;
	}
}

Int WideCharNICmp( const WideChar *a, const WideChar *b, size_t n )
{
	while (n-- > 0)
	{
		const WideChar ca = foldAscii( *a++ );
		const WideChar cb = foldAscii( *b++ );
		if (ca != cb)
			return compareUnits( ca, cb );
		if (ca == 0)
			return 0;
	}
	return 0;
}

//-----------------------------------------------------------------------------------------------
// Search
//-----------------------------------------------------------------------------------------------

const WideChar *WideCharChr( const WideChar *s, WideChar c )
{
#ifdef _WIN32
	return reinterpret_cast<const WideChar *>( ::wcschr( AS_CRT( s ), (wchar_t)c ) );
#else
	for (;; ++s)
	{
		if (*s == c)
			return s;			// finding the terminator is wcschr's documented behaviour, not an accident
		if (*s == 0)
			return NULL;
	}
#endif
}

const WideChar *WideCharRChr( const WideChar *s, WideChar c )
{
#ifdef _WIN32
	return reinterpret_cast<const WideChar *>( ::wcsrchr( AS_CRT( s ), (wchar_t)c ) );
#else
	const WideChar *found = NULL;
	for (;; ++s)
	{
		if (*s == c)
			found = s;
		if (*s == 0)
			return found;
	}
#endif
}

const WideChar *WideCharStr( const WideChar *haystack, const WideChar *needle )
{
#ifdef _WIN32
	return reinterpret_cast<const WideChar *>( ::wcsstr( AS_CRT( haystack ), AS_CRT( needle ) ) );
#else
	if (*needle == 0)
		return haystack;

	for (; *haystack; ++haystack)
	{
		const WideChar *h = haystack;
		const WideChar *n = needle;
		while (*n && *h == *n)
		{
			++h;
			++n;
		}
		if (*n == 0)
			return haystack;
	}
	return NULL;
#endif
}

size_t WideCharSpn( const WideChar *s, const WideChar *accept )
{
#ifdef _WIN32
	return ::wcsspn( AS_CRT( s ), AS_CRT( accept ) );
#else
	// *p is never the terminator inside the loop, so WideCharChr's match-the-terminator behaviour
	// cannot fire here.
	const WideChar *p = s;
	while (*p && WideCharChr( accept, *p ) != NULL)
		++p;
	return (size_t)(p - s);
#endif
}

size_t WideCharCSpn( const WideChar *s, const WideChar *reject )
{
#ifdef _WIN32
	return ::wcscspn( AS_CRT( s ), AS_CRT( reject ) );
#else
	const WideChar *p = s;
	while (*p && WideCharChr( reject, *p ) == NULL)
		++p;
	return (size_t)(p - s);
#endif
}

//-----------------------------------------------------------------------------------------------
// Classification - width-independent, forwarded on both platforms
//-----------------------------------------------------------------------------------------------

Bool WideCharIsSpace( Int c ) { return ::iswspace( (wint_t)c ) != 0; }
Bool WideCharIsDigit( Int c ) { return ::iswdigit( (wint_t)c ) != 0; }
Bool WideCharIsAlpha( Int c ) { return ::iswalpha( (wint_t)c ) != 0; }
Bool WideCharIsAlNum( Int c ) { return ::iswalnum( (wint_t)c ) != 0; }
Int  WideCharToUpper( Int c ) { return (Int)::towupper( (wint_t)c ); }

/* Spelled out rather than forwarded to iswascii, which is a BSD/Win32 extension and is not in
	 the standard headers everywhere.  This is its definition. */
Bool WideCharIsAscii( Int c ) { return c >= 0 && c < 128; }

//-----------------------------------------------------------------------------------------------
// Formatting
//-----------------------------------------------------------------------------------------------

#ifndef _WIN32

/* Off Windows the funnel widens to wchar_t, calls the C library, and narrows back.  The two
	 scratch buffers are the whole cost of the thing, and they are why this is one function and not
	 five call sites. */
static const size_t WIDE_SCRATCH = 2048;		// UnicodeString::MAX_FORMAT_BUF_LEN

/* ...and this is the part that is not obvious, and that was measured rather than reasoned about.

	 A process starts in the "C" locale.  In that locale macOS's vswprintf, vswscanf and fwprintf
	 **fail outright, returning -1 with errno EILSEQ, on any wide character outside ASCII** - they
	 have to be able to convert between the wide and multibyte forms and the C locale's multibyte
	 form is ASCII.  MSVC's _vsnwprintf does no such conversion and passes anything through.

	 So a funnel that just called vswprintf would have every UnicodeString::format of a Turkish,
	 German or French string return negative, which UnicodeString::format_va turns into a thrown
	 ERROR_OUT_OF_MEMORY.  Every localisation but English would abort on the first formatted
	 message, on the Mac only, for a reason nothing in the log would name.

	 uselocale() is POSIX-2008, sets LC_CTYPE for the calling thread only, and leaves the global
	 locale and every other thread alone - which matters, because the engine formats text from
	 more than one thread and this must not become a global setting the rest of the game can see.
	 The locale object is created once and never freed: it lives as long as the process, and
	 freeing it at exit would only introduce a teardown order to get wrong. */

static locale_t utf8CtypeLocale( void )
{
	// C++11 magic statics: initialised once, thread-safely, on first use.
	static locale_t loc = []() -> locale_t
	{
		static const char *const names[] = { "en_US.UTF-8", "UTF-8", "C.UTF-8" };
		for (size_t i = 0; i < sizeof( names ) / sizeof( names[0] ); ++i)
		{
			locale_t l = ::newlocale( LC_CTYPE_MASK, names[i], (locale_t)0 );
			if (l != (locale_t)0)
				return l;
		}
		return (locale_t)0;
	}();
	return loc;
}

/** LC_CTYPE is UTF-8 for this thread for the lifetime of the object, and exactly as it was after
	  it.  A scope guard rather than a pair of calls because WideCharFormatV has an early return. */
class ScopedUtf8Ctype
{
public:
	ScopedUtf8Ctype() : m_previous( (locale_t)0 )
	{
		const locale_t utf8 = utf8CtypeLocale();
		if (utf8 != (locale_t)0)
			m_previous = ::uselocale( utf8 );
	}
	~ScopedUtf8Ctype()
	{
		if (m_previous != (locale_t)0)
			::uselocale( m_previous );
	}
private:
	locale_t m_previous;
	ScopedUtf8Ctype( const ScopedUtf8Ctype & );
	ScopedUtf8Ctype &operator=( const ScopedUtf8Ctype & );
};

static size_t widenToWchar( const WideChar *in, wchar_t *out, size_t outCount )
{
	size_t i = 0;
	while (in[i] != 0 && i + 1 < outCount)
	{
		out[i] = (wchar_t)in[i];
		++i;
	}
	out[i] = 0;
	return i;
}

static size_t narrowToWideChar( const wchar_t *in, WideChar *out, size_t outCount )
{
	size_t i = 0;
	while (in[i] != 0 && i + 1 < outCount)
	{
		// Anything outside the BMP cannot be carried by a 16-bit code unit.  B1 changes a width,
		// not a text model (by design), and nothing in the engine's format
		// strings or string tables is outside the BMP - so substitute rather than invent a
		// surrogate pair, and make it visible if it ever happens.
		const unsigned long cp = (unsigned long)in[i];
		out[i] = (WideChar)(cp > 0xFFFFul ? 0xFFFDul : cp);
		++i;
	}
	out[i] = 0;
	return i;
}

/* The funnel's pieces.  See WideCharFormatV for why the POSIX side formats rather than forwards. */

/** Collects the output under the header's contract: at most outCount units, the terminator counted
	  among them; on overflow, MSVC's fill - all outCount units, no terminator - and a negative return
	  (measured on Windows, W2). */
class WideCharFormatSink
{
public:
	WideCharFormatSink( WideChar *out, size_t outCount ) : m_out( out ), m_count( outCount ), m_length( 0 ) {}
	void put( WideChar c )
	{
		if (m_length < m_count)
			m_out[ m_length ] = c;
		++m_length;
	}
	Int finish( void )
	{
		if (m_length >= m_count)
			return -1;		// truncated, or exactly full: every unit written, none of them a terminator
		m_out[ m_length ] = 0;
		return (Int)m_length;
	}
private:
	WideChar *m_out;
	size_t m_count;
	size_t m_length;
};

enum FormatLength
{
	LENGTH_NONE, LENGTH_HH, LENGTH_H, LENGTH_L, LENGTH_LL, LENGTH_BIG_L, LENGTH_J, LENGTH_Z, LENGTH_T, LENGTH_W
};

struct FormatSpec
{
	FormatSpec() : left( false ), zero( false ), width( -1 ), precision( -1 ), length( LENGTH_NONE ), flagCount( 0 )
	{
		flags[0] = 0;
	}
	bool left;
	bool zero;
	int width;
	int precision;
	FormatLength length;
	size_t flagCount;
	char flags[ 8 ];
};

/** units, padded to the width - on the right for '-', otherwise on the left, with spaces.  (A '0'
	  flag on a string or character is undefined in C; MSVC pads such a field with zeros, and so does
	  this, since that is the output the Windows build gives.) */
static void formatUnits( WideCharFormatSink &sink, const FormatSpec &spec, const WideChar *units, size_t count )
{
	const size_t width = spec.width < 0 ? 0 : (size_t)spec.width;
	const size_t pad = width > count ? width - count : 0;
	const WideChar padUnit = (spec.zero && !spec.left) ? (WideChar)'0' : (WideChar)' ';
	if (!spec.left)
		for (size_t i = 0; i < pad; ++i) sink.put( padUnit );
	for (size_t i = 0; i < count; ++i)
		sink.put( units[i] );
	if (spec.left)
		for (size_t i = 0; i < pad; ++i) sink.put( (WideChar)' ' );
}

static const char NULL_STRING_TEXT[] = "(null)";		// what MSVC prints for a null %s or %S

static void formatWideString( WideCharFormatSink &sink, const FormatSpec &spec, const WideChar *s )
{
	if (s == NULL)
	{
		const FormatSpec copy = spec;
		WideChar text[ sizeof( NULL_STRING_TEXT ) ];
		size_t n = 0;
		for (; NULL_STRING_TEXT[n] != 0; ++n) text[n] = (WideChar)NULL_STRING_TEXT[n];
		formatUnits( sink, copy, text, spec.precision >= 0 && (size_t)spec.precision < n ? (size_t)spec.precision : n );
		return;
	}
	size_t count = 0;
	while (s[count] != 0 && (spec.precision < 0 || count < (size_t)spec.precision))
		++count;
	formatUnits( sink, spec, s, count );
}

static void formatNarrowString( WideCharFormatSink &sink, const FormatSpec &spec, const char *s )
{
	if (s == NULL)
		s = NULL_STRING_TEXT;
	size_t count = 0;
	while (s[count] != 0 && (spec.precision < 0 || count < (size_t)spec.precision))
		++count;
	// Each byte becomes the code unit of the same value, as MSVC's "C" locale converts it.
	WideChar stackUnits[ 256 ];
	WideChar *units = count <= 256 ? stackUnits : (WideChar *)malloc( count * sizeof( WideChar ) );
	if (units == NULL)
		return;
	for (size_t i = 0; i < count; ++i)
		units[i] = (WideChar)(unsigned char)s[i];
	formatUnits( sink, spec, units, count );
	if (units != stackUnits)
		free( units );
}

/** MSVC prints a pointer as sizeof(void*)*2 upper-case hex digits and no "0x"; the C library's %p
	  would say "0x..." instead. */
static void formatPointer( WideCharFormatSink &sink, const FormatSpec &spec, void *pointer )
{
	unsigned long long value = (unsigned long long)(uintptr_t)pointer;
	WideChar digits[ sizeof( void * ) * 2 ];
	for (size_t i = sizeof( digits ) / sizeof( digits[0] ); i-- > 0; value >>= 4)
		digits[i] = (WideChar)"0123456789ABCDEF"[ value & 0xF ];
	formatUnits( sink, spec, digits, sizeof( digits ) / sizeof( digits[0] ) );
}

/** One numeric conversion through the C library, then narrowed into the sink.  The sub-format is
	  rebuilt in C99's spelling (MSVC's I64 becomes ll), with any '*' width and precision already
	  taken from the arguments and written in as digits. */
static bool formatDelegated( WideCharFormatSink &sink, const FormatSpec &spec, WideChar conversion, va_list &args )
{
	wchar_t sub[ 48 ];
	size_t n = 0;
	sub[n++] = L'%';
	for (size_t i = 0; i < spec.flagCount; ++i)
		sub[n++] = (wchar_t)spec.flags[i];
	char number[ 16 ];
	if (spec.width >= 0)
	{
		snprintf( number, sizeof( number ), "%d", spec.width );
		for (size_t i = 0; number[i] != 0; ++i) sub[n++] = (wchar_t)number[i];
	}
	if (spec.precision >= 0)
	{
		sub[n++] = L'.';
		snprintf( number, sizeof( number ), "%d", spec.precision );
		for (size_t i = 0; number[i] != 0; ++i) sub[n++] = (wchar_t)number[i];
	}
	const bool isFloat = conversion == 'e' || conversion == 'E' || conversion == 'f' || conversion == 'F'
		|| conversion == 'g' || conversion == 'G' || conversion == 'a' || conversion == 'A';
	const bool isSigned = conversion == 'd' || conversion == 'i';
	switch (isFloat ? LENGTH_NONE : spec.length)
	{
		case LENGTH_HH: sub[n++] = L'h'; sub[n++] = L'h'; break;
		case LENGTH_H:  sub[n++] = L'h'; break;
		case LENGTH_L:  sub[n++] = L'l'; break;
		case LENGTH_LL: sub[n++] = L'l'; sub[n++] = L'l'; break;
		case LENGTH_J:  sub[n++] = L'j'; break;
		case LENGTH_Z:  sub[n++] = L'z'; break;
		case LENGTH_T:  sub[n++] = L't'; break;
		default: break;
	}
	if (isFloat && spec.length == LENGTH_BIG_L)
		sub[n++] = L'L';
	sub[n++] = (wchar_t)conversion;
	sub[n] = 0;

	wchar_t text[ 512 ];
	int written;
	if (isFloat)
	{
		if (spec.length == LENGTH_BIG_L)
			written = swprintf( text, 512, sub, va_arg( args, long double ) );
		else
			written = swprintf( text, 512, sub, va_arg( args, double ) );
	}
	else
	{
		switch (spec.length)
		{
			case LENGTH_L:
				written = isSigned ? swprintf( text, 512, sub, va_arg( args, long ) )
					: swprintf( text, 512, sub, va_arg( args, unsigned long ) );
				break;
			case LENGTH_LL:
				written = isSigned ? swprintf( text, 512, sub, va_arg( args, long long ) )
					: swprintf( text, 512, sub, va_arg( args, unsigned long long ) );
				break;
			case LENGTH_J:
				written = isSigned ? swprintf( text, 512, sub, va_arg( args, intmax_t ) )
					: swprintf( text, 512, sub, va_arg( args, uintmax_t ) );
				break;
			case LENGTH_Z:
			case LENGTH_T:
				written = isSigned ? swprintf( text, 512, sub, va_arg( args, ptrdiff_t ) )
					: swprintf( text, 512, sub, va_arg( args, size_t ) );
				break;
			default:		// hh, h and none all arrive promoted to int
				written = isSigned ? swprintf( text, 512, sub, va_arg( args, int ) )
					: swprintf( text, 512, sub, va_arg( args, unsigned int ) );
				break;
		}
	}
	if (written < 0)
		return false;
	// Narrowed explicitly.  Digits only, so every unit is ASCII - see WideCharFormatV.
	for (int i = 0; i < written; ++i)
		sink.put( (WideChar)text[i] );
	return true;
}

#endif // !_WIN32

#ifndef _WIN32

/* WideCharFormatV's body off Windows, unchanged but for being its own function: it takes the argument
	 list by reference so that formatDelegated can take it on, and a va_list PARAMETER cannot be bound
	 to va_list& everywhere.  Where va_list is an array type (x86-64 System V: Linux and Intel Macs), a
	 parameter declared va_list is a pointer to its first element, which a va_list& will not accept, and
	 the x86_64 build did not compile.  WideCharFormatV hands this a va_copy of its argument instead,
	 which is a va_list object on every ABI. */
static Int formatWideV( WideChar *out, size_t outCount, const WideChar *format, va_list &args )
{

	/* Off Windows this is a formatter, not a forwarder.

		 Every wide format in this tree was written against MSVC's LEGACY wide-format meanings - there
		 is no _CRT_STDIO_ISO_WIDE_SPECIFIERS anywhere - and C99's vswprintf reads half of them the
		 other way round:

		                     MSVC wide format            C99 vswprintf
		     %s  %ls %ws     a wide string               %s: a NARROW char*
		     %S  %hs         a narrow string             %S: a WIDE wchar_t*
		     %c  %lc         a wide character            %c: a narrow char
		     %C  %hc         a narrow character          %C: a wide character

		 So `message(UnicodeString(L"Now playing: %s"), name.str())` - thirty-nine formats written
		 like that - printed garbage here, width or no width.  And once WideChar is char16_t no C
		 library function can take the argument at all.  So this does the strings and characters
		 itself, with MSVC's meanings, and hands the C library only the conversions whose output is
		 digits: d i o u x X e E f F g G a A.  Those are delegated one at a time and narrowed back
		 explicitly.  In the "C" LC_NUMERIC the game keeps (it sets LC_TIME and nothing else), their
		 output is ASCII by construction - digits, sign, radix point, hex letters, "inf", "nan" - so
		 **every non-ASCII code unit in the result comes from the format's own text or from %s, %S,
		 %c or %C, all of which are copied here unit by unit.**  Nothing is converted through the C
		 library's multibyte machinery, so there is no locale behaviour left to get wrong.

		 A narrow string or character becomes wide the way MSVC's "C" locale does it: each byte is the
		 code unit of the same value.  %p prints MSVC's form, not the C library's.  %n is refused: a
		 format that writes through a pointer has no business in a UI string, and UCRT refuses it too.
		 The contract - truncation, the terminator, the return value - is the header's. */
	WideCharFormatSink sink( out, outCount );
	const WideChar *p = format;
	while (*p != 0)
	{
		if (*p != (WideChar)'%')
		{
			sink.put( *p++ );
			continue;
		}
		++p;
		if (*p == (WideChar)'%')
		{
			sink.put( (WideChar)'%' );
			++p;
			continue;
		}

		// flags
		FormatSpec spec;
		for (;;)
		{
			const WideChar f = *p;
			if (f == '-') spec.left = true;
			else if (f == '0') spec.zero = true;
			else if (f != '+' && f != ' ' && f != '#') break;
			if (spec.flagCount < sizeof( spec.flags ) - 1)
				spec.flags[ spec.flagCount++ ] = (char)f;
			++p;
		}
		// width
		if (*p == '*')
		{
			spec.width = va_arg( args, int );
			if (spec.width < 0) { spec.left = true; spec.width = -spec.width; }
			++p;
		}
		else
		{
			while (*p >= '0' && *p <= '9')
				spec.width = (spec.width < 0 ? 0 : spec.width) * 10 + (int)(*p++ - '0');
		}
		// precision
		if (*p == '.')
		{
			++p;
			spec.precision = 0;
			if (*p == '*')
			{
				spec.precision = va_arg( args, int );
				if (spec.precision < 0) spec.precision = -1;
				++p;
			}
			else
			{
				while (*p >= '0' && *p <= '9')
					spec.precision = spec.precision * 10 + (int)(*p++ - '0');
			}
		}
		// length, MSVC's extensions included
		if (p[0] == 'h' && p[1] == 'h')      { spec.length = LENGTH_HH; p += 2; }
		else if (p[0] == 'h')                { spec.length = LENGTH_H; p += 1; }
		else if (p[0] == 'l' && p[1] == 'l') { spec.length = LENGTH_LL; p += 2; }
		else if (p[0] == 'l')                { spec.length = LENGTH_L; p += 1; }
		else if (p[0] == 'L')                { spec.length = LENGTH_BIG_L; p += 1; }
		else if (p[0] == 'j')                { spec.length = LENGTH_J; p += 1; }
		else if (p[0] == 'z')                { spec.length = LENGTH_Z; p += 1; }
		else if (p[0] == 't')                { spec.length = LENGTH_T; p += 1; }
		else if (p[0] == 'w')                { spec.length = LENGTH_W; p += 1; }
		else if (p[0] == 'I' && p[1] == '6' && p[2] == '4') { spec.length = LENGTH_LL; p += 3; }
		else if (p[0] == 'I' && p[1] == '3' && p[2] == '2') { spec.length = LENGTH_NONE; p += 3; }
		else if (p[0] == 'I')                { spec.length = LENGTH_Z; p += 1; }

		const WideChar conversion = *p;
		if (conversion == 0)
			return -1;						// a format that ends inside a conversion
		++p;

		switch (conversion)
		{
			case 's':
			case 'S':
			{
				// %s is wide unless h; %S is narrow unless l or w.
				const bool wide = conversion == 's' ? spec.length != LENGTH_H
					: (spec.length == LENGTH_L || spec.length == LENGTH_W);
				if (wide)
					formatWideString( sink, spec, va_arg( args, const WideChar * ) );
				else
					formatNarrowString( sink, spec, va_arg( args, const char * ) );
				break;
			}
			case 'c':
			case 'C':
			{
				const bool wide = conversion == 'c' ? spec.length != LENGTH_H
					: (spec.length == LENGTH_L || spec.length == LENGTH_W);
				const int value = va_arg( args, int );
				const WideChar unit = wide ? (WideChar)value : (WideChar)(unsigned char)value;
				formatUnits( sink, spec, &unit, 1 );
				break;
			}
			case 'n':
				return -1;
			case 'p':
				formatPointer( sink, spec, va_arg( args, void * ) );
				break;
			case 'd': case 'i': case 'o': case 'u': case 'x': case 'X':
			case 'e': case 'E': case 'f': case 'F': case 'g': case 'G': case 'a': case 'A':
				if (!formatDelegated( sink, spec, conversion, args ))
					return -1;
				break;
			default:
				return -1;						// not a conversion MSVC knows either
		}
	}
	return sink.finish();
}

/** Owns a va_copy for the length of a call, so every return path ends it. */
struct VaListCopy
{
	va_list ap;
	explicit VaListCopy( va_list src ) { va_copy( ap, src ); }
	~VaListCopy() { va_end( ap ); }
};

#endif // !_WIN32

Int WideCharFormatV( WideChar *out, size_t outCount, const WideChar *format, va_list args )
{
	if (out == NULL || outCount == 0 || format == NULL)
		return -1;

#ifdef _WIN32

	// Byte for byte the call this replaces.
	return (Int)::_vsnwprintf( AS_CRTW( out ), outCount, AS_CRT( format ), args );

#else

	VaListCopy copy( args );
	return formatWideV( out, outCount, format, copy.ap );

#endif
}

Int WideCharFormat( WideChar *out, size_t outCount, const WideChar *format, ... )
{
	va_list args;
	va_start( args, format );
	const Int result = WideCharFormatV( out, outCount, format, args );
	va_end( args );
	return result;
}

Int WideCharScan( const WideChar *in, const WideChar *format, ... )
{
	if (in == NULL || format == NULL)
		return -1;

	va_list args;
	va_start( args, format );

#ifdef _WIN32

	const Int result = (Int)::vswscanf( AS_CRT( in ), AS_CRT( format ), args );

#else

	const ScopedUtf8Ctype utf8;		// see WideCharFormatV

	wchar_t wideIn[ WIDE_SCRATCH ];
	wchar_t wideFormat[ WIDE_SCRATCH ];
	widenToWchar( in, wideIn, WIDE_SCRATCH );
	widenToWchar( format, wideFormat, WIDE_SCRATCH );

	const Int result = (Int)::vswscanf( wideIn, wideFormat, args );

#endif

	va_end( args );
	return result;
}

//-----------------------------------------------------------------------------------------------
// WideChar text in a narrow printf
//-----------------------------------------------------------------------------------------------

size_t WideCharToUtf8( const WideChar *s, char *out, size_t outBytes )
{
	if (out == NULL || outBytes == 0)
		return 0;

	size_t written = 0;
	if (s == NULL)
	{
		out[0] = 0;
		return 0;
	}

	while (*s != 0)
	{
		/* Read one code point.  At a 2-byte WideChar a character outside the BMP arrives as a
			 surrogate pair and has to be put back together, or the output is CESU-8 rather than UTF-8
			 and a text editor shows two replacement characters.  At a 4-byte WideChar there are no
			 surrogates and the first branch never fires.  Both widths have to work: this file is
			 written to be correct before and after the typedef moves. */
		unsigned long cp = (unsigned long)*s++;

		if (cp >= 0xD800ul && cp <= 0xDBFFul)				// high surrogate
		{
			const unsigned long low = (unsigned long)*s;
			if (low >= 0xDC00ul && low <= 0xDFFFul)
			{
				cp = 0x10000ul + ((cp - 0xD800ul) << 10) + (low - 0xDC00ul);
				++s;
			}
			else
			{
				cp = 0xFFFDul;										// a high surrogate with nothing after it
			}
		}
		else if (cp >= 0xDC00ul && cp <= 0xDFFFul)	// a low surrogate on its own
		{
			cp = 0xFFFDul;
		}

		// how many bytes this character needs, and stop on a whole character rather than half of one
		size_t need;
		if (cp < 0x80ul)					need = 1;
		else if (cp < 0x800ul)		need = 2;
		else if (cp < 0x10000ul)	need = 3;
		else											need = 4;

		if (written + need + 1 > outBytes)
			break;

		switch (need)
		{
			case 1:
				out[written++] = (char)cp;
				break;
			case 2:
				out[written++] = (char)(0xC0ul | (cp >> 6));
				out[written++] = (char)(0x80ul | (cp & 0x3Ful));
				break;
			case 3:
				out[written++] = (char)(0xE0ul | (cp >> 12));
				out[written++] = (char)(0x80ul | ((cp >> 6) & 0x3Ful));
				out[written++] = (char)(0x80ul | (cp & 0x3Ful));
				break;
			default:
				out[written++] = (char)(0xF0ul | (cp >> 18));
				out[written++] = (char)(0x80ul | ((cp >> 12) & 0x3Ful));
				out[written++] = (char)(0x80ul | ((cp >> 6) & 0x3Ful));
				out[written++] = (char)(0x80ul | (cp & 0x3Ful));
				break;
		}
	}

	out[written] = 0;
	return written;
}

size_t WideCharFromUtf8( const char *in, WideChar *out, size_t outUnits )
{
	if (out == NULL || outUnits == 0)
		return 0;

	size_t written = 0;
	const unsigned char *p = (const unsigned char *)in;
	while (p != NULL && *p != 0)
	{
		// Read one code point, or U+FFFD for one bad byte.
		unsigned long cp = 0xFFFDul;
		size_t length = 1;
		const unsigned char lead = p[0];
		if (lead < 0x80)
		{
			cp = lead;
		}
		else
		{
			size_t follow = 0;
			unsigned long value = 0, least = 0;
			if ((lead & 0xE0) == 0xC0)			{ follow = 1; value = lead & 0x1F; least = 0x80ul; }
			else if ((lead & 0xF0) == 0xE0)	{ follow = 2; value = lead & 0x0F; least = 0x800ul; }
			else if ((lead & 0xF8) == 0xF0)	{ follow = 3; value = lead & 0x07; least = 0x10000ul; }
			size_t k = 1;
			for (; follow != 0 && k <= follow && (p[k] & 0xC0) == 0x80; ++k)
				value = (value << 6) | (p[k] & 0x3F);
			if (follow != 0 && k == follow + 1 && value >= least && value <= 0x10FFFFul
				&& !(value >= 0xD800ul && value <= 0xDFFFul))
			{
				cp = value;
				length = follow + 1;
			}
		}

		// Write it, and stop on a whole character rather than half of one.
		const size_t need = (sizeof(WideChar) == 2 && cp >= 0x10000ul) ? 2 : 1;
		if (written + need >= outUnits)
			break;
		if (need == 2)
		{
			out[written++] = (WideChar)(0xD800ul + ((cp - 0x10000ul) >> 10));
			out[written++] = (WideChar)(0xDC00ul + ((cp - 0x10000ul) & 0x3FFul));
		}
		else
		{
			out[written++] = (WideChar)cp;
		}
		p += length;
	}
	out[written] = 0;
	return written;
}

// The replay header's strings, as MSVC's wide stream calls wrote and read them on the "wb"/"rb"
// FILE* Recorder.cpp uses: in binary mode they convert nothing, so each code unit is its two bytes,
// low byte first.  Written here with fputc and read with fgetc, which are byte calls, so the stream
// can carry them between its fwrite and fread calls on every C library (C1, PR (g)).

static bool putUnit( FILE *f, UnsignedInt unit )
{
	return ::fputc( (int)(unit & 0xFF), f ) != EOF && ::fputc( (int)((unit >> 8) & 0xFF), f ) != EOF;
}

Int WideCharFilePut( FILE *f, WideChar c )
{
	const UnsignedInt unit = (UnsignedInt)c & 0xFFFF;
	if (f == NULL || !putUnit( f, unit ))
		return WIDECHAR_FILE_EOF;
	return (Int)unit;
}

Int WideCharFileGet( FILE *f )
{
	if (f == NULL)
		return WIDECHAR_FILE_EOF;
	const int low = ::fgetc( f );
	if (low == EOF)
		return WIDECHAR_FILE_EOF;
	const int high = ::fgetc( f );
	if (high == EOF)
		return WIDECHAR_FILE_EOF;
	return (Int)(((UnsignedInt)high << 8) | (UnsignedInt)low);
}

Int WideCharFileWrite( FILE *f, const WideChar *s )
{
	if (f == NULL || s == NULL)
		return -1;
	Int written = 0;
	for (; *s != 0; ++s, ++written)
	{
		if (!putUnit( f, (UnsignedInt)*s & 0xFFFF ))
			return -1;
	}
	return written;
}
