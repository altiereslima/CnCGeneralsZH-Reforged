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

/*************************************************************************** 
 ***    C O N F I D E N T I A L  ---  W E S T W O O D  S T U D I O S     *** 
 *************************************************************************** 
 *                                                                         * 
 *                 Project Name : G                                        * 
 *                                                                         * 
 *                     $Archive:: /G/wwlib/strtok_r.h                     $* 
 *                                                                         * 
 *                      $Author:: Neal_k2                                 $* 
 *                                                                         * 
 *                     $Modtime:: 4/13/00 1:33p                           $* 
 *                                                                         * 
 *                    $Revision:: 2                                       $* 
 *                                                                         * 
 *-------------------------------------------------------------------------* 
 * Functions:                                                              * 
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
#if _MSC_VER >= 1000
#pragma once
#endif // _MSC_VER >= 1000

#if defined(_MSC_VER)
#pragma once
#endif

#ifndef __STRTOK_R_H__
#define __STRTOK_R_H__

/*
**	POSIX declares strtok_r in <string.h> with C linkage; declaring it again here with C++ linkage
**	is a hard error, not a redundancy.  The tree's own _UNIX guard was the right idea and the wrong
**	predicate - _UNIX is never defined anywhere in this build, and defining it would arm sixty other
**	arms that were never finished.  Ask about the compiler that actually lacks the function.
*/
#if defined(_MSC_VER)
char *strtok_r(char *strptr, const char *delimiters, char **lasts);
#endif

#endif
