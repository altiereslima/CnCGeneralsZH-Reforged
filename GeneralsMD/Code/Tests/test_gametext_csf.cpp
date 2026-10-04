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

// The game's own string table, loaded by the game's own code: B1's "a .csf loads and a known string
// compares equal".
//
// The .csf is the one a Zero Hour install ships, Data\English\generals.csf inside EnglishZH.big, read
// from ZH_DATA_DIR (a folder holding zerohour/, read only).  GameTextManager - the real GameText.cpp,
// with the real RAMFile, File and memory manager under it - loads it through its CSF path, and this
// checks what comes out three ways:
//
//   1. Every one of its 6,422 labels, fetched through GameTextInterface::fetch, against this file's
//      own reading of the same bytes.  That reading is written from the format, not from
//      GameText.cpp: little-endian ints, UTF-16 units stored inverted.  The one thing it copies is
//      stripSpaces' rule for collapsing spaces, transcribed below, so what this checks is the
//      parse, the decode and the width of a WideChar, not stripSpaces itself.
//   2. A handful of labels against their English text, written out here by hand: plain UI words, the
//      copyright line with its (c) sign, a credit with an o-umlaut, and the two strings whose file
//      records carry a 0x1A byte - in a label length and in a string length.  0x1A is Ctrl-Z, the
//      end of a file to a C library reading in Windows text mode; GameText.cpp opens the file
//      BINARY, so it is only a length, and these two prove it is read as one.
//   3. A golden hash over every label and its fetched text, printed so that a Windows run of this
//      test can be diffed against a Mac one line for line, and checked against a constant for the
//      one .csf it was computed from (the 1.04 English file, recognised by its size and hash).
//      Another install's file prints its hash and is not checked against the constant.
//
// Without ZH_DATA_DIR it prints "skip" and returns 77, which ctest reports as Skipped rather than
// Passed.  With ZH_DATA_DIR set and the file missing, it fails.
//
// WHAT THIS DOES NOT PROVE:
//   - The archive file system.  The .big is read here by this file, and the .csf's bytes reach
//     GameText.cpp through RAMFile::openFromMemory; the engine's own BIG reader is not in the path.
//   - The .str path (parseStringFile, which trims with WideCharIsSpace), the language overlays and
//     the map string files.  Only the CSF path runs.
//   - Rendering.  That the text is right is not that it draws.

#include "PreRTS.h"
#include "Common/CriticalSection.h"
#include "Common/ExecutableDirectory.h"
#include "Common/FileSystem.h"
#include "Common/GameMemory.h"
#include "Common/JobSystem.h"
#include "Common/RAMFile.h"
#include "Common/Registry.h"
#include "Common/SubsystemInterface.h"
#include "Common/UnicodeString.h"
#include "GameClient/GameText.h"
#include "GameClient/LanguageFilter.h"
#include "Lib/WideCharFns.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

static int failures = 0;

#define CHECK( COND, ... ) \
	do { if (!(COND)) { printf( "FAIL line %d: ", __LINE__ ); printf( __VA_ARGS__ ); printf( "\n" ); ++failures; } } while (0)

// The .csf this file checks the golden hash against: Zero Hour 1.04, English.
static const unsigned KNOWN_CSF_SIZE = 928775;
static const unsigned KNOWN_CSF_HASH = 0x0FA04110u;
static const unsigned KNOWN_CSF_LABELS = 6422;
static const unsigned KNOWN_STRINGS_HASH = 0x62CF1DC2u;	// also computed by a separate Python reader

static unsigned fnv1a( const void *data, size_t size, unsigned hash = 0x811C9DC5u )
{
	const unsigned char *p = (const unsigned char *)data;
	for (size_t i = 0; i < size; ++i)
		hash = (hash ^ p[i]) * 0x01000193u;
	return hash;
}

//-------------------------------------------------------------------------------------------------
// The .big, read here and not by the engine

static unsigned be32( const unsigned char *p ) { return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | p[3]; }
static unsigned le32( const unsigned char *p ) { return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24); }

static bool sameName( const char *a, const char *b )
{
	for (;; ++a, ++b)
	{
		char x = *a == '/' ? '\\' : *a, y = *b == '/' ? '\\' : *b;
		if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
		if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
		if (x != y) return false;
		if (x == 0) return true;
	}
}

