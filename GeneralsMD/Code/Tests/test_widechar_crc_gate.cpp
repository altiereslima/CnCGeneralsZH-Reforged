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

// B1's gate: the bytes a UnicodeString puts into the replay and network CRC, before and after
// WideChar becomes char16_t.
//
// Xfer::xferUnicodeString (Xfer.cpp:209) hashes sizeof(WideChar) * getLength() bytes of the string,
// so the width of WideChar IS the checksum.  Windows has always had a two-byte, unsigned wchar_t,
// and B1 moves every platform to char16_t, which is also two bytes and unsigned.  This test proves
// the move changes no byte the CRC sees, by hashing the same code units three ways:
//
//   (A) the code before the flip, built with -fshort-wchar: wchar_t is then 16-bit and unsigned,
//       which is MSVC's wchar_t, so this is the Windows model on a Mac or a Linux box
//   (B) the code after the flip: WideChar is char16_t
//   (C) the code before the flip with the platform's own 4-byte wchar_t: the control, which must
//       NOT match, or the table cannot tell a width change from no change at all
//
// The expected values are absolute and were not produced by the code under test: they come from an
// independent re-implementation of XferCRC written from XferCRC.cpp (see the table's comment), and
// the Turkish row agrees with the constant test_widechar_width.inc derived by hand.
//
// This binary is built from the REAL Xfer.cpp, XferCRC.cpp, UnicodeString.cpp, AsciiString.cpp and
// WideCharFns.cpp.  What it stubs, in widechar_test_stubs.cpp, is what those objects name and the hashing path never
// runs: the memory manager (a malloc-backed stand-in, which decides where the bytes live, not what
// they are) and five engine singletons that Xfer's other members reach.
//
// WHAT THIS DOES NOT PROVE:
//   - Anything about MSVC.  (A) is clang's or GCC's reading of MSVC's wchar_t, not MSVC.  The
//     golden lines this prints are there so that a Windows run can be diffed against them.
//   - Where the code units come from.  A UnicodeString built from a literal, a .csf file, the
//     keyboard or the network has to hold these units in the first place; that is what the
//     literal sweep and widechar_check are for.  Here the units are written in by number.
//   - Byte order.  Both targets are little-endian.  A big-endian port would change every row.
//   - The real memory manager, and XferSave/XferLoad's length byte (test_widechar_width.inc's).

#include "PreRTS.h"

#include "Common/UnicodeString.h"
#include "Common/XferCRC.h"

#include <stdio.h>
#include <stdlib.h>

// The stubs this program links with are in widechar_test_stubs.cpp.

