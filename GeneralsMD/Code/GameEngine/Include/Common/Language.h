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

// FILE: Language.h ///////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//                                                                          
//                       Westwood Studios Pacific.                          
//                                                                          
//                       Confidential Information					         
//                Copyright (C) 2001 - All Rights Reserved                  
//                                                                          
//-----------------------------------------------------------------------------
//
// Project:    RTS3
//
// File name:  Language.h
//
// Created:    Colin Day, June 2001
//
// Desc:       Header for dealing with multiple languages
//
//-----------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __LANGUAGE_H_
#define __LANGUAGE_H_

// SYSTEM INCLUDES ////////////////////////////////////////////////////////////

// USER INCLUDES //////////////////////////////////////////////////////////////
#include "Lib/WideCharFns.h"

// FORWARD REFERENCES /////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
// TYPE DEFINES ///////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

// IMPORTANT: Make sure this enum is identical to the one in Noxstring tool
typedef enum
{

	LANGUAGE_ID_US = 0,
	LANGUAGE_ID_UK,
	LANGUAGE_ID_GERMAN,
	LANGUAGE_ID_FRENCH,
	LANGUAGE_ID_SPANISH,
	LANGUAGE_ID_ITALIAN,
	LANGUAGE_ID_JAPANESE,
	LANGUAGE_ID_JABBER,
	LANGUAGE_ID_KOREAN,
	LANGUAGE_ID_UNKNOWN

} LanguageID;

/* These name the engine's own WideChar functions, not the C library's wchar_t ones.

	 They used to expand straight to wcscpy, wcslen, swscanf and the rest.  That was correct for
	 exactly as long as WideChar was a typedef for wchar_t and wchar_t was two bytes, which is to
	 say for exactly as long as MSVC was the only compiler - clang's wchar_t is four.  Only five
	 of the twenty were ever called, but each one of them was a wchar_t entry point one #include
	 away from any file in the engine, and three of them (_itow, wcsicmp, wcsnicmp) do not exist
	 off Windows under any spelling at all.  Pointing them here is what makes them safe to use
	 again.  See Lib/WideCharFns.h and PORTING.md ("Wide characters").

	 The macros with no shim behind them are gone rather than repointed, listed below so that
	 nothing is lost: repointing a name at a function with different argument or return semantics
	 is worse than not having the name.  None of them had a caller. */
#define GameStrcpy WideCharCpy
#define GameStrncpy WideCharNCpy
#define GameStrlen WideCharLen
#define GameStrcat WideCharCat
#define GameStrcmp WideCharCmp
#define GameStrncmp WideCharNCmp
/* Note: these two fold ASCII only, where wcsicmp/wcsnicmp folded by the C locale.  Nothing calls
	 them today; Lib/WideCharFns.h says why the change is the right one when something does. */
#define GameStricmp WideCharICmp
#define GameStrnicmp WideCharNICmp
#define GameStrstr WideCharStr
#define GameStrchr WideCharChr
#define GameIsDigit WideCharIsDigit
#define GameIsAscii WideCharIsAscii
#define GameIsAlNum WideCharIsAlNum
#define GameIsAlpha WideCharIsAlpha

/* Removed, with what to use instead:

			GameSprintf   swprintf     -> WideCharFormat.  swprintf itself is ambiguous: MSVC's legacy
			                              three-argument form and C99's four-argument one differ in
			                              whether a buffer size is passed.
			GameVsprintf  vswprintf    -> WideCharFormatV.  Same funnel as UnicodeString::format_va,
			                              and it presents MSVC's truncation contract, which vswprintf
			                              does not.
			GameSscanf    swscanf      -> WideCharScan.
			GameStrtok    wcstok       -> no shim.  MSVC's two-argument form and C99's three-argument
			                              reentrant one are different functions with one name; write
			                              the caller first and the shim to match it.
			GameAtoi      wcstol       -> no shim yet.
			GameAtod      wcstod       -> no shim yet.
			GameItoa      _itow        -> no shim.  MSVC-only, and nothing has ever called it.
			GameStrdup    wcsdup       -> was already only a @todo, never defined. */

/* Pure sizeof arithmetic over an array; nothing to do with the width of a character. */
#define GameArrayEnd(array) (array)[(sizeof(array)/sizeof((array)[0]))-1] = 0

// INLINING ///////////////////////////////////////////////////////////////////

// EXTERNALS //////////////////////////////////////////////////////////////////
extern LanguageID OurLanguage;  ///< our current language definition

#endif // __LANGUAGE_H_