// One entry of a .big ("BIGF", then big-endian offset/size/name records), or empty.
static std::vector<unsigned char> readBigEntry( const std::string &path, const char *entryName )
{
	std::vector<unsigned char> data;
	FILE *file = fopen( path.c_str(), "rb" );
	if (file == NULL)
		return data;
	unsigned char header[ 16 ];
	if (fread( header, 1, 16, file ) == 16 && memcmp( header, "BIGF", 4 ) == 0)
	{
		const unsigned count = be32( header + 8 );
		for (unsigned i = 0; i < count; ++i)
		{
			unsigned char pair[ 8 ];
			if (fread( pair, 1, 8, file ) != 8)
				break;
			std::string name;
			int c;
			while ((c = fgetc( file )) > 0)
				name += (char)c;
			if (sameName( name.c_str(), entryName ))
			{
				data.resize( be32( pair + 4 ) );
				if (fseek( file, (long)be32( pair ), SEEK_SET ) != 0 || fread( data.data(), 1, data.size(), file ) != data.size())
					data.clear();
				break;
			}
		}
	}
	fclose( file );
	return data;
}

//-------------------------------------------------------------------------------------------------
// This file's own reading of a .csf, from the format

struct CsfEntry
{
	std::string label;
	std::vector<WideChar> text;		///< the first string, decoded, before stripSpaces
	bool hasString;
};

static bool readCsf( const std::vector<unsigned char> &b, std::vector<CsfEntry> &out )
{
	// Header: " FSC", version, label count, string count, reserved, language.
	if (b.size() < 24 || memcmp( b.data(), " FSC", 4 ) != 0)
		return false;
	size_t i = 24;
	while (i + 12 <= b.size())
	{
		if (memcmp( &b[i], " LBL", 4 ) != 0)
			return false;
		const unsigned strings = le32( &b[i + 4] ), labelLength = le32( &b[i + 8] );
		i += 12;
		CsfEntry entry;
		entry.label.assign( (const char *)&b[i], labelLength );
		entry.hasString = strings > 0;
		i += labelLength;
		for (unsigned s = 0; s < strings; ++s)
		{
			const bool withWave = memcmp( &b[i], "WRTS", 4 ) == 0;
			if (!withWave && memcmp( &b[i], " RTS", 4 ) != 0)
				return false;
			const unsigned units = le32( &b[i + 4] );
			i += 8;
			for (unsigned u = 0; u < units && s == 0; ++u)
				entry.text.push_back( (WideChar)~(b[i + 2 * u] | (b[i + 2 * u + 1] << 8)) );
			i += 2 * units;
			if (withWave)
				i += 4 + le32( &b[i] );
		}
		out.push_back( entry );
	}
	return i == b.size();
}

// GameText.cpp's stripSpaces, transcribed: runs of spaces become one, and spaces at the start, at the
// end and on either side of a newline or tab go.
static std::vector<WideChar> stripped( const std::vector<WideChar> &in )
{
	std::vector<WideChar> out;
	WideChar last = 0;
	bool skipAll = true;
	for (WideChar ch : in)
	{
		if (ch == 0)
			break;
		if (ch == ' ' && (last == ' ' || skipAll))
			continue;
		if (ch == '\n' || ch == '\t')
		{
			if (last == ' ')
				out.pop_back();
			skipAll = true;
			out.push_back( ch );
			last = ch;
			continue;
		}
		out.push_back( ch );
		last = ch;
		skipAll = false;
	}
	if (last == ' ')
		out.pop_back();
	return out;
}

static std::string printable( const WideChar *s )
{
	std::string out;
	for (; *s != 0; ++s)
	{
		char buf[ 16 ];
		snprintf( buf, sizeof( buf ), (unsigned)*s >= 0x20 && (unsigned)*s < 0x7F ? "%c" : "{%04X}", (unsigned)*s );
		out += buf;
	}
	return out;
}

// A WideChar string from ASCII text with {XXXX} escapes, so that this file holds no non-ASCII byte.
static std::vector<WideChar> units( const char *text )
{
	std::vector<WideChar> out;
	for (const char *p = text; *p != 0;)
	{
		if (*p == '{')
		{
			out.push_back( (WideChar)strtoul( p + 1, NULL, 16 ) );
			p = strchr( p, '}' ) + 1;
		}
		else
			out.push_back( (WideChar)(unsigned char)*p++ );
	}
	return out;
}

static bool equals( const UnicodeString &got, const std::vector<WideChar> &want )
{
	const WideChar *g = got.str();
	size_t i = 0;
	for (; i < want.size(); ++i)
		if (g[i] != want[i])
			return false;
	return g[i] == 0;
}

