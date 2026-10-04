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
// Modified 2026 by İlyas Akın for the macOS/Linux port: parts come from GeneralsMD/Code/Libraries/Source/WWVegas/WWDownload/urlBuilder.cpp, the rest is new code, copyright 2026 İlyas Akın; see NOTICE.md and the git history.

// FILE: WWDownloadRegistryPosix.cpp //////////////////////////////////////////////////////////////
// Desc:   WWDownload's registry API and FormatURLFromRegistry off Windows, over Registry.ini.
///////////////////////////////////////////////////////////////////////////////////////////////////

/* WWDownload is a Windows-only library, but five of its functions are called from gameengine on
	 every platform: OptionsMenu keeps the HTTP proxy with them, ScoreScreen its disconnect and desync
	 counts, PopupPlayerInfo and SkirmishGameOptionsMenu the Preorder flag, MainMenuUtils the version
	 and the patch-server URLs.  On Windows they are WWDownload/registry.cpp and urlBuilder.cpp, on
	 Zero Hour's registry key.  Here they are that key in Registry.ini, through Common/RegistryFile.h,
	 the protocol every reader and writer of the file shares (C1) - so a value written here
	 is the value registry.cpp's readers see, and the other way round.  B6.

	 The getters are the engine's own (registry.cpp, AsciiString forms): same key, same file, same
	 decimal parse, rather than a second copy of it. */

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include <stdio.h>
#include <string>

#include "WWDownload/Registry.h"
#include "WWDownload/urlBuilder.h"

/* WWDownload/urlBuilder.cpp's body, as it is there: the patch, map, config and message-of-the-day URLs
	 from the registry's BaseURL, Language, Version and MapPackVersion.  The servers are gone, and the
	 defaults it starts from are the same on both platforms. */
void FormatURLFromRegistry( std::string& gamePatchURL, std::string& mapPatchURL,
													 std::string& configURL, std::string& motdURL )
{
	std::string sku = "GeneralsZH";
	std::string language = "english";
	unsigned int version = 0; // invalid version - can't get on with a corrupt reg.
	unsigned int mapVersion = 0; // invalid version - can't get on with a corrupt reg.
	std::string baseURL = "http://servserv.generals.ea.com/servserv/";
	baseURL.append(sku);
	baseURL.append("/");

	GetStringFromRegistry("", "BaseURL", baseURL);
	GetStringFromRegistry("", "Language", language);
	GetUnsignedIntFromRegistry("", "Version", version);
	GetUnsignedIntFromRegistry("", "MapPackVersion", mapVersion);

	char buf[256];
	snprintf(buf, 256, "%s%s-%d.txt", baseURL.c_str(), language.c_str(), version);
	gamePatchURL = buf;
	snprintf(buf, 256, "%smaps-%d.txt", baseURL.c_str(), mapVersion);
	mapPatchURL = buf;
	snprintf(buf, 256, "%sconfig.txt", baseURL.c_str());
	configURL = buf;
	snprintf(buf, 256, "%sMOTD-%s.txt", baseURL.c_str(), language.c_str());
	motdURL = buf;
}

/* The engine's registry header comes in only here, below FormatURLFromRegistry: its AsciiString
	 overloads of the two getters and WWDownload's std::string ones both take a string literal, so with
	 both in sight urlBuilder's text above is ambiguous.  Windows compiles that text seeing WWDownload's
	 header alone, and so does this file. */
#include "Common/Registry.h"
#include "Common/RegistryFile.h"

bool GetStringFromRegistry(std::string path, std::string key, std::string& val)
{
	AsciiString value;
	if (!GetStringFromRegistry( AsciiString( path.c_str() ), AsciiString( key.c_str() ), value ))
		return false;
	val = value.str();
	return true;
}

bool GetUnsignedIntFromRegistry(std::string path, std::string key, unsigned int& val)
{
	UnsignedInt value = 0;
	if (!GetUnsignedIntFromRegistry( AsciiString( path.c_str() ), AsciiString( key.c_str() ), value ))
		return false;
	val = value;
	return true;
}

/* An empty value clears the setting on Windows (an empty REG_SZ, which every caller reads as "none":
	 the HTTP proxy box, cleared).  Registry.ini writes it as "key =", which reads as missing, and
	 missing is "none" to the same callers (RegistryFile.h). */
bool SetStringInRegistry(std::string path, std::string key, std::string val)
{
	return writeRegistryFile( registryFileKey( "", AsciiString( path.c_str() ), AsciiString( key.c_str() ) ),
		AsciiString( val.c_str() ) ) ? true : false;
}

bool SetUnsignedIntInRegistry(std::string path, std::string key, unsigned int val)
{
	char text[ 16 ];
	snprintf( text, sizeof( text ), "%u", val );
	return writeRegistryFile( registryFileKey( "", AsciiString( path.c_str() ), AsciiString( key.c_str() ) ),
		AsciiString( text ) ) ? true : false;
}
