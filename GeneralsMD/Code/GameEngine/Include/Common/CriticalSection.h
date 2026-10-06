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

#include <atomic>
#include <stdint.h>

#if defined(__APPLE__)
#include <os/lock.h>
#include <pthread.h>
#else
#include <condition_variable>
#include <cstdlib>
#include <thread>
#if defined(_MSC_VER)
#include <intrin.h>	// _mm_pause, __yield
#endif
#endif

#ifdef PERF_TIMERS
extern PerfGather TheCritSecPerfGather;
#endif

/*
	This was a raw Win32 CRITICAL_SECTION, then std::recursive_mutex (B11), and is now an owner and
	a count over a platform word: RecursiveUnfairLock on Apple, RecursiveLock everywhere else.  It
	stays recursive, as CRITICAL_SECTION was.  The one caller that re-entered, UnicodeString::set
	through releaseBuffer, takes no lock since its count went atomic (2026-10-05), so no caller
	needs the recursion now; keeping it costs one compare per enter.  TheUnicodeStringCriticalSection
	is still declared because WinMain, PosixMain and the tests assign it.  Nothing takes it.

	The three locks in use:

	  TheMemoryPoolCriticalSection  MemoryPool's allocate, free, releaseEmpties and reset in
	                                GameMemory.cpp.  What they call under it takes no lock.
	  TheDmaCriticalSection         DynamicMemoryAllocator's allocate and free.  Nests into the pool
	                                lock, a different object.
	  TheDebugLogCriticalSection    DebugLog in Debug.cpp, under DEBUG_THREADSAFE.  A leaf.

	Lock order: Dma -> Pool, that direction only.  Nothing in GameMemory.cpp takes the Dma lock
	under the pool lock.  Keep it that way.

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

	Everywhere else (2026-10-05) the lock is RecursiveLock below, the same owner and count over a
	three-state word in place of os_unfair_lock.  MSVC's std::recursive_mutex went through
	msvcp140's _Mtx_lock and mtx_do_lock into ntdll's SRW lock on every allocation:
	RtlAcquire/ReleaseSRWLockExclusive were 2.3% and mtx_do_lock 0.6% of the main thread in a
	530-unit fight.  Uncontended, this is one compare-exchange to enter and one exchange to leave,
	inline, the same two locked instructions the SRW lock executes; what goes is the calls around
	them and the second owner check.  A thread that finds the lock taken spins on a CPU pause for a
	short while, as SRW does (JobSystem workers hold the pool lock for well under a microsecond),
	then yields a few times, then sleeps on a condition variable until the holder's exit wakes it,
	so a long hold costs a waiter no CPU.  The owner tag is the address of a thread_local, which costs no call; as with pthread_t
	on Apple, a later thread can be given the address of one that ended, which matters only if that
	one ended holding the lock.  Linux's glibc mutex was never measured against this.  It takes the
	same path because nothing in it is Windows.
*/
#if !defined(__APPLE__)
class RecursiveLock
{
	std::atomic<unsigned int> m_state{ 0 };	///< 0 free, 1 held, 2 held and someone may be asleep
	std::atomic<uintptr_t> m_owner{ 0 };		///< the holder's tag, 0 when free
	unsigned int m_depth = 0;					///< read and written by the holder only
	std::mutex m_parkMutex;						///< guards the sleep, not the lock
	std::condition_variable m_parkCv;

	static uintptr_t self()
	{
		static thread_local char tag;
		return reinterpret_cast<uintptr_t>( &tag );
	}

	bool tryTake()
	{
		unsigned int expected = 0;
		return m_state.compare_exchange_strong( expected, 1, std::memory_order_acquire, std::memory_order_relaxed );
	}

	static void cpuRelax()
	{
	#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
		_mm_pause();
	#elif defined(_MSC_VER) && defined(_M_ARM64)
		__yield();
	#elif defined(__x86_64__) || defined(__i386__)
		__builtin_ia32_pause();
	#elif defined(__aarch64__)
		__asm__ __volatile__( "yield" );
	#endif
	}

	void lockContended()
	{
		for (int spin = 0; spin < 128; ++spin)
		{
			cpuRelax();
			if (m_state.load( std::memory_order_relaxed ) == 0 && tryTake())
				return;
		}
		for (int spin = 0; spin < 16; ++spin)
		{
			std::this_thread::yield();
			if (m_state.load( std::memory_order_relaxed ) == 0 && tryTake())
				return;
		}
		// Marking the word 2 before sleeping is what makes the holder's exit wake someone.  The mark
		// and the check happen under m_parkMutex, and the waker takes m_parkMutex before it notifies,
		// so a wake cannot fall between this thread's check and its wait.  Whoever gets the lock out
		// of here leaves it at 2, which at worst costs one wake nobody needed.
		std::unique_lock<std::mutex> park( m_parkMutex );
		while (m_state.exchange( 2, std::memory_order_acquire ) != 0)
			m_parkCv.wait( park );
	}

	void wake()
	{
		{ std::lock_guard<std::mutex> park( m_parkMutex ); }
		m_parkCv.notify_all();
	}

public:
	void lock()
	{
		const uintptr_t me = self();
		// Only this thread ever stores its own tag, so a relaxed load that sees it is this thread's own
		// earlier store: the lock is ours already.
		if (m_owner.load( std::memory_order_relaxed ) == me)
		{
			++m_depth;
			return;
		}
		if (!tryTake())
			lockContended();
		m_owner.store( me, std::memory_order_relaxed );
		m_depth = 1;
	}

	void unlock()
	{
		if (m_owner.load( std::memory_order_relaxed ) != self())
			std::abort();		// CriticalSection::exit by a thread that does not hold it
		if (--m_depth == 0)
		{
			m_owner.store( 0, std::memory_order_relaxed );
			if (m_state.exchange( 0, std::memory_order_release ) == 2)
				wake();
		}
	}
};
#else
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
	RecursiveLock m_mutex;
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
// nothing else in the tree named it, and WinMain never assigned it.  The <intrin.h> at the top is
// RecursiveLock's own, for the pause in its contended spin, not a leftover of mutex.h.
extern CriticalSection *TheUnicodeStringCriticalSection;
extern CriticalSection *TheDmaCriticalSection;
extern CriticalSection *TheMemoryPoolCriticalSection;
extern CriticalSection *TheDebugLogCriticalSection;

#endif /* __CRITICALSECTION_H__ */
