/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
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
// Modified 2025-2026 by Olcay Seygan for Zero Hour Reforged; see the git history.
// Modified 2026 by İlyas Akın for the macOS/Linux port; see NOTICE.md and the git history.

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// Registry.cpp
// Simple interface for storing/retreiving registry values
// Author: Matthew D. Campbell, December 2001

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/Registry.h"
#if !defined(_WIN32)
#include "Common/EarlyOptions.h"	// findRegistryFile, findEarlyOptionValueIn, and zh_fopen through it
#include "Common/RegistryFile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <unistd.h>
#endif

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

#if defined(_WIN32)
/*
	A 64-bit process reads HKLM\SOFTWARE natively, and the game's installers are 32-bit: what they
	wrote is under HKLM\SOFTWARE\WOW6432Node, which the native view does not show.  So every read
	asks for the native view first and the 32-bit one after it, which is where an install path
	written by Steam's copy of the game actually is.  On Win32 the second open is the same key as
	the first and costs nothing.
*/
static int openForRead(HKEY root, const char *path, HKEY *handle)
{
	int returnValue = RegOpenKeyEx( root, path, 0, KEY_READ, handle );
	if (returnValue != ERROR_SUCCESS)
	{
		returnValue = RegOpenKeyEx( root, path, 0, KEY_READ | KEY_WOW64_32KEY, handle );
	}
	return returnValue;
}

Bool  getStringFromRegistry(HKEY root, AsciiString path, AsciiString key, AsciiString& val)
{
	HKEY handle;
	unsigned char buffer[256];
	unsigned long size = 256;
	unsigned long type;
	int returnValue;

	if ((returnValue = openForRead( root, path.str(), &handle )) == ERROR_SUCCESS)
	{
		returnValue = RegQueryValueEx(handle, key.str(), NULL, &type, (unsigned char *) &buffer, &size);
		RegCloseKey( handle );
	}

	if (returnValue == ERROR_SUCCESS)
	{
		val = (char *)buffer;
		return TRUE;
	}

	return FALSE;
}

Bool getUnsignedIntFromRegistry(HKEY root, AsciiString path, AsciiString key, UnsignedInt& val)
{
	HKEY handle;
	unsigned char buffer[4];
	unsigned long size = 4;
	unsigned long type;
	int returnValue;

	if ((returnValue = openForRead( root, path.str(), &handle )) == ERROR_SUCCESS)
	{
		returnValue = RegQueryValueEx(handle, key.str(), NULL, &type, (unsigned char *) &buffer, &size);
		RegCloseKey( handle );
	}

	if (returnValue == ERROR_SUCCESS)
	{
		val = *(UnsignedInt *)buffer;
		return TRUE;
	}

	return FALSE;
}

Bool setStringInRegistry( HKEY root, AsciiString path, AsciiString key, AsciiString val)
{
	HKEY handle;
	unsigned long type;
	unsigned long returnValue;
	int size;

	if ((returnValue = RegCreateKeyEx( root, path.str(), 0, "REG_NONE", REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &handle, NULL )) == ERROR_SUCCESS)
	{
		type = REG_SZ;
		size = val.getLength()+1;
		returnValue = RegSetValueEx(handle, key.str(), 0, type, (unsigned char *)val.str(), size);
		RegCloseKey( handle );
	}

	return (returnValue == ERROR_SUCCESS);
}

Bool setUnsignedIntInRegistry( HKEY root, AsciiString path, AsciiString key, UnsignedInt val)
{
	HKEY handle;
	unsigned long type;
	unsigned long returnValue;
	int size;

	if ((returnValue = RegCreateKeyEx( root, path.str(), 0, "REG_NONE", REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &handle, NULL )) == ERROR_SUCCESS)
	{
		type = REG_DWORD;
		size = 4;
		returnValue = RegSetValueEx(handle, key.str(), 0, type, (unsigned char *)&val, size);
		RegCloseKey( handle );
	}

	return (returnValue == ERROR_SUCCESS);
}

Bool GetStringFromGeneralsRegistry(AsciiString path, AsciiString key, AsciiString& val)
{
	AsciiString fullPath = "SOFTWARE\\Electronic Arts\\EA Games\\Generals";

	fullPath.concat(path);
	DEBUG_LOG(("GetStringFromRegistry - looking in %s for key %s\n", fullPath.str(), key.str()));
	if (getStringFromRegistry(HKEY_CURRENT_USER, fullPath.str(), key.str(), val))
	{
		return TRUE;
	}

	return getStringFromRegistry(HKEY_LOCAL_MACHINE, fullPath.str(), key.str(), val);
}

