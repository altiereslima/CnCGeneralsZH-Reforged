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

/***********************************************************************************************
 ***              C O N F I D E N T I A L  ---  W E S T W O O D  S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : G                                                            *
 *                                                                                             *
 *                     $Archive:: /G/wwlib/wwfile.cpp                                         $*
 *                                                                                             *
 *                      $Author:: Eric_c                                                      $*
 *                                                                                             *
 *                     $Modtime:: 8/19/99 2:36p                                               $*
 *                                                                                             *
 *                    $Revision:: 2                                                           $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include <stdio.h>
#include <stdarg.h>
#include <memory.h>
#include "wwfile.h"

#pragma warning(disable : 4514)

int FileClass::Printf(char *str, ...)
{
	char text[PRINTF_BUFFER_SIZE];
	va_list args;
	va_start(args, str);
	// vsnprintf returns the length it *wanted*, which on truncation is larger than the buffer - so
	// handing it straight to Write() would read past the end.  Microsoft's vsnprintf returned -1
	// here instead, which Write() took as a negative length; neither is defensible, so clamp.
	int length = vsnprintf(text, sizeof(text), str, args);
	va_end(args);
	if (length < 0) return 0;
	if ((size_t)length >= sizeof(text)) length = (int)sizeof(text) - 1;
	return Write(text, length);
}

int FileClass::Printf(char *buffer, int bufferSize, char *str, ...)
{
	va_list args;
	va_start(args, str);
	int length = vsnprintf(buffer, bufferSize, str, args);
	va_end(args);
	if (length < 0) return 0;
	if (length >= bufferSize) length = bufferSize - 1;   // truncated; see Printf above
	return Write(buffer, length);
}

int FileClass::Printf_Indented(unsigned depth, char *str, ...)
{
	char text[PRINTF_BUFFER_SIZE];
	va_list args;
	va_start(args, str);

	if(depth > PRINTF_BUFFER_SIZE) 
		depth = PRINTF_BUFFER_SIZE;

	memset(text, '\t', depth);

	int length;
	if(depth < PRINTF_BUFFER_SIZE) {
		length = vsnprintf(text + depth, PRINTF_BUFFER_SIZE - depth, str, args);
		if (length < 0) length = 0;
		// truncated; see Printf above
		if ((unsigned)length >= PRINTF_BUFFER_SIZE - depth) length = PRINTF_BUFFER_SIZE - depth - 1;
	} else
		length = PRINTF_BUFFER_SIZE;

	va_end(args);

	return Write(text, length + depth);
}

