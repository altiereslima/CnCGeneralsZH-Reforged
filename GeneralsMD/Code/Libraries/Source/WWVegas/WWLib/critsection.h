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
 *                 Project Name : critsection.h                                                *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/wwlib/critsection.h                          $*
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

#ifndef CRIT_SECTION
#define CRIT_SECTION

#if defined(_MSC_VER)
#pragma once
#endif

#include "always.h"
#include "wwdebug.h"

#include <mutex>
#include <thread>

/*
	THIS FILE IS DEAD, and B14 is recording that rather than acting on it.

	Nothing includes critsection.h except critsection.cpp, critsection.cpp is in no target's source
	list, and the class below has the same name - CriticalSectionClass - as a live class in
	WWLib/mutex.h with a different interface and different callers.  Two definitions of one name in
	one library is an ODR violation waiting for the first translation unit that includes both; it
	has never happened only because nothing includes this one.

	It is ported anyway, because leaving the last raw CRITICAL_SECTION in WWVegas in a file marked
	"dead" is how the next sweep finds a fourth copy.  Deleting it instead is probably right and is
	a separate decision: B14 deliberately did not unify the three
	implementations, and deleting one is close enough to that to ask first.

	Unlike mutex.h's, this class is deliberately NOT recursive - Enter() asserted inside==false.
	That assert was itself wrong: it read a plain bool BEFORE acquiring, so a second thread
	arriving while the first held the lock failed it, which is ordinary contention and the whole
	point of the class.  It is taken after the acquire now, where it means what it was written to
	mean.
*/
class CriticalSectionClass
{
public:
	CriticalSectionClass();
	~CriticalSectionClass();

	class LockClass
	{
		CriticalSectionClass& crit;
	public:
		// In order to enter a critical section create a local
		// instance of LockClass with critical section as a parameter.
		LockClass(CriticalSectionClass& c);
		~LockClass();
	private:
		LockClass &operator=(const LockClass&) { return(*this); }
	};
	friend LockClass;

private:
	std::mutex Bar;
	std::thread::id Owner;		// written and read only under Bar
	bool inside;				// likewise
	void Enter();
	void Exit();
};


#endif