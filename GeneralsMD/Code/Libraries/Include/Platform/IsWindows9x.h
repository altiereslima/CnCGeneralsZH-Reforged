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

// Whether this is Windows 95, 98 or Me, which some languages ship separate font files for
// (Language9x.ini, HeaderTemplate9x.ini) and which cannot draw Asian text in the player-info popup.
//
// Windows asks GetVersionEx, exactly as its three callers - GlobalLanguage, HeaderTemplate and
// PopupPlayerInfo - always did.  Nothing else is Windows 9x, so everywhere else it is false and the
// modern files are used.

#pragma once

#if defined(_WIN32)
#include <windows.h>
inline bool isWindows9x( void )
{
	OSVERSIONINFO	osvi;
	osvi.dwOSVersionInfoSize=sizeof(OSVERSIONINFO);
	return GetVersionEx(&osvi)  &&  osvi.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS;
}
#else
inline bool isWindows9x( void ) { return false; }
#endif
