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

/* $Header: /G/wwlib/bittype.h 4     4/02/99 1:37p Eric_c $ */
/*************************************************************************** 
 ***                  Confidential - Westwood Studios                    *** 
 *************************************************************************** 
 *                                                                         * 
 *                 Project Name : Voxel Technology                         * 
 *                                                                         * 
 *                    File Name : BITTYPE.H                                * 
 *                                                                         * 
 *                   Programmer : Greg Hjelstrom                           * 
 *                                                                         * 
 *                   Start Date : 02/24/97                                 * 
 *                                                                         * 
 *                  Last Update : February 24, 1997 [GH]                   * 
 *                                                                         * 
 *-------------------------------------------------------------------------* 
 * Functions:                                                              * 
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
#if _MSC_VER >= 1000
#pragma once
#endif // _MSC_VER >= 1000

#ifndef BITTYPE_H
#define BITTYPE_H

#include <stdint.h>

/*
** uint32 and sint32 were `unsigned long` and `signed long` until 2026.  That is
** 32 bits under Windows' LLP64 and 64 under LP64, so every struct built out of
** them changed shape off Windows: 50 of the 77 .w3d structs in w3d_file.h, and
** ChunkHeader with them - 8 bytes on disk, 16 under clang - which is the header
** every .w3d chunk is read through and the amount chunkio.cpp:465 advances the
** file position by.  A .w3d was not misread off Windows, it was unreadable past
** its first chunk.  Tests/test_w3dlayout.cpp pins the on-disk layouts so that
** cannot silently come back.
**
** The width on Windows is unchanged by this.  The type IDENTITY is not:
** `unsigned long` and `unsigned int` are distinct types even where both are 32
** bits, so this can move overload resolution and printf-format diagnostics
** there.  See docs/porting/windows-impact.md.
*/
typedef unsigned char	uint8;
typedef unsigned short	uint16;
typedef uint32_t			uint32;
typedef unsigned int    uint;

typedef signed char		sint8;
typedef signed short		sint16;
typedef int32_t			sint32;
typedef signed int      sint;

typedef float				float32;
typedef double				float64;

/*
** The names below are the Windows SDK's, not this library's; bittype.h only
** ever duplicated them.  DWORD and ULONG used to be here too and have been
** REMOVED rather than fixed, which needs explaining.
**
** They could not be fixed in place.  The duplicate of an SDK typedef is legal
** solely by being verbatim, so `typedef uint32_t DWORD;` beside minwindef.h's
** `typedef unsigned long DWORD;` is a hard "typedef redefinition with different
** types" on MSVC in every translation unit that sees both.
**
** They could not be left either: `unsigned long` is 32 bits under Windows'
** LLP64 and 64 under LP64, so off Windows this header was handing out a 64-bit
** DWORD - the same bug as the uint32 one above, in a name that looks like it
** cannot have it.
**
** Removing them is what closes it.  On Windows nothing changes: every
** translation unit in WWVegas that uses DWORD or ULONG also reaches windows.h,
** and in every one of them the include resolves before the first use, so the
** SDK's own typedef is already in scope and always was.  Off Windows the name
** is now simply absent, so a use that needs it fails to compile instead of
** silently getting the wrong width - which is the whole point.
**
** WW3D2/agg_def.h was the one file that used them without reaching windows.h;
** it now uses uint32, which is what it meant.
**
** The six that remain are not width bugs - WORD and USHORT are 2 bytes, BYTE 1,
** BOOL and UINT 4, on both toolchains - and B10 is a widths task, so they are
** deliberately left alone.  If they are ever tidied it is for duplication, not
** correctness, and it is somebody else's task.
*/
typedef unsigned short	WORD;
typedef unsigned char   BYTE;
typedef int             BOOL;
typedef unsigned short	USHORT;
typedef const char *		LPCSTR;
typedef unsigned int    UINT;

#endif //BITTYPE_H