Bool GetStringFromRegistry(AsciiString path, AsciiString key, AsciiString& val)
{
	AsciiString fullPath = "SOFTWARE\\Electronic Arts\\EA Games\\Command and Conquer Generals Zero Hour";

	fullPath.concat(path);
	// HKCU first, HKLM second. The machine-wide hive needs administrator rights to write, so a
	// per-user or Steam install writes only HKCU - and a stale HKLM entry left by some other copy of
	// the game used to win, sending this one at another install's language or data folder.
	DEBUG_LOG(("GetStringFromRegistry - looking in %s for key %s\n", fullPath.str(), key.str()));
	if (getStringFromRegistry(HKEY_CURRENT_USER, fullPath.str(), key.str(), val))
	{
		return TRUE;
	}

	return getStringFromRegistry(HKEY_LOCAL_MACHINE, fullPath.str(), key.str(), val);
}

Bool GetUnsignedIntFromRegistry(AsciiString path, AsciiString key, UnsignedInt& val)
{
	AsciiString fullPath = "SOFTWARE\\Electronic Arts\\EA Games\\Command and Conquer Generals Zero Hour";

	fullPath.concat(path);
	DEBUG_LOG(("GetUnsignedIntFromRegistry - looking in %s for key %s\n", fullPath.str(), key.str()));
	if (getUnsignedIntFromRegistry(HKEY_CURRENT_USER, fullPath.str(), key.str(), val))
	{
		return TRUE;
	}

	return getUnsignedIntFromRegistry(HKEY_LOCAL_MACHINE, fullPath.str(), key.str(), val);
}

#else
/* Off Windows there is no registry.  The same three reads come from one file, Registry.ini in the user
	 data directory, in Options.ini's "key = value" form and read by the same parser, EarlyOptions'
	 findEarlyOptionValueIn: case-insensitive keys, the last one wins.  A Zero Hour value's key is its
	 registry path below the game's key, then its name - "Language", or "ergc\\<name>" for the \\ergc
	 subkey - and an original-Generals value's is the same under "Generals\\".  Windows' two hives, per
	 user and per machine, are one file here.  Common/RegistryFile.h has the whole protocol, and gives
	 WWDownload's registry functions this file's reader and writer.

	 Nothing in the engine proper writes the file: on Windows the installer writes the registry, and
	 here that is the future launcher's or installer's job (C1).  Where there is no file, or
	 no user data directory, every caller keeps its compiled-in default - GetRegistryLanguage's
	 "english", GetRegistryVersion's 65536.  The user data directory is EarlyOptions.h's
	 findUserDataDirectory (C1 (e)). */

Bool readRegistryFileAt( const char *file, const AsciiString &name, AsciiString &val )
{
	FILE *fp = zh_fopen( file, "r" );		// the path is spelled as the engine spells paths (C1 (d))
	if (fp == NULL)
		return FALSE;
	char value[ 256 ];
	const bool found = findEarlyOptionValueIn( fp, name.str(), value, sizeof( value ) );
	fclose( fp );
	if (found)
		val = value;
	return found ? TRUE : FALSE;
}

Bool readRegistryFile( const AsciiString &name, AsciiString &val )
{
	char file[ 1024 ];
	if (!findRegistryFile( file, sizeof( file ) ))		// every reader and writer takes the path from there
		return FALSE;
	return readRegistryFileAt( file, name, val );
}

AsciiString registryFileKey( const char *tree, const AsciiString &path, const AsciiString &key )
{
	AsciiString name = tree;
	const char *below = path.str();
	while (*below == '\\')
		++below;
	if (*below != 0)
	{
		name.concat( below );
		name.concat( '\\' );
	}
	name.concat( key );
	return name;
}

/** Whether findEarlyOptionValueIn could give back exactly `val`: it reads a value to the line's end,
	* trims blanks after the '=' and at the end, and readRegistryFileAt reads it into 256 bytes.  (An
	* empty value is written anyway, and reads as missing: see RegistryFile.h.)  The write checks the
	* file it made in any case; this says why up front. */
static Bool valueReadsBack( const AsciiString &val )
{
	const char *text = val.str();
	const size_t length = ::strlen( text );
	if (length == 0)
		return TRUE;
	if (length > 255 || ::strpbrk( text, "\r\n" ) != NULL)
		return FALSE;
	const char first = text[ 0 ], last = text[ length - 1 ];
	return (first != ' ' && first != '\t' && last != ' ' && last != '\t') ? TRUE : FALSE;
}