//-------------------------------------------------------------------------------------------------
// The file system GameText.cpp opens its file through: the .csf's bytes, and nothing else.

static std::vector<unsigned char> s_csf;
static int s_csfOpens = 0;

static const char *CSF_NAME = "Data\\English\\generals.csf";

int main( void )
{
	initMemoryManager();

	const char *dir = getenv( "ZH_DATA_DIR" );
	if (dir == NULL || *dir == 0)
	{
		printf( "skip: no game data (ZH_DATA_DIR)\n" );
		return 77;
	}
	const std::string big = std::string( dir ) + "/zerohour/EnglishZH.big";
	s_csf = readBigEntry( big, CSF_NAME );
	if (s_csf.empty())
	{
		printf( "FAIL: ZH_DATA_DIR is set but %s holds no %s\n", big.c_str(), CSF_NAME );
		return 1;
	}
	const unsigned fileHash = fnv1a( s_csf.data(), s_csf.size() );
	const bool knownFile = s_csf.size() == KNOWN_CSF_SIZE && fileHash == KNOWN_CSF_HASH;
	printf( "csf\t%s\t%u bytes\t0x%08X\t%s\n", CSF_NAME, (unsigned)s_csf.size(), fileHash,
		knownFile ? "the 1.04 English file" : "not the file the constants were computed from" );

	std::vector<CsfEntry> entries;
	CHECK( readCsf( s_csf, entries ), "this file's own reader could not read the .csf" );
	unsigned lengthBytes0x1A = 0;
	for (unsigned char byte : s_csf)
		lengthBytes0x1A += byte == 0x1A;
	printf( "csf\t%u labels\t%u bytes of 0x1A in the file\n", (unsigned)entries.size(), lengthBytes0x1A );

	// The real thing: GameText.cpp's CSF path, through the engine's File and RAMFile.
	TheFileSystem = new FileSystem;
	GameTextInterface *text = CreateGameTextInterface();
	text->init();
	CHECK( s_csfOpens >= 2, "GameText.cpp opened the .csf %d times; getCSFInfo and parseCSF should each open it", s_csfOpens );

	// 1. Every label.
	unsigned hash = 0x811C9DC5u, totalUnits = 0, compared = 0, mismatches = 0;
	for (const CsfEntry &entry : entries)
	{
		Bool exists = FALSE;
		const UnicodeString got = text->fetch( entry.label.c_str(), &exists );
		const std::vector<WideChar> want = stripped( entry.text );
		if (!exists || !equals( got, want ))
		{
			if (mismatches++ < 10)
				printf( "FAIL: %s: got %s\"%s\"\n", entry.label.c_str(), exists ? "" : "(missing) ", printable( got.str() ).c_str() );
			continue;
		}
		++compared;
		// The golden: each label's bytes and a zero, then its text as little-endian UTF-16 and a zero unit.
		hash = fnv1a( entry.label.c_str(), entry.label.size() + 1, hash );
		for (const WideChar *p = got.str(); *p != 0; ++p, ++totalUnits)
		{
			const unsigned char pair[ 2 ] = { (unsigned char)(*p & 0xFF), (unsigned char)(*p >> 8) };
			hash = fnv1a( pair, 2, hash );
		}
		const unsigned char zero[ 2 ] = { 0, 0 };
		hash = fnv1a( zero, 2, hash );
	}
	failures += mismatches;
	printf( "golden\tstrings\t%u of %u equal\t%u units\t0x%08X\n", compared, (unsigned)entries.size(), totalUnits, hash );
	if (knownFile)
	{
		CHECK( entries.size() == KNOWN_CSF_LABELS, "%u labels, want %u", (unsigned)entries.size(), KNOWN_CSF_LABELS );
		CHECK( hash == KNOWN_STRINGS_HASH, "strings hash 0x%08X, want 0x%08X", hash, KNOWN_STRINGS_HASH );
	}

	// 2. Known text, written out by hand.
	struct Known { const char *label; const char *text; };
	static const Known known[] =
	{
		{ "GUI:Ok", "OK" },
		{ "GUI:Cancel", "CANCEL" },
		{ "GUI:Skirmish", "SKIRMISH" },
		{ "GUI:LoadGame", "LOAD GAME" },
		{ "OBJECT:Ranger", "Ranger" },
		{ "GUI:EACopyright", "{00A9} 2003 ELECTRONIC ARTS INC. ALL RIGHTS RESERVED" },
		{ "CREDITS:JorgLindner", "J{00F6}rg Lindner" },
		{ "GUI:BeaconPlaced", "A Beacon was placed by %ls" },				// 26 units: 0x1A in its string length
		{ "GUI:WorldBuilderLoadFailed", "World Builder failed to load" },	// 26 characters: 0x1A in its label length
	};
	for (const Known &k : known)
	{
		Bool exists = FALSE;
		const UnicodeString got = text->fetch( k.label, &exists );
		CHECK( exists && equals( got, units( k.text ) ), "%s: got \"%s\", want \"%s\"", k.label, printable( got.str() ).c_str(), k.text );
		printf( "known\t%s\t%s\n", k.label, printable( got.str() ).c_str() );
	}
	// The two 0x1A records, checked for what the comment says about them, so a different file cannot
	// quietly turn them into ordinary strings.
	if (knownFile)
	{
		CHECK( strlen( "GUI:WorldBuilderLoadFailed" ) == 0x1A, "label length" );
		CHECK( units( "A Beacon was placed by %ls" ).size() == 0x1A, "string length" );
	}

	// A label that is not there comes back through the wide-format funnel's narrow %hs.
	{
		Bool exists = TRUE;
		const UnicodeString got = text->fetch( "NOPE:NotALabel", &exists );
		CHECK( !exists && equals( got, units( "MISSING: 'NOPE:NotALabel'" ) ), "missing label: got \"%s\"", printable( got.str() ).c_str() );
	}

	delete text;
	if (failures != 0)
	{
		printf( "gametext_csf: %d failure(s)\n", failures );
		return 1;
	}
	printf( "gametext_csf: GameText.cpp reads every string in the game's .csf as this file does\n" );
	return 0;
}

