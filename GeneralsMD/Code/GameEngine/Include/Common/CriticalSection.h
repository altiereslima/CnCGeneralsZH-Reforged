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

// CriticalSection.h ///////////////////////////////////////////////////////
// Utility class to use critical sections in areas of code.
// Author: JohnM And MattC, August 13, 2002

#pragma once

#ifndef __CRITICALSECTION_H__
#define __CRITICALSECTION_H__

#include "Common/PerfTimer.h"

#include <mutex>

#if defined(__APPLE__)
#include <os/lock.h>
#include <pthread.h>
#include <atomic>
#include <stdint.h>
#endif

#ifdef PERF_TIMERS
extern PerfGather TheCritSecPerfGather;
#endif

/*
	This was a raw Win32 CRITICAL_SECTION.  It is a std::recursive_mutex now (B11), and the
	"recursive" is not conservatism - it is required, by one caller, unconditionally:

	    UnicodeString::set( const UnicodeString & )    UnicodeString.cpp:155
	        takes TheUnicodeStringCriticalSection      UnicodeString.cpp:157
	        calls releaseBuffer()                      UnicodeString.cpp:162
	            takes TheUnicodeStringCriticalSection  UnicodeString.cpp:130   <- again, same lock

	The scoped lock taken at :157 is still in scope when :162 runs, and the branch it sits in is
	`if (&stringSrc != this)` - which is the ordinary case, not an edge one.  A Win32
	CRITICAL_SECTION is recursive, so this has always been well-defined; a std::mutex is not, and
	re-locking one you already hold is undefined behaviour rather than a deadlock you would
	notice in a debugger.  So std::mutex is not available to us here, and a future reader who
	thinks the "recursive" looks unnecessary should re-read those four lines before removing it.

	The other three locks this class backs do not recurse - checked, not assumed:

	  TheMemoryPoolCriticalSection  GameMemory.cpp 1644, 1734, 1793, 1814.  The functions reached
	                                under it - createBlob, freeBlob, init, sysAllocateDoNotZero,
	                                sysFree - take no lock of their own.
	  TheDmaCriticalSection         GameMemory.cpp 2182, 2298.  Nests into the pool lock, which is
	                                a different object.
	  TheDebugLogCriticalSection    Debug.cpp 451.  doLogOutput is a leaf: fprintf and
	                                OutputDebugString, no allocation and no way back into DebugLog.

	They share one class, so they get the recursive one too.  The cost of that over a plain mutex
	is an owner check on an uncontended acquire, which is also exactly what CRITICAL_SECTION was
	doing before - this is the mapping that keeps behaviour identical, not a concession.

	Lock ordering, since three of the four nest: Unicode -> Dma -> Pool, in that direction only.
	UnicodeString::releaseBuffer calls TheDynamicMemoryAllocator->freeBytes under the Unicode
	lock, and DynamicMemoryAllocator::allocateBytesDoNotZeroImplementation calls into the pool
	under the Dma lock.  Nothing in GameMemory.cpp takes the Unicode lock, so there is no cycle.
	Keep it that way.

	No spin count is lost in the move.  InitializeCriticalSectionAndSpinCount,
	SetCriticalSectionSpinCount and TryEnterCriticalSection appear nowhere in this tree, so every
	one of these locks was already a plain InitializeCriticalSection with the system default.

	On Apple the lock is os_unfair_lock with an owner and a count (PERF1, 2026-09-27), not libc++'s
	std::recursive_mutex, which is a recursive pthread mutex there.  The allocator takes these locks
	on every block it hands out or takes back, and on an M3 Pro's profile that mutex was 10% of the main
	thread in a skirmish and 14% under mobstress.  What it keeps:
	  - recursion, exactly: the owner re-entering counts up, and the lock is released at zero;
	  - the pairing of every enter with its exit, and every caller's order of locks above;
	  - what the allocator hands out.  No allocator code changes, so on one thread the same calls
	    get the same blocks in the same order.  Between threads, which one wins a contended lock was
	    never ordered (neither a CRITICAL_SECTION nor a pthread mutex is FIFO), so nothing that
	    replays the same could depend on it, and this lock stays inside that same freedom.
	An exit by a thread that does not hold the lock is a programming error on every platform, and here
	it stops the process in every build: one relaxed load and a compare per exit (a second review's finding).
	Without it a non-owner exit at depth above one would quietly count down someone else's depth.  A
	thread that ends while holding the lock is a bug on every platform too; here a later thread given
	the same pthread_t would find itself the owner.
	Windows and Linux keep std::recursive_mutex: Linux's glibc mutex is to be measured before it is
	replaced, and Windows is not ours to measure.
*/
#if defined(__APPLE__)
class RecursiveUnfairLock
{
	os_unfair_lock m_lock = OS_UNFAIR_LOCK_INIT;
	std::atomic<uintptr_t> m_owner{ 0 };	///< the holder's pthread_self(), 0 when free
	unsigned int m_depth = 0;				///< read and written by the holder only

