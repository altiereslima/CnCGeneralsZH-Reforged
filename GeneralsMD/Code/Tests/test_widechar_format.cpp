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

// WideCharFormatV has to print what MSVC's _vsnwprintf prints, because every wide format in this
// tree was written against it - including MSVC's LEGACY wide-format meanings, where %s is a wide
// string and %S a narrow one (the opposite of C99).  On Windows the funnel IS _vsnwprintf; off
// Windows it is a formatter written to match (WideCharFns.cpp says why).
//
// The expected strings below are MSVC's documented behaviour, written out as code units - not the
// output of either implementation.  So this test means something different on each platform, and
// both meanings are wanted:
//   - off Windows it checks the hand-written formatter against MSVC's semantics;
//   - on Windows it checks this file's claims about MSVC's semantics against the real _vsnwprintf.
// A test that passes on both is the two agreeing.  Until a Windows machine runs it, the second half
// is a debt row, and the "golden" lines this prints are what that run should reproduce.
//
// Built without literals of either width - no L"..." and no u"..." - so the file does not change when
// WideChar does, and does not depend on how a compiler reads non-ASCII source.
//
// WHAT THIS DOES NOT PROVE:
//   - MSVC's formatting of floating-point values bit for bit.  %f and %g are checked for values
//     whose decimal form is exact; the last digit of an inexact value is the C library's on each
//     platform (both are correctly rounded, which is a claim about the libraries, not a check).
//   - The exact-fit and %n edge cases on Windows: there MSVC is documented to differ (no terminator
//     at an exact fit; %n raises the invalid-parameter handler), so those two run off Windows only.

#include "PreRTS.h"
#include "Lib/WideCharFns.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

// A WideChar string from ASCII text with {U+XXXX} escapes, e.g. "OYUN SE{00C7}".
struct W
{
	WideChar units[ 256 ];
	explicit W( const char *text )
	{
		size_t n = 0;
		for (const char *p = text; *p != 0 && n < 255; ++n)
		{
			if (*p == '{')
			{
				unsigned value = 0;
				++p;
				while (*p != '}')
				{
					const char c = *p++;
					value = value * 16 + (unsigned)(c >= 'A' ? c - 'A' + 10 : c - '0');
				}
				++p;
				units[n] = (WideChar)value;
			}
			else
				units[n] = (WideChar)(unsigned char)*p++;
		}
		units[n] = 0;
	}
	operator const WideChar *() const { return units; }
};

static void printUnits( const WideChar *s )
{
	for (; *s != 0; ++s)
		printf( (unsigned)*s < 0x80 && *s >= 0x20 ? "%c" : "{%04X}", (unsigned)*s );
}

static void expect( int line, const WideChar *got, Int gotResult, const char *wantText, Int wantResult )
{
	const W want( wantText );
	bool same = gotResult == wantResult;
	size_t i = 0;
	for (; same && want.units[i] != 0; ++i)
		same = got[i] == want.units[i];
	same = same && got[i] == 0;
	printf( "golden\t%d\t%d\t", line, (int)gotResult );
	printUnits( got );
	printf( "\n" );
	if (!same)
	{
		printf( "FAIL line %d: got %d \"", line, (int)gotResult );
		printUnits( got );
		printf( "\", want %d \"%s\"\n", (int)wantResult, wantText );
		++failures;
	}
}

#define CHECK_FORMAT( WANT_TEXT, WANT_RESULT, FORMAT, ... ) \
	do { \
		WideChar out[ 128 ]; \
		const Int result = WideCharFormat( out, 128, W( FORMAT ), __VA_ARGS__ ); \
		expect( __LINE__, out, result, WANT_TEXT, WANT_RESULT ); \
	} while (0)