// ---------------------------------------------------------------------------------------- table
// Code units, then XferCRC over Xfer::xferUnicodeString at two bytes a unit and at four.
// Computed by an independent Python re-implementation of XferCRC.cpp: addCRC's rotate-and-add
// with htonl, xferImplementation's whole 32-bit words read in native order, and its leftover bytes
// assembled little-endian and then byte-swapped twice (once there, once in addCRC).  Little-endian.
static const unsigned ROW0[] = { 0x004F, 0x0059, 0x0055, 0x004E, 0x0020, 0x0053, 0x0045, 0x00C7, 0x0045, 0x004E, 0x0045, 0x004B, 0x004C, 0x0045, 0x0052, 0x0130 };
static const unsigned ROW1[] = { 0x0041 };
static const unsigned ROW2[] = { 0x0130 };
static const unsigned ROW3[] = { 0x4E2D, 0x6587 };
static const unsigned ROW4[] = { 0xD83D, 0xDE00, 0x0078 };
static const unsigned ROW5[] = { 0xD83D, 0x0078 };
static const unsigned ROW6[] = { 0xDE00 };
static const unsigned ROW7[] = { 0xFFFF, 0xFFFE, 0xFEFF };
static const unsigned ROW8[] = { 0x00FF, 0x0100 };
static const unsigned ROW9[] = { 0 };
static const unsigned ROW10[] = { 0x0001, 0x9E38, 0x3C70, 0xDAA7, 0x78DF, 0x1717, 0xB54E, 0x5386, 0xF1BD, 0x8FF5, 0x2E2D, 0xCC64, 0x6A9C, 0x08D4, 0xA70B, 0x4543, 0xE37A, 0x81B2, 0x1FEA, 0xBE21, 0x5C59, 0xFA90, 0x98C8, 0x3700, 0xD537, 0x736F, 0x11A7, 0xAFDE, 0x4E16, 0xEC4D, 0x8A85, 0x28BD, 0xC6F4, 0x652C, 0x0364, 0xA19B, 0x3FD3, 0xDE0A, 0x7C42, 0x1A7A, 0xB8B1, 0x56E9, 0xF520, 0x9358, 0x3190, 0xCFC7, 0x6DFF, 0x0C37, 0xAA6E, 0x48A6, 0xE6DD, 0x8515, 0x234D, 0xC184, 0x5FBC, 0xFDF3, 0x9C2B, 0x3A63, 0xD89A, 0x76D2, 0x150A, 0xB341, 0x5179, 0xEFB0, 0x8DE8, 0x2C20, 0xCA57, 0x688F, 0x06C7, 0xA4FE, 0x4336, 0xE16D, 0x7FA5, 0x1DDD, 0xBC14, 0x5A4C, 0xF883, 0x96BB, 0x34F3, 0xD32A, 0x7162, 0x0F9A, 0xADD1, 0x4C09, 0xEA40, 0x8878, 0x26B0, 0xC4E7, 0x631F, 0x0157, 0x9F8E, 0x3DC6, 0xDBFD, 0x7A35, 0x186D, 0xB6A4, 0x54DC, 0xF313, 0x914B, 0x2F83, 0xCDBA, 0x6BF2, 0x0A2A, 0xA861, 0x4699, 0xE4D0, 0x8308, 0x2140, 0xBF77, 0x5DAF, 0xFBE6, 0x9A1E, 0x3856, 0xD68D, 0x74C5, 0x12FD, 0xB134, 0x4F6C, 0xEDA3, 0x8BDB, 0x2A13, 0xC84A, 0x6682, 0x04BA, 0xA2F1, 0x4129, 0xDF60, 0x7D98, 0x1BD0, 0xBA07, 0x583F, 0xF676, 0x94AE, 0x32E6, 0xD11D, 0x6F55, 0x0D8D, 0xABC4, 0x49FC, 0xE833, 0x866B, 0x24A3, 0xC2DA, 0x6112, 0xFF49, 0x9D81, 0x3BB9, 0xD9F0, 0x7828, 0x1660, 0xB497, 0x52CF, 0xF106, 0x8F3E, 0x2D76, 0xCBAD, 0x69E5, 0x081D, 0xA654, 0x448C, 0xE2C3, 0x80FB, 0x1F33, 0xBD6A, 0x5BA2, 0xF9D9, 0x9811, 0x3649, 0xD480, 0x72B8, 0x10F0, 0xAF27, 0x4D5F, 0xEB96, 0x89CE, 0x2806, 0xC63D, 0x6475, 0x02AD, 0xA0E4, 0x3F1C, 0xDD53, 0x7B8B, 0x19C3, 0xB7FA, 0x5632, 0xF469, 0x92A1, 0x30D9, 0xCF10, 0x6D48, 0x0B80, 0xA9B7, 0x47EF, 0xE626, 0x845E, 0x2296, 0xC0CD, 0x5F05, 0xFD3C, 0x9B74, 0x39AC, 0xD7E3, 0x761B, 0x1453, 0xB28A, 0x50C2, 0xEEF9, 0x8D31, 0x2B69, 0xC9A0, 0x67D8, 0x0610, 0xA447, 0x427F, 0xE0B6, 0x7EEE, 0x1D26, 0xBB5D, 0x5995, 0xF7CC, 0x9604, 0x343C, 0xD273, 0x70AB, 0x0EE3, 0xAD1A, 0x4B52, 0xE989, 0x87C1, 0x25F9, 0xC430, 0x6268, 0x00A0, 0x9ED7, 0x3D0F, 0xDB46, 0x797E, 0x17B6, 0xB5ED, 0x5425, 0xF25C, 0x9094, 0x2ECC, 0xCD03, 0x6B3B, 0x0973, 0xA7AA, 0x45E2, 0xE419, 0x8251, 0x2089, 0xBEC0, 0x5CF8, 0xFB2F };

struct GateRow
{
	const char *why;
	const unsigned *units;
	Int length;
	UnsignedInt crcAtTwoBytes;
	UnsignedInt crcAtFourBytes;
};

