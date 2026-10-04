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

// FILE: ThreadUtils.h //////////////////////////////////////////////////////
// Generals GameSpy thread utils
// Author: Matthew D. Campbell, July 2002

#pragma once

#ifndef __GAMESPY_THREADUTILS_H__
#define __GAMESPY_THREADUTILS_H__

// A basic_string of WideChar, not std::wstring: the result is built in WideChar units and every
// caller hands its c_str() straight to UnicodeString.  std::wstring stopped being that type the
// moment WideChar became char16_t (B1); this is the one signature that change reached.
WideCharString MultiByteToWideCharSingleLine( const char *orig );
std::string WideCharStringToMultiByte( const WideChar *orig );

#endif // __GAMESPY_THREADUTILS_H__
