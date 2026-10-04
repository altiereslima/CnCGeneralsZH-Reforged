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
 *                 Project Name : Command & Conquer                                            * 
 *                                                                                             * 
 *                     $Archive:: /G/wwlib/bool.h                                             $* 
 *                                                                                             * 
 *                      $Author:: Neal_k                                                      $*
 *                                                                                             * 
 *                     $Modtime:: 9/23/99 1:46p                                               $*
 *                                                                                             * 
 *                    $Revision:: 3                                                           $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------* 
 * Functions:                                                                                  * 
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
#if _MSC_VER >= 1000
#pragma once
#endif // _MSC_VER >= 1000

/*
**	This whole file is a 1994 workaround for compilers that did not have `bool` yet, and its own
**	condition says so: (_MSC_VER < 1100) is false on every MSVC since Visual C++ 5, so it has been
**	inert on Windows for twenty-five years.  The trouble is what the condition does on a compiler
**	that does not define _MSC_VER at all - the preprocessor reads the undefined name as 0, (0 < 1100)
**	passes, and clang walks into the #else branch below and tries to `typedef int bool` over a
**	keyword.  Eighteen of wwlib's translation units stopped there.
**
**	Requiring _MSC_VER to be defined says what was meant.  Every compiler that reached the old test
**	reaches the same answer: modern MSVC inert, MSVC before 5 active, Borland and Watcom excluded by
**	their own clauses.  Only the case nobody had in 1994 changes.
*/
#if !defined(TRUE_FALSE_DEFINED) && !defined(__BORLANDC__) && defined(_MSC_VER) && (_MSC_VER < 1100) && !defined(__WATCOMC__)
#define TRUE_FALSE_DEFINED

/**********************************************************************
**      The "bool" integral type was defined by the C++ comittee in
**      November of '94. Until the compiler supports this, use the following
**      definition.
*/
#ifdef _MSC_VER

#include        "yvals.h"
#define bool    unsigned

#elif defined(_UNIX)

/////#define bool    unsigned

#else

enum {false=0,true=1};
typedef int bool;

#endif

#endif
