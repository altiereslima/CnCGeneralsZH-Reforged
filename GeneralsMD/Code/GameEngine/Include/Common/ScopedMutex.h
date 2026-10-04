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

// FILE: ScopedMutex.h ////////////////////////////////////////////////////////////////////////////
// Author: John McDonald, November 2002
// Desc:   A scoped mutex class to easily lock a scope with a pre-existing mutex object.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __SCOPEDMUTEX_H__
#define __SCOPEDMUTEX_H__

#if defined(_WIN32)

class ScopedMutex
{
	private:
		HANDLE m_mutex;

	public:
		ScopedMutex(HANDLE mutex) : m_mutex(mutex)
		{
			DWORD status = WaitForSingleObject(m_mutex, 500);
			if (status != WAIT_OBJECT_0) {
				DEBUG_LOG(("ScopedMutex WaitForSingleObject timed out - status %d\n", status));
			}
		}

		~ScopedMutex()
		{
			ReleaseMutex(m_mutex);
		}
};

#else

#include <chrono>
#include <mutex>

/* Off Windows (C4): the same lock over a std::timed_mutex, which is what AudioFileCache makes there.
	 The behaviour is Windows', kept, not chosen: a wait that runs out after 500 ms goes ahead WITHOUT
	 the lock and says so, as WaitForSingleObject's timeout did, and only a lock that was taken is given
	 back - ReleaseMutex on a mutex the thread does not own fails and does nothing, where unlocking a
	 std::timed_mutex it does not own would be undefined. */
class ScopedMutex
{
	private:
		std::timed_mutex *m_mutex;
		bool m_locked;

	public:
		ScopedMutex(std::timed_mutex *mutex) : m_mutex(mutex), m_locked(false)
		{
			m_locked = m_mutex != NULL && m_mutex->try_lock_for(std::chrono::milliseconds(500));
			if (!m_locked) {
				DEBUG_LOG(("ScopedMutex try_lock_for timed out after 500 ms\n"));
			}
		}

		~ScopedMutex()
		{
			if (m_locked)
				m_mutex->unlock();
		}
};

#endif

#endif /* __SCOPEDMUTEX_H__ */