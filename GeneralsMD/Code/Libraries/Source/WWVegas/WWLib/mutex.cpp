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

#include "mutex.h"
#include "wwdebug.h"

#include <chrono>


// ----------------------------------------------------------------------------

/*
	The #ifdef _UNIX arms these functions used to carry were not a port; they were a hole.  Each
	one returned success having taken no lock at all - MutexClass::Lock answered true, Unlock did
	nothing, and the constructor built nothing - with a commented-out assert where the
	implementation should have been.  Anything built with _UNIX defined ran the whole engine's
	WWVegas locking as no-ops and would have looked like it worked until two threads met.  They
	are gone: there is one implementation now and it is the same one on every platform.
*/

MutexClass::MutexClass(const char* name) : locked(0)
{
	// See the note in mutex.h: a named, cross-process mutex has no standard equivalent, and
	// nothing in this tree asks for one.
	WWASSERT(name == NULL);
	(void)name;
}

MutexClass::~MutexClass()
{
	WWASSERT(!locked); // Can't delete locked mutex!
}

bool MutexClass::Lock(int time)
{
	if (time == WAIT_INFINITE) {
		Mutex.lock();
	}
	else if (time <= 0) {
		// WaitForSingleObject with a zero timeout is a poll, and callers pass 0 for exactly that.
		if (!Mutex.try_lock()) return false;
	}
	else {
		if (!Mutex.try_lock_for(std::chrono::milliseconds(time))) return false;
	}
	locked++;
	return true;
}

void MutexClass::Unlock()
{
	WWASSERT(locked);
	locked--;
	Mutex.unlock();
}

// ----------------------------------------------------------------------------

MutexClass::LockClass::LockClass(MutexClass& mutex_,int time) : mutex(mutex_)
{
	failed=!mutex.Lock(time);
}

MutexClass::LockClass::~LockClass()
{
	if (!failed) mutex.Unlock();
}







// ----------------------------------------------------------------------------

CriticalSectionClass::CriticalSectionClass() : locked(0)
{
	// The CRITICAL_SECTION this used to hold was heap-allocated as a char array of its size and
	// reached through a void*, which is what you do when a header may not include windows.h.  The
	// mutex is a member now, so the allocation and the cast are both gone.
}

CriticalSectionClass::~CriticalSectionClass()
{
	WWASSERT(!locked); // Can't delete locked mutex!
}

void CriticalSectionClass::Lock()
{
	Mutex.lock();
	locked++;
}

void CriticalSectionClass::Unlock()
{
	WWASSERT(locked);
	locked--;
	Mutex.unlock();
}

// ----------------------------------------------------------------------------

CriticalSectionClass::LockClass::LockClass(CriticalSectionClass& critical_section) : CriticalSection(critical_section)
{
	CriticalSection.Lock();
}

CriticalSectionClass::LockClass::~LockClass()
{
	CriticalSection.Unlock();
}