//-------------------------------------------------------------------------------------------------
// Stubs.  The memory manager, File and RAMFile are the real ones; these are what GameText.cpp and
// they name that this test does not build.  Anything the CSF path should never reach stops the
// program instead of returning something plausible.

static void notOnThisPath( const char *what )
{
	printf( "FAIL: reached %s, which this test stubs because the CSF path never calls it\n", what );
	fflush( stdout );
	abort();
}

FileSystem *TheFileSystem = NULL;
FileSystem::FileSystem() {}
FileSystem::~FileSystem() {}
void FileSystem::init() {}
void FileSystem::reset() {}
void FileSystem::update() {}

// GameText.cpp asks for the .str first (and gets nothing, as a retail install does), then the .csf,
// then the patch overlay (nothing again).
File *FileSystem::openFile( const Char *filename, Int access )
{
	if (!sameName( filename, CSF_NAME ))
		return NULL;
	if ((access & File::BINARY) == 0)
		notOnThisPath( "a TEXT-mode open of the .csf" );
	++s_csfOpens;
	RAMFile *file = newInstance( RAMFile );
	if (!file->openFromMemory( (const Char *)s_csf.data(), (Int)s_csf.size(), AsciiString( filename ) ))
	{
		file->deleteInstance();
		return NULL;
	}
	return file;
}

SubsystemInterface::SubsystemInterface() {}
SubsystemInterface::~SubsystemInterface() {}

AsciiString GetRegistryLanguage( void ) { return AsciiString( "English" ); }
#if defined(_WIN32)
HWND ApplicationHWnd = NULL;	// GameText.cpp names the game's window through it, and skips a null one
#endif
const Char *g_strFile = "data\\Generals.str";
const Char *g_csfFile = "data\\%s\\Generals.csf";

GlobalData *TheWritableGlobalData = NULL;		// GameText.cpp treats a null one as English
LanguageFilter *TheLanguageFilter = NULL;		// and a null one as no filter
void LanguageFilter::filterLine( UnicodeString & ) { notOnThisPath( "LanguageFilter::filterLine" ); }

CriticalSection *TheUnicodeStringCriticalSection = NULL;	// ScopedCriticalSection skips a null one
CriticalSection *TheDmaCriticalSection = NULL;
CriticalSection *TheMemoryPoolCriticalSection = NULL;

Bool JobSystem::isWorkerThread() { return FALSE; }
void JobSystem::noteWorkerAllocation() {}

// MemoryInit.cpp looks beside the executable for a pool-size override; there is none.
void getExecutableDirectory( char *buf, size_t size, Bool ) { if (size > 0) buf[0] = 0; }

// GameMemory.cpp starts the debug log when the memory manager starts; this test has no log.
#ifdef ALLOW_DEBUG_UTILS
DEBUG_EXTERN_C void DebugInit( int ) {}
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
