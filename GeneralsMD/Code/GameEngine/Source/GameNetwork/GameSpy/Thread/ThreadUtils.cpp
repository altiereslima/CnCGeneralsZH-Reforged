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
// Modified 2026 by İlyas Akın for the macOS/Linux port; see NOTICE.md and the git history.

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: ThreadUtils.cpp //////////////////////////////////////////////////////
// GameSpy thread utils
// Author: Matthew D. Campbell, July 2002

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

//-------------------------------------------------------------------------

#include "Lib/WideCharFns.h"

/* UTF-8 in, WideChar out, and every line break a space.  On Windows this is the call it always was -
	 MultiByteToWideChar(CP_UTF8) with the same arguments; its WCHAR and WideChar are the same two
	 unsigned bytes there, which is what makes the cast honest.  Elsewhere it is WideCharFromUtf8,
	 which agrees with it on every well-formed input.  (The two differ only in how many U+FFFD they
	 put where the bytes are not UTF-8 at all.) */
WideCharString MultiByteToWideCharSingleLine( const char *orig )
{
	const size_t len = strlen( orig );
	WideCharString dest( len + 1, (WideChar)0 );	// UTF-8 never needs more units than bytes

#if defined(_WIN32)
	MultiByteToWideChar( CP_UTF8, 0, orig, -1, reinterpret_cast<LPWSTR>( &dest[0] ), (int)len );
#else
	WideCharFromUtf8( orig, &dest[0], len + 1 );
#endif
	dest[len] = 0;
	dest.resize( WideCharLen( dest.c_str() ) );

	for (size_t i = 0; i < dest.size(); ++i)
	{
		if (dest[i] == (WideChar)'\n' || dest[i] == (WideChar)'\r')
			dest[i] = (WideChar)' ';
	}
	return dest;
}

std::string WideCharStringToMultiByte( const WideChar *orig )
{
	std::string ret;
#if defined(_WIN32)
	LPCWSTR wide = reinterpret_cast<LPCWSTR>( orig );	// the same two bytes a unit, as above
	Int len = WideCharToMultiByte( CP_UTF8, 0, wide, (int)WideCharLen( orig ), NULL, 0, NULL, NULL ) + 1;
	if (len > 0)
	{
		char *dest = NEW char[len];
		WideCharToMultiByte( CP_UTF8, 0, wide, -1, dest, len, NULL, NULL );
		dest[len-1] = 0;
		ret = dest;
		delete[] dest;
	}
#else
	// Up to three bytes per unit (a surrogate pair is two units and four bytes), and a terminator.
	std::string buffer( WideCharLen( orig ) * 3 + 1, '\0' );
	buffer.resize( WideCharToUtf8( orig, &buffer[0], buffer.size() ) );
	ret = buffer;
#endif
	return ret;
}

//-------------------------------------------------------------------------

