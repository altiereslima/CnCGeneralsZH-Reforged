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
 *                 Project Name : critsection.cpp                                              *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/wwlib/critsection.cpp                        $*
 *                                                                                             *
 *              Original Author:: Hector Yee                                                   *
 *                                                                                             *
 *                      $Author:: Hector_y                                                    $*
 *                                                                                             *
 *                     $Modtime:: 3/14/01 4:04p                                               $*
 *                                                                                             *
 *                    $Revision:: 1                                                           $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include "critsection.h"

CriticalSectionClass::CriticalSectionClass():
inside(false)
{
}

CriticalSectionClass::~CriticalSectionClass()
{
	WWASSERT(inside==false);	// Can't delete a locked critical section
}

void CriticalSectionClass::Enter()
{
	Bar.lock();
	WWASSERT(inside==false);	// see the note in critsection.h: after the acquire, not before
	inside=true;
	Owner=std::this_thread::get_id();
}

void CriticalSectionClass::Exit()
{
	WWASSERT(inside==true);
	WWASSERT(Owner==std::this_thread::get_id());
	inside=false;
	Owner=std::thread::id();
	Bar.unlock();
}

CriticalSectionClass::LockClass::LockClass(CriticalSectionClass &c):
crit(c)
{
	crit.Enter();
}

CriticalSectionClass::LockClass::~LockClass()
{
	crit.Exit();
}