int main( void )
{
	const W turkish( "OYUN SE{00C7}ENEKLER{0130}" );
	const W pair( "{D83D}{DE00}x" );

	// ---- text and %%, including non-ASCII text in the format itself
	CHECK_FORMAT( "plain", 5, "plain", 0 );
	CHECK_FORMAT( "100%", 4, "100%%", 0 );
	CHECK_FORMAT( "{0130}stanbul", 8, "{0130}stanbul", 0 );

	// ---- MSVC legacy: %s, %ls and %ws are WIDE strings in a wide format
	CHECK_FORMAT( "[OYUN SE{00C7}ENEKLER{0130}]", 18, "[%s]", (const WideChar *)turkish );
	CHECK_FORMAT( "OYUN SE{00C7}ENEKLER{0130}", 16, "%ls", (const WideChar *)turkish );
	CHECK_FORMAT( "OYUN SE{00C7}ENEKLER{0130}", 16, "%ws", (const WideChar *)turkish );
	CHECK_FORMAT( "{D83D}{DE00}x", 3, "%s", (const WideChar *)pair );	// units pass through untouched

	// ---- and %S and %hs are NARROW strings; each byte becomes the code unit of the same value
	CHECK_FORMAT( "abc", 3, "%S", "abc" );
	CHECK_FORMAT( "abc", 3, "%hs", "abc" );
	CHECK_FORMAT( "caf{00E9}", 4, "%S", "caf\xE9" );
	CHECK_FORMAT( "WIDE narrow", 11, "%s %S", (const WideChar *)W( "WIDE" ), "narrow" );

	// ---- characters: %c and %lc wide, %C and %hc narrow
	CHECK_FORMAT( "{0130}", 1, "%c", (int)0x0130 );
	CHECK_FORMAT( "{0130}", 1, "%lc", (int)0x0130 );
	CHECK_FORMAT( "A", 1, "%C", (int)'A' );
	CHECK_FORMAT( "{00E9}", 1, "%hc", (int)(unsigned char)0xE9 );

	// ---- width, precision, alignment, and * from the arguments
	CHECK_FORMAT( "     OYUN SE{00C7}ENEKLER{0130}", 21, "%21s", (const WideChar *)turkish );
	CHECK_FORMAT( "OYU|", 4, "%.3s|", (const WideChar *)turkish );
	CHECK_FORMAT( "OY    |", 7, "%-6.2s|", (const WideChar *)turkish );
	CHECK_FORMAT( "  ab|", 5, "%*S|", 4, "ab" );
	CHECK_FORMAT( "ab  |", 5, "%-*S|", 4, "ab" );
	CHECK_FORMAT( "abc|", 4, "%.*S|", 3, "abcdef" );

	// ---- null strings print as MSVC prints them
	CHECK_FORMAT( "(null)", 6, "%s", (const WideChar *)NULL );
	CHECK_FORMAT( "(null)", 6, "%S", (const char *)NULL );

	// ---- the numeric conversions, delegated to the C library and narrowed back
	CHECK_FORMAT( "42 -7 ff FF 0x1f", 16, "%d %i %x %X %#x", 42, -7, 255, 255, 31 );
	CHECK_FORMAT( "  007|-5  |+3", 13, "%5.3d|%-4d|%+d", 7, -5, 3 );
	CHECK_FORMAT( "4294967295 18446744073709551615", 31, "%u %llu", 4294967295u, 18446744073709551615ull );
	CHECK_FORMAT( "123456789012", 12, "%I64d", 123456789012ll );
	CHECK_FORMAT( "1.500000 0.25 2.5e+06", 21, "%f %g %g", 1.5, 0.25, 2500000.0 );
	CHECK_FORMAT( "  3.14|", 7, "%6.2f|", 3.140625 );
	// A mix, the way the engine writes them: a Turkish name inside a number line.
	CHECK_FORMAT( "Player OYUN SE{00C7}ENEKLER{0130}: 3/4 (75.0%)", 36, "Player %s: %d/%d (%.1f%%)",
		(const WideChar *)turkish, 3, 4, 75.0 );

	// ---- every delegated conversion's output is ASCII: the claim WideCharFns.cpp makes, checked
	{
		WideChar out[ 256 ];
		const Int n = WideCharFormat( out, 256, W( "%d %u %x %o %e %f %g %a %E %G %lld" ), -123456, 987654u,
			0xABCDEFu, 0777u, 6.02e23, -0.001, 1e-300, 1.0, 1e300, 12345.678, -9000000000ll );
		bool ascii = n > 0;
		for (Int i = 0; i < n; ++i)
			ascii = ascii && (unsigned)out[i] < 0x80;
		if (!ascii)
		{
			printf( "FAIL: a delegated numeric conversion produced a non-ASCII code unit\n" );
			++failures;
		}
	}

	// ---- truncation: MSVC's contract - every unit written, none of them a terminator, and negative.
	//      Measured on Windows (W2): _vsnwprintf filled all 8 and wrote no terminator, where this test
	//      used to expect 7 and a terminator.  Compared unit by unit; a guard unit catches a write past.
	{
		WideChar out[ 9 ];
		out[ 8 ] = (WideChar)0x2A2A;
		const Int result = WideCharFormat( out, 8, W( "%s" ), (const WideChar *)turkish );
		const W want( "OYUN SE{00C7}" );
		bool same = result == -1 && out[ 8 ] == (WideChar)0x2A2A;
		for (int i = 0; same && i < 8; ++i)
			same = out[ i ] == want.units[ i ];
		printf( "golden\t%d\t%d\t(8 units, unterminated)\n", __LINE__, (int)result );
		if (!same)
		{
			printf( "FAIL line %d: truncation is not MSVC's 8 units of \"OYUN SE{00C7}\", unterminated, and -1 (got %d)\n",
				__LINE__, (int)result );
			++failures;
		}
	}

#if !defined(_WIN32)
	// ---- the two cases where this deliberately differs from, or refuses more than, MSVC
	{
		// Exactly outCount units: MSVC writes them unterminated and returns 5; off Windows the same
		// five units, and truncation's sign (-1).  Compared unit by unit, a guard catching a write past.
		WideChar out[ 6 ];
		out[ 5 ] = (WideChar)0x2A2A;
		const Int result = WideCharFormat( out, 5, W( "abcde" ), 0 );
		const W want( "abcde" );
		bool same = result == -1 && out[ 5 ] == (WideChar)0x2A2A;
		for (int i = 0; same && i < 5; ++i)
			same = out[ i ] == want.units[ i ];
		if (!same)
		{
			printf( "FAIL line %d: an exact fit is not five units of \"abcde\", unterminated, and -1 (got %d)\n",
				__LINE__, (int)result );
			++failures;
		}
	}
	{
		int ignored = 0;
		WideChar out[ 16 ];
		if (WideCharFormat( out, 16, W( "ab%n" ), &ignored ) >= 0 || ignored != 0)
		{
			printf( "FAIL: %%n was not refused\n" );
			++failures;
		}
	}
#endif

	if (failures != 0)
	{
		printf( "widechar_format: %d failure(s)\n", failures );
		return 1;
	}
	printf( "widechar_format: every case prints what MSVC's legacy wide printf prints\n" );
	return 0;
}