	static uintptr_t self() { return reinterpret_cast<uintptr_t>( pthread_self() ); }

public:
	void lock()
	{
		const uintptr_t me = self();
		// Only this thread ever stores its own id, so a relaxed load that sees it is this thread's own
		// earlier store: the lock is ours already.
		if (m_owner.load( std::memory_order_relaxed ) == me)
		{
			++m_depth;
			return;
		}
		os_unfair_lock_lock( &m_lock );
		m_owner.store( me, std::memory_order_relaxed );
		m_depth = 1;
	}

	void unlock()
	{
		if (m_owner.load( std::memory_order_relaxed ) != self())
			__builtin_trap();		// CriticalSection::exit by a thread that does not hold it
		if (--m_depth == 0)
		{
			m_owner.store( 0, std::memory_order_relaxed );
			os_unfair_lock_unlock( &m_lock );
		}
	}
};
#endif

class CriticalSection
{
#if defined(__APPLE__)
	RecursiveUnfairLock m_mutex;
#else
	std::recursive_mutex m_mutex;
#endif

	public:
		CriticalSection()
		{
			#ifdef PERF_TIMERS
			AutoPerfGather a(TheCritSecPerfGather);
			#endif
		}

		virtual ~CriticalSection()
		{
			#ifdef PERF_TIMERS
			AutoPerfGather a(TheCritSecPerfGather);
			#endif
		}

	public:	// Use these when entering/exiting a critical section.
		void enter( void )
		{
			#ifdef PERF_TIMERS
			AutoPerfGather a(TheCritSecPerfGather);
			#endif
			m_mutex.lock();
		}

		void exit( void )
		{
			#ifdef PERF_TIMERS
			AutoPerfGather a(TheCritSecPerfGather);
			#endif
			m_mutex.unlock();
		}
};

class ScopedCriticalSection
{
	private:
		CriticalSection *m_cs;

	public:
		ScopedCriticalSection( CriticalSection *cs ) : m_cs(cs)
		{
			if (m_cs)
				m_cs->enter();
		}

		virtual ~ScopedCriticalSection( )
		{
			if (m_cs)
				m_cs->exit();
		}
};

// These should be NULL on creation then non-NULL in WinMain or equivalent.
// This allows us to be silently non-threadsafe for WB and other single-threaded apps.
//
// TheAsciiStringCriticalSection used to sit at the top of this list, a FastCriticalSectionClass
// from WWVegas' mutex.h rather than one of these.  It is gone, and so is the #include of mutex.h
// that only it needed: its three uses in AsciiString.h (:378, :389, :450) are all commented out,
// nothing else in the tree named it, and WinMain never assigned it.  mutex.h reaches <intrin.h>
// and _interlockedbittestandset, so a dead extern was keeping MSVC intrinsics in the header that
// the allocator and both string classes compile against.
extern CriticalSection *TheUnicodeStringCriticalSection;
extern CriticalSection *TheDmaCriticalSection;
extern CriticalSection *TheMemoryPoolCriticalSection;
extern CriticalSection *TheDebugLogCriticalSection;

#endif /* __CRITICALSECTION_H__ */