Bool writeRegistryFileAt( const char *file, const AsciiString &name, const AsciiString &val )
{
	if (name.isEmpty() || !valueReadsBack( val ))
		return FALSE;

	std::string contents;
	FILE *in = zh_fopen( file, "r" );
	if (in != NULL)
	{
		char chunk[ 1024 ];
		size_t count;
		while ((count = ::fread( chunk, 1, sizeof( chunk ), in )) > 0)
			contents.append( chunk, count );
		::fclose( in );
	}
	else if (zh_access( file, F_OK ) == 0)
		return FALSE;		// there, but not readable: writing a new one would lose what it holds

	// The last line that sets the key, by the reader's own test (EarlyOptions.h), is the one replaced.
	// Lines end at '\n' and keep it.
	size_t lastStart = std::string::npos, lastEnd = 0;
	for (size_t start = 0; start < contents.size(); )
	{
		size_t end = contents.find( '\n', start );
		end = (end == std::string::npos) ? contents.size() : end + 1;
		if (earlyOptionLineValue( contents.substr( start, end - start ).c_str(), name.str() ) != NULL)
		{
			lastStart = start;
			lastEnd = end;
		}
		start = end;
	}

	std::string line = name.str();
	line += val.isEmpty() ? " =" : " = ";		// "name =": no blank for the reader to trim, and nothing after it
	line += val.str();
	line += '\n';
	if (lastStart != std::string::npos)
		contents.replace( lastStart, lastEnd - lastStart, line );
	else
	{
		if (!contents.empty() && contents[ contents.size() - 1 ] != '\n')
			contents += '\n';	// the last line had no line ending; ours starts a line of its own
		contents += line;
	}

	// Written beside the file and renamed over it, so a reader sees the old file or the new one; and
	// renamed only if the reader reads the value back from it as written - or, for an empty value,
	// reads the key as missing - which covers what the line-by-line view above cannot see (a name the
	// reader would not match, or a line longer than the reader's 1024-byte buffer, which it reads in
	// pieces).
	AsciiString temporary;
	temporary.format( "%s.%d", file, (int)::getpid() );
	FILE *out = zh_fopen( temporary.str(), "w" );
	if (out == NULL)
		return FALSE;
	const Bool wrote = ::fwrite( contents.data(), 1, contents.size(), out ) == contents.size();
	const Bool closed = ::fclose( out ) == 0;
	AsciiString readBack;
	const Bool found = readRegistryFileAt( temporary.str(), name, readBack );
	const Bool readsBack = val.isEmpty() ? !found : (found && readBack.compare( val ) == 0);
	if (!wrote || !closed || !readsBack || zh_rename( temporary.str(), file ) != 0)
	{
		zh_remove( temporary.str() );
		return FALSE;
	}
	return TRUE;
}

Bool writeRegistryFile( const AsciiString &name, const AsciiString &val )
{
	char file[ 1024 ];
	if (!findRegistryFile( file, sizeof( file ) ))
		return FALSE;
	return writeRegistryFileAt( file, name, val );
}

Bool GetStringFromGeneralsRegistry(AsciiString path, AsciiString key, AsciiString& val)
{
	return readRegistryFile( registryFileKey( "Generals\\", path, key ), val );
}

Bool GetStringFromRegistry(AsciiString path, AsciiString key, AsciiString& val)
{
	return readRegistryFile( registryFileKey( "", path, key ), val );
}

/** A REG_DWORD on Windows; here decimal text.  Text that is not a whole decimal number reads as missing. */
Bool GetUnsignedIntFromRegistry(AsciiString path, AsciiString key, UnsignedInt& val)
{
	AsciiString text;
	if (!readRegistryFile( registryFileKey( "", path, key ), text ))
		return FALSE;
	char *end = NULL;
	const unsigned long parsed = strtoul( text.str(), &end, 10 );
	if (end == text.str() || *end != 0)
		return FALSE;
	val = (UnsignedInt)parsed;
	return TRUE;
}
#endif

// NOTE: static causes a memory leak, but we have to keep it because the value is cached.
static AsciiString s_registryLanguage = "english";
static Bool s_registryLanguageCached = FALSE;

AsciiString GetRegistryLanguage(void)
{
	if (!s_registryLanguageCached) {
		s_registryLanguageCached = TRUE;
		GetStringFromRegistry("", "Language", s_registryLanguage);
	}
	return s_registryLanguage;
}

void SetRegistryLanguage(const AsciiString &language)
{
	s_registryLanguageCached = TRUE;
	s_registryLanguage = language;
}

AsciiString GetRegistryGameName(void)
{
	AsciiString val = "GeneralsMPTest";
	GetStringFromRegistry("", "SKU", val);
	return val;
}

UnsignedInt GetRegistryVersion(void)
{
	UnsignedInt val = 65536;
	GetUnsignedIntFromRegistry("", "Version", val);
	return val;
}

UnsignedInt GetRegistryMapPackVersion(void)
{
	UnsignedInt val = 65536;
	GetUnsignedIntFromRegistry("", "MapPackVersion", val);
	return val;
}