static const GateRow ROWS[] =
{
	{ "turkish OYUN SECENEKLERI, from Data/Turkish/Generals.str; U+0130's high byte is 0x01",
	  ROW0, 16, 0x25265B36u, 0xD22E0198u },
	{ "one ASCII unit: 2 bytes, so all of it goes through XferCRC's leftover-bytes path",
	  ROW1, 1, 0x41000000u, 0x00000041u },
	{ "U+0130 alone, the leftover path with a non-zero high byte",
	  ROW2, 1, 0x30010000u, 0x00000130u },
	{ "CJK U+4E2D U+6587, three-byte UTF-8, one whole 32-bit word",
	  ROW3, 2, 0x65874E2Du, 0x000001E2u },
	{ "U+1F600 as its surrogate pair, then 'x': a word and a leftover",
	  ROW4, 3, 0x3402B07Bu, 0x00001C71u },
	{ "a lone high surrogate, then 'x'",
	  ROW5, 2, 0x0078D83Du, 0x0000B0F3u },
	{ "a lone low surrogate",
	  ROW6, 1, 0x00DE0000u, 0x0000DE00u },
	{ "U+FFFF, U+FFFE, U+FEFF: the top of the BMP and a byte-order mark",
	  ROW7, 3, 0xFEFC0000u, 0x0300F8FDu },
	{ "U+00FF, U+0100: where one byte stops being enough",
	  ROW8, 2, 0x010000FFu, 0x010001FEu },
	{ "empty: XferCRC adds nothing",
	  ROW9, 0, 0x00000000u, 0x00000000u },
	{ "255 units, the save format's length-byte limit, none zero",
	  ROW10, 255, 0x3B8B1B40u, 0x91B7D573u },
};
static const Int ROW_COUNT = (Int)( sizeof( ROWS ) / sizeof( ROWS[0] ) );

// ---------------------------------------------------------------------------------------- test
static UnsignedInt crcOf( UnicodeString text )
{
	XferCRC xfer;
	xfer.open( "widechar_crc_gate" );
	xfer.xferUnicodeString( &text );		// Xfer::xferUnicodeString: XferCRC does not override it
	xfer.close();
	return xfer.getCRC();
}

static UnsignedInt crcOfBytes( unsigned char *bytes, Int count )
{
	XferCRC xfer;
	xfer.open( "widechar_crc_gate" );
	xfer.xferUser( bytes, count );
	xfer.close();
	return xfer.getCRC();
}

int main( void )
{
	int failures = 0;
	const Int width = (Int)sizeof( WideChar );
	const char *model = width == 2 ? "two-byte WideChar: the Windows model, (A) or (B)"
		: width == 4 ? "four-byte WideChar: the control, (C)" : "unexpected";
	printf( "widechar_crc_gate: sizeof(WideChar) == %d, %s\n", width, model );
	if (width != 2 && width != 4) {
		printf( "FAIL: no row of this table describes a %d-byte WideChar\n", width );
		return 1;
	}

	for (Int r = 0; r < ROW_COUNT; ++r) {
		const GateRow &row = ROWS[r];

		// Through the string class, at whatever width this build has.
		WideChar buffer[ 256 ];
		for (Int i = 0; i < row.length; ++i)
			buffer[i] = (WideChar)row.units[i];
		buffer[ row.length ] = 0;
		const UnicodeString text( buffer );
		const UnsignedInt got = crcOf( text );
		const UnsignedInt want = width == 2 ? row.crcAtTwoBytes : row.crcAtFourBytes;
		if (text.getLength() != row.length || got != want) {
			printf( "FAIL row %d (%s): length %d, crc 0x%08X, want length %d, crc 0x%08X\n",
				(int)r, row.why, (int)text.getLength(), (unsigned)got, (int)row.length, (unsigned)want );
			++failures;
		}

		// Width-independent: the same units as explicit two- and four-byte little-endian bytes, so
		// XferCRC itself is checked against the independent model whatever WideChar is.  This is
		// also the control that outlives the flip, when no build has a four-byte WideChar any more.
		unsigned char two[ 512 ], four[ 1024 ];
		for (Int i = 0; i < row.length; ++i) {
			const unsigned u = row.units[i];
			two[2*i] = (unsigned char)u;         two[2*i + 1] = (unsigned char)(u >> 8);
			four[4*i] = (unsigned char)u;        four[4*i + 1] = (unsigned char)(u >> 8);
			four[4*i + 2] = 0;                   four[4*i + 3] = 0;
		}
		if (crcOfBytes( two, 2 * row.length ) != row.crcAtTwoBytes
			|| crcOfBytes( four, 4 * row.length ) != row.crcAtFourBytes) {
			printf( "FAIL row %d (%s): XferCRC over explicit bytes disagrees with the table\n", (int)r, row.why );
			++failures;
		}
		// The table has to be able to tell the widths apart, or matching it proves nothing.
		if (row.length > 0 && row.crcAtTwoBytes == row.crcAtFourBytes) {
			printf( "FAIL row %d (%s): the table's two widths agree, so it cannot discriminate\n", (int)r, row.why );
			++failures;
		}

		// A golden line per row, for diffing a Windows run against this one.
		printf( "golden\t%d\t%d\t0x%08X\n", (int)r, (int)row.length, (unsigned)got );
	}

	if (failures) {
		printf( "widechar_crc_gate: %d failure(s)\n", failures );
		return 1;
	}
	printf( "widechar_crc_gate: %d rows match the %d-byte column\n", (int)ROW_COUNT, (int)width );
	return 0;
}